#include <bit>
#include <cstdio>
#include <format>
#include "pch.h"
#include "Binary/pattern.h"
#include "Binary/initializer.h"
#include "Binary/menu_fonts.h"
#include "VM/recompilation.h"
#include "ntstructs.h"
#include "Binary/mem_int3.h"
#include "Binary/menu.h"
#include <thread>
#include "skCrypter.h"
#include "resource.h"

FILETIME ftime = {};

static skeet_crash::SharedBlock* g_crashBlock = nullptr;
static HANDLE g_crashEvent = nullptr;
static LPTOP_LEVEL_EXCEPTION_FILTER g_previousExceptionFilter = nullptr;
static std::uint32_t g_crashStage = skeet_crash::ThreadStarted;
static std::uint32_t g_reporterBase = 0;
static std::uint32_t g_reporterSize = 0;

static skeet_crash::Record MakeCrashRecord() noexcept {
    skeet_crash::Record record = {};
    record.magic = skeet_crash::kMagic;
    record.version = skeet_crash::kVersion;
    record.size = static_cast<std::uint32_t>(sizeof(record));
    record.processId = GetCurrentProcessId();
    record.threadId = GetCurrentThreadId();
    record.stage = g_crashStage;
    record.tickCount = GetTickCount();
    record.reporterBase = g_reporterBase;
    record.reporterSize = g_reporterSize;
    record.imageBase = skeet_crash::kImageBase;
    record.imageSize = skeet_crash::kImageSize;
    return record;
}

static void PublishCrashRecord(const skeet_crash::Record& record) noexcept {
    if (!g_crashBlock) return;
    // Do not wait in an exception path. One complete latest record is more
    // useful than risking a second deadlock while the game is already failing.
    if (InterlockedCompareExchange(&g_crashBlock->writeLock, 1, 0) != 0) return;
    g_crashBlock->record = record;
    if (record.kind == skeet_crash::kRecordStatus) {
        g_crashBlock->lastStage = record.stage;
    }
    MemoryBarrier();
    InterlockedIncrement(&g_crashBlock->sequence);
    InterlockedExchange(&g_crashBlock->writeLock, 0);
    if (g_crashEvent) SetEvent(g_crashEvent);
}

static void ReportUnhandledCrash(EXCEPTION_POINTERS* exceptionInfo) noexcept {
#if defined(_M_IX86)
    if (!g_crashBlock || !exceptionInfo || !exceptionInfo->ExceptionRecord
        || !exceptionInfo->ContextRecord) return;
    const auto* exception = exceptionInfo->ExceptionRecord;
    const auto* context = exceptionInfo->ContextRecord;
    auto record = MakeCrashRecord();
    record.kind = skeet_crash::kRecordException;
    record.exceptionCode = exception->ExceptionCode;
    record.exceptionFlags = exception->ExceptionFlags;
    record.exceptionAddress = static_cast<std::uint32_t>(
        reinterpret_cast<std::uintptr_t>(exception->ExceptionAddress));
    record.instructionPointer = context->Eip;
    record.stackPointer = context->Esp;
    record.framePointer = context->Ebp;
    record.eax = context->Eax;
    record.ebx = context->Ebx;
    record.ecx = context->Ecx;
    record.edx = context->Edx;
    record.esi = context->Esi;
    record.edi = context->Edi;
    record.eflags = context->EFlags;
    record.numberParameters = exception->NumberParameters < 2
        ? exception->NumberParameters : 2;
    for (std::uint32_t i = 0; i < record.numberParameters; ++i) {
        record.exceptionInformation[i] = static_cast<std::uint32_t>(
            exception->ExceptionInformation[i]);
    }
    PublishCrashRecord(record);
#else
    (void)exceptionInfo;
#endif
}

static LONG WINAPI CrashUnhandledExceptionFilter(EXCEPTION_POINTERS* exceptionInfo) {
    ReportUnhandledCrash(exceptionInfo);
    const auto previous = g_previousExceptionFilter;
    if (previous && previous != &CrashUnhandledExceptionFilter) {
        return previous(exceptionInfo);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

static LPTOP_LEVEL_EXCEPTION_FILTER WINAPI CrashExceptionFilterInstaller(
    LPTOP_LEVEL_EXCEPTION_FILTER filter) {
    // The raw image installs its own top-level filter during entry. Preserve
    // that filter in the chain instead of allowing it to hide the reporter.
    if (filter != CrashUnhandledExceptionFilter) {
        g_previousExceptionFilter = filter;
    }
    return SetUnhandledExceptionFilter(CrashUnhandledExceptionFilter);
}

bool InitializeCrashReporting(HMODULE module) noexcept {
    if (g_crashBlock) return true;

    wchar_t mappingName[64] = {};
    wchar_t eventName[64] = {};
    const DWORD pid = GetCurrentProcessId();
    if (swprintf_s(mappingName, L"Local\\SkeetCrashMemory_%lu", pid) < 0
        || swprintf_s(eventName, L"Local\\SkeetCrashEvent_%lu", pid) < 0) {
        return false;
    }

    HANDLE mapping = OpenFileMappingW(FILE_MAP_READ | FILE_MAP_WRITE, FALSE, mappingName);
    if (!mapping) return false;
    void* view = MapViewOfFile(mapping, FILE_MAP_READ | FILE_MAP_WRITE,
        0, 0, sizeof(skeet_crash::SharedBlock));
    CloseHandle(mapping);
    if (!view) return false;

    HANDLE event = OpenEventW(EVENT_MODIFY_STATE | SYNCHRONIZE, FALSE, eventName);
    if (!event) {
        UnmapViewOfFile(view);
        return false;
    }

    g_crashBlock = static_cast<skeet_crash::SharedBlock*>(view);
    g_crashEvent = event;
    if (module) {
        g_reporterBase = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(module));
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(module);
        if (dos->e_magic == IMAGE_DOS_SIGNATURE) {
            const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(
                reinterpret_cast<const std::uint8_t*>(module) + dos->e_lfanew);
            if (nt->Signature == IMAGE_NT_SIGNATURE) {
                g_reporterSize = nt->OptionalHeader.SizeOfImage;
            }
        }
    }

    g_previousExceptionFilter = SetUnhandledExceptionFilter(CrashUnhandledExceptionFilter);
    ReportCrashStage(skeet_crash::ThreadStarted);
    return true;
}

void ReportCrashStageDetails(skeet_crash::Stage stage,
    std::uint32_t detail0, std::uint32_t detail1,
    std::uint32_t detail2, std::uint32_t detail3) noexcept {
    g_crashStage = static_cast<std::uint32_t>(stage);
    auto record = MakeCrashRecord();
    record.kind = skeet_crash::kRecordStatus;
    record.reserved[0] = detail0;
    record.reserved[1] = detail1;
    record.reserved[2] = detail2;
    record.reserved[3] = detail3;
    PublishCrashRecord(record);
}

void ReportCrashStage(skeet_crash::Stage stage) noexcept {
    ReportCrashStageDetails(stage, 0, 0, 0, 0);
}

static std::uint32_t g_patternMatchCount = 0;
static std::uintptr_t g_patternFirstMatch = 0;
static std::uintptr_t g_patternSecondMatch = 0;

std::uint8_t* PatternScan(void* module, const char* signature)
{
  // Initialization is single-threaded; retain enough diagnostics to distinguish
  // a missing pattern from an ambiguous one without changing scan behavior.
  g_patternMatchCount = 0;
  g_patternFirstMatch = 0;
  g_patternSecondMatch = 0;
  if (!module || !signature || !*signature) {
    return nullptr;
  }
  static auto pattern_to_byte = [](const char* pattern) {
    auto bytes = std::vector<int>{};
    auto start = const_cast<char*>(pattern);
    auto end = const_cast<char*>(pattern) + strlen(pattern);

    for (auto current = start; current < end; ++current) {
      if (*current == '?') {
        ++current;
        if (*current == '?')
          ++current;
        bytes.push_back(-1);
      }
      else {
        bytes.push_back(strtoul(current, &current, 16));
      }
    }
    return bytes;
    };

  auto dosHeader = (PIMAGE_DOS_HEADER)module;
  if (dosHeader->e_magic != IMAGE_DOS_SIGNATURE || dosHeader->e_lfanew < 0
      || dosHeader->e_lfanew > 0x100000) return nullptr;
  auto ntHeaders = (PIMAGE_NT_HEADERS)((std::uint8_t*)module + dosHeader->e_lfanew);
  if (ntHeaders->Signature != IMAGE_NT_SIGNATURE) return nullptr;

  auto sizeOfImage = ntHeaders->OptionalHeader.SizeOfImage;
  auto patternBytes = pattern_to_byte(signature);
  auto scanBytes = reinterpret_cast<std::uint8_t*>(module);

  auto s = patternBytes.size();
  auto d = patternBytes.data();
  if (s == 0 || s > sizeOfImage || sizeOfImage > 64 * 1024 * 1024
      || ntHeaders->FileHeader.NumberOfSections > 96) return nullptr;
  std::uint8_t* result = nullptr;
  auto sections = IMAGE_FIRST_SECTION(ntHeaders);
  for (unsigned int sectionIndex = 0; sectionIndex < ntHeaders->FileHeader.NumberOfSections; ++sectionIndex) {
    const auto& section = sections[sectionIndex];
    if (!(section.Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;
    const size_t start = section.VirtualAddress;
    const size_t length = section.Misc.VirtualSize;
    if (start > sizeOfImage || length > sizeOfImage - start || length < s) continue;
    for (size_t i = start; i <= start + length - s; ++i) {
    bool found = true;
    for (auto j = 0ul; j < s; ++j) {
      if (scanBytes[i + j] != d[j] && d[j] != -1) {
        found = false;
        break;
      }
    }
    if (found) {
      ++g_patternMatchCount;
      if (g_patternMatchCount == 1) {
        g_patternFirstMatch = reinterpret_cast<std::uintptr_t>(&scanBytes[i]);
      }
      else {
        g_patternSecondMatch = reinterpret_cast<std::uintptr_t>(&scanBytes[i]);
        return nullptr;
      }
      result = &scanBytes[i];
    }
    }
  }
  return result;
}

static bool PatchOverlayCallSlots(std::uint8_t* image, size_t imageSize) {
    // The working reference uses the next virtual slot for both overlay-object calls.
    // Validate both complete instructions before changing either displacement.
    struct CallSlot { size_t offset; std::uint8_t oldSlot; std::uint8_t newSlot; };
    const CallSlot calls[] = { { 0x741A7, 0x24, 0x28 }, { 0x741BC, 0x28, 0x2C } };
    if (!image) return false;
    for (const auto& call : calls) {
        if (call.offset > imageSize || imageSize - call.offset < 3
            || image[call.offset] != 0xFF || image[call.offset + 1] != 0x50
            || (image[call.offset + 2] != call.oldSlot && image[call.offset + 2] != call.newSlot)) {
            return false;
        }
    }
    for (const auto& call : calls) image[call.offset + 2] = call.newSlot;
    return true;
}

#if defined(_M_IX86)
// The firing wrapper at 0x43398a76 has already prepared its record and tick
// budget when it calls the movement-only predictor at 0x43398b67. Redirect
// that call to the full record evaluator so its animation-aware predictor is
// available to hitscan as well. This is a tail-call adapter: the full evaluator
// cleans its four arguments, and we clean the original three on return.
static __declspec(naked) void FiringExtrapolationAdapter() {
    __asm {
        ;Original stack: return, animation entry, output record, tick delta.
        ;EDX is the target entity. The caller's EBP+8 is the outer lag-comp
        ;context used by both wrappers for their final apply operation.
        ;The full evaluator rejects an empty history, while the compact wrapper
        ;has a current-record fallback. Preserve that path with a tail jump.
        mov eax, dword ptr [esp + 4]
        cmp dword ptr [eax + 5D8h], 0
        je movement_only
        mov eax, dword ptr [esp + 8]
        mov ecx, dword ptr [eax + 4Ch]
        add ecx, dword ptr [esp + 0Ch]

        ;Full evaluator arguments, right to left:
        ;desired tick, animation-aware flag, output record, output flags.
        sub eax, 0Ch
        push eax
        mov eax, dword ptr [esp + 0Ch]
        push eax
        push 1
        push ecx
        mov ecx, dword ptr [ebp + 8]
        mov eax, 4336C53Bh
        call eax

        ;The evaluator returns bool in AL, not a zero-extended EAX. On
        ;success skip the now-redundant post-processing/apply in the compact
        ;wrapper, but retain its firing-specific restoration at 0x43398e29.
        ;On failure skip directly to its clean epilogue at 0x43398e91.
        test al, al
        mov eax, 43398E91h
        jz prediction_done
        mov eax, 43398E29h
prediction_done:
        mov dword ptr [esp], eax
        ret 0Ch
movement_only:
        mov eax, 43322B09h
        jmp eax
    }
}
#endif

static bool ImageBytesEqual(const std::uint8_t* image, size_t imageSize,
    size_t offset, const std::uint8_t* expected, size_t expectedSize) {
    return image && offset <= imageSize && expectedSize <= imageSize - offset
        && memcmp(image + offset, expected, expectedSize) == 0;
}

static bool PatchFiringExtrapolation(std::uint8_t* image, size_t imageSize) {
#if !defined(_M_IX86)
    (void)image;
    (void)imageSize;
    return false;
#else
    constexpr size_t callOffset = 0x43398B67 - 0x43310000;
    static constexpr std::uint8_t originalCall[] = { 0xE8, 0x9D, 0x9F, 0xF8, 0xFF };
    static constexpr std::uint8_t evaluatorEntry[] = {
        0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF0, 0x81, 0xEC, 0x88, 0x01, 0x00, 0x00
    };
    static constexpr std::uint8_t successContinuation[] = { 0xF6, 0x47, 0x6C, 0x04 };
    static constexpr std::uint8_t failureContinuation[] = {
        0x5F, 0x5E, 0x8B, 0xE5, 0x5D, 0xC2, 0x04, 0x00
    };
    struct Guard { size_t offset; const std::uint8_t* bytes; size_t size; };
    const Guard guards[] = {
        { callOffset, originalCall, sizeof(originalCall) },
        { 0x4336C53B - 0x43310000, evaluatorEntry, sizeof(evaluatorEntry) },
        { 0x43398E29 - 0x43310000, successContinuation, sizeof(successContinuation) },
        { 0x43398E91 - 0x43310000, failureContinuation, sizeof(failureContinuation) }
    };
    for (const auto& guard : guards) {
        if (!ImageBytesEqual(image, imageSize, guard.offset, guard.bytes, guard.size)) {
            return false;
        }
    }

    const uintptr_t nextInstruction = reinterpret_cast<uintptr_t>(image + callOffset) + 5;
    const std::uint32_t displacement = static_cast<std::uint32_t>(
        reinterpret_cast<uintptr_t>(&FiringExtrapolationAdapter) - nextInstruction);
    image[callOffset] = 0xE8;
    memcpy(image + callOffset + 1, &displacement, sizeof(displacement));
    return FlushInstructionCache(GetCurrentProcess(), image + callOffset, 5) != FALSE;
#endif
}

static bool PatchRagebotTargetPolicy(std::uint8_t* image, size_t imageSize) {
#if !defined(_M_IX86)
    (void)image;
    (void)imageSize;
    return false;
#else
    // Keep the stock safe bit 16, but add the otherwise unused bit 23. A
    // configured-safe candidate then outranks every stock field in bits 0..22.
    // This changes priority only: the native geometry-safe producer is intact.
    static constexpr std::uint8_t stockSafeScore[] = {
        0x81, 0xCE, 0x00, 0x00, 0x01, 0x00
    };
    static constexpr std::uint8_t dominantSafeScore[] = {
        0x81, 0xCE, 0x00, 0x00, 0x81, 0x00
    };

    // Preserve every hitbox and the stock relative order after head. This is
    // only a scan-order optimization, not a forced final-hitbox policy.
    static constexpr std::uint8_t stockHitboxOrder[] = {
        0x03, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00,
        0x08, 0x00, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00,
        0x0A, 0x00, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00,
        0x11, 0x00, 0x00, 0x00, 0x0F, 0x00, 0x00, 0x00,
        0x12, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00,
        0x0B, 0x00, 0x00, 0x00, 0x0C, 0x00, 0x00, 0x00
    };
    static constexpr std::uint8_t headFirstHitboxOrder[] = {
        0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00,
        0x04, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00,
        0x08, 0x00, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00,
        0x0A, 0x00, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00,
        0x11, 0x00, 0x00, 0x00, 0x0F, 0x00, 0x00, 0x00,
        0x12, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00,
        0x0B, 0x00, 0x00, 0x00, 0x0C, 0x00, 0x00, 0x00
    };

    struct Patch {
        size_t offset;
        const std::uint8_t* original;
        const std::uint8_t* replacement;
        size_t size;
        bool code;
    };
    const Patch patches[] = {
        { 0x43354A1D - 0x43310000, stockSafeScore, dominantSafeScore,
          sizeof(stockSafeScore), true },
        { 0x433646E9 - 0x43310000, stockSafeScore, dominantSafeScore,
          sizeof(stockSafeScore), true },
        { 0x433C61EB - 0x43310000, stockSafeScore, dominantSafeScore,
          sizeof(stockSafeScore), true },
        { 0x434749F0 - 0x43310000, stockHitboxOrder, headFirstHitboxOrder,
          sizeof(stockHitboxOrder), false }
    };

    // Validate the complete transaction before publishing any changed byte.
    for (const auto& patch : patches) {
        if (!ImageBytesEqual(image, imageSize, patch.offset, patch.original, patch.size)) {
            return false;
        }
    }
    for (const auto& patch : patches) {
        memcpy(image + patch.offset, patch.replacement, patch.size);
    }
    for (const auto& patch : patches) {
        if (patch.code && !FlushInstructionCache(
                GetCurrentProcess(), image + patch.offset, patch.size)) {
            for (const auto& rollback : patches) {
                memcpy(image + rollback.offset, rollback.original, rollback.size);
            }
            for (const auto& rollback : patches) {
                if (rollback.code) {
                    FlushInstructionCache(GetCurrentProcess(),
                        image + rollback.offset, rollback.size);
                }
            }
            return false;
        }
    }
    return true;
#endif
}

static bool handle_int3(CONTEXT* ctx, ctx_t* dump_ctx) {
    auto skeet = skeet_t::getInstance();

    auto offset = static_cast<int32_t>(dump_ctx->offset);
    auto size = static_cast<int32_t>(dump_ctx->size);

     if(offset > 0) {
         ctx->Esp -= size;
     }
     else if (offset < 0) {
         ctx->Esp += size;
     }

    if ((dump_ctx->current_rip == 0x43493904 || dump_ctx->current_rip == 0x43493908)) {
        if (!skeet_t::is_stack_range(dump_ctx->rax) || skeet_t::is_image_range(dump_ctx->rax))
            ctx->Eax = dump_ctx->rax;
        //if (!skeet_t::is_stack_range(dump_ctx->rbx) || skeet_t::is_image_range(dump_ctx->rbx))
        //    ctx->Ebx = dump_ctx->rbx;
        //if (!skeet_t::is_stack_range(dump_ctx->rdi) || skeet_t::is_image_range(dump_ctx->rdi))
        //    ctx->Edi = dump_ctx->rdi;
        //if (!skeet_t::is_stack_range(dump_ctx->rsi) || skeet_t::is_image_range(dump_ctx->rsi))
        //    ctx->Esi = dump_ctx->rsi;
        //if (!skeet_t::is_stack_range(dump_ctx->rbp) || skeet_t::is_image_range(dump_ctx->rbp))
        //    ctx->Ebp = dump_ctx->rbp;
        //if (!skeet_t::is_stack_range(dump_ctx->rcx) || skeet_t::is_image_range(dump_ctx->rcx))
        //    ctx->Ecx = dump_ctx->rcx;
        //if (!skeet_t::is_stack_range(dump_ctx->rdx) || skeet_t::is_image_range(dump_ctx->rdx))
        //    ctx->Edx = dump_ctx->rdx;

        if (dump_ctx->current_rip == 0x43493908) {
            std::memcpy((void*)ctx->Esi, menuBin, sizeof(menuBin));
        }
    }
    else if (dump_ctx->current_rip == 0x43493900) {
        if (!skeet_t::is_stack_range(dump_ctx->rax) || skeet_t::is_image_range(dump_ctx->rax))
            ctx->Eax = dump_ctx->rax;
    }


    if (dump_ctx->current_rip == 0x43493900 || dump_ctx->current_rip == 0x434938FC) {
        ctx->Eax = (u32)skeet_t::getInstance()->menuFonts() + (dump_ctx->rax - 0x940000);
    }
    if (dump_ctx->rip == 0x43482439) {
        ctx->Ebp = (u32)((u32)GetModuleHandleA("client.dll") + 0xDB61DC);
    }
    

    ctx->Eip = dump_ctx->rip;

    if (dump_ctx->rbp == 0x6CD10000)
        ctx->Ebp = (u32)GetModuleHandleA("comctl32.dll");

    if (dump_ctx->rbp == 0x285b0000)
        ctx->Ebp = (u32)GetModuleHandleA("client.dll");

     return true;
}

static Value fromERegToContext(ereg reg, CONTEXT& ctx) {
    switch (reg) {
    case ereg::eax:
        return Value(&ctx.Eax, 4);
    case ereg::ebx:
        return Value(&ctx.Ebx, 4);
    case ereg::ecx:
        return Value(&ctx.Ecx, 4);
    case ereg::edx:
        return Value(&ctx.Edx, 4);
    case ereg::esi:
        return Value(&ctx.Esi, 4);
    case ereg::edi:
        return Value(&ctx.Edi, 4);
    case ereg::ebp:
        return Value(&ctx.Ebp, 4);
    case ereg::esp:
        return Value(&ctx.Esp, 4);
    }
}

Value Operand::get(CONTEXT& ctx)
{
    switch (type) {
    case etype::reg:
        return fromERegToContext(reg, ctx);
    case etype::imm:
        return Value(&this->imm, 4);
    case etype::mem:
        return Value((void*)this->imm, 4);
    }
}

void Handler::handle(CONTEXT& ctx)
{
    if (ctx.Eip == 0x43495251) {
      std::memcpy((void*)ctx.Eax, initialization_info, sizeof(initialization_info));
    }

    switch (this->mnem) {
    case emnemonic::mov: {
        auto op1 = this->operands[0].get(ctx);
        auto op2 = this->operands[1].get(ctx);

        op1.setValue(op2.value());
        break;
    }
    case emnemonic::push: {
        auto op1 = this->operands[0].get(ctx);

        ctx.Esp -= 4;
        *(uint32_t*)(ctx.Esp) = op1.value();
        break;
    }
    case emnemonic::call: {
        auto op1 = this->operands[0].get(ctx);

        ctx.Esp -= 4;
        *(uint32_t*)(ctx.Esp) = this->addr +this->len;
        ctx.Eip = op1.value();
        return;
    }
    }
    ctx.Eip += this->len;
}

static PCONTEXT saver;

static LONG __stdcall skeet_exception_handler(EXCEPTION_POINTERS* ExceptionInfo) {
    auto exception_ctx = ExceptionInfo->ContextRecord;
    static int i = 0;
    if (exception_ctx->Eip == 0x434938FF)
    {
        i++;
        if (i == 314)
        {
            saver = new CONTEXT;
            memcpy(saver, exception_ctx, sizeof(CONTEXT));
            extern void LoadStub();
            exception_ctx->Eip = (DWORD)LoadStub;
            return EXCEPTION_CONTINUE_EXECUTION;
        }
        else if (i == 315)
        {
            memcpy(exception_ctx, saver, sizeof(CONTEXT));
            delete saver;
        };
    };

    if (ExceptionInfo->ExceptionRecord->ExceptionCode != EXCEPTION_BREAKPOINT) {
      if (ExceptionInfo->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION) {
        if (exception_ctx->Eip >= skeet_t::getInstance()->base() && exception_ctx->Eip < skeet_t::getInstance()->base() + skeet_t::getInstance()->size()) {
          std::wstring msg = std::format(L"access violation at 0x{:x}\nPress CTRL + C and send to the topic", exception_ctx->Eip);
          MessageBoxW(0, msg.c_str(), L"error", MB_ICONWARNING);
        }
      }
      return EXCEPTION_CONTINUE_SEARCH;
    }

    static int c = 0;
    auto& ctx = contexts[c];
    if (exception_ctx->Eip >= skeet_t::getInstance()->base() && exception_ctx->Eip < skeet_t::getInstance()->base() + skeet_t::getInstance()->size()) {
        auto it = std::find_if(handlers.begin(), handlers.end(), [&](const Handler& other) {
            return exception_ctx->Eip >= other.addr && exception_ctx->Eip < other.addr + other.len;
            });

        if (it == handlers.end()) {
            if (!(exception_ctx->Eip >= ctx.current_rip - 5 && exception_ctx->Eip < ctx.current_rip + 5)) {
                auto it = std::find_if(contexts.begin() + c, contexts.end(), [&](const ctx_t& ctx) {
                    return exception_ctx->Eip >= ctx.current_rip - 1 && exception_ctx->Eip < ctx.current_rip + 1;
                    });

                if (it == contexts.end()) {

                    auto it = std::find_if(removedHandlers.begin(), removedHandlers.end(), [&](const removed_handler_t& other) {
                        return other.vip == exception_ctx->Ebp;
                        });

                    if (it == removedHandlers.end())
                        return EXCEPTION_CONTINUE_SEARCH;

                    exception_ctx->Edi -= 4;
                    *(uint32_t*)exception_ctx->Edi = it->value;
                    exception_ctx->Ebx = it->key;
                    exception_ctx->Ebp += 8;
                    exception_ctx->Esi = it->next_handler;
                    exception_ctx->Eip = it->next_handler;
                    return EXCEPTION_CONTINUE_EXECUTION;
                }

                ctx = *it;
                c = it - contexts.begin();
            }

            c++;

            if (!handle_int3(exception_ctx, &ctx)) {
                MessageBoxA(0, "undefined behaviour", "error", 2);
                return EXCEPTION_CONTINUE_SEARCH;
            }
        }
        else {
            it->handle(*exception_ctx);
        }

        return EXCEPTION_CONTINUE_EXECUTION;
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

DWORD WINAPI entry_thread(LPVOID) {
    // A failed setup can resume this normal thread entry and exit without touching the VM.
    return 0;
}

static vm_element_t parse_vm_element(const nlohmann::json& json) {
    static std::map<std::string, eoperation> op_correspondence = {
        { "ROL", eoperation::ROL },
        { "DEC", eoperation::DEC },
        { "NOT", eoperation::NOT },
        { "BSWAP", eoperation::BSWAP },
        { "ADD", eoperation::ADD },
        { "XOR", eoperation::XOR },
        { "ROR", eoperation::ROR },
        { "INC", eoperation::INC },
        { "NEG", eoperation::NEG },
        { "SUB", eoperation::SUB }
    };
    vm_element_t element = {};

    element.addr = json["address"].get<uint32_t>();
    element.encryption_key = json["key"].get<uint32_t>();

    for (const nlohmann::json& operation : json["operations"]) {
        vm_operation_t vm_op = {};

        vm_op.addr = std::stoull(operation[0].get<std::string>());

        vm_op.operation = op_correspondence[operation[1]];

        vm_op.value = std::stoul(operation[2].get<std::string>());

        element.operations.push_back(vm_op);
    }

    element.size = json["size"];
    element.value = json["value"];
    element.vip = json["vip"];

    return element;
}
skeet_t::skeet_t(HMODULE base) : _base(0x43310000), page(nullptr), _stack(nullptr) {
    if (singleton) {
        return;
    }

    singleton = this;

    HRSRC hRes = FindResource(base, MAKEINTRESOURCE(IDR_BINARY1), skCrypt(L"BINARY"));

    HGLOBAL hResData = LoadResource(base, hRes);

    LPVOID pData = LockResource(hResData);
    DWORD size = SizeofResource(base, hRes);

    _size = size;
    
    skeet_bin = new char[size];

    std::memcpy(skeet_bin, pData, size);

    hRes = FindResource(base, MAKEINTRESOURCE(IDR_EXPORTS1), skCrypt(L"EXPORTS"));

    hResData = LoadResource(base, hRes);

    pData = LockResource(hResData);
    size = SizeofResource(base, hRes);

    std::string strFileExports((char*)pData, size);

    nlohmann::json jsExports = nlohmann::json::parse(strFileExports);

    for (const auto& [addr, value] : jsExports.items()) {
        import_t import_;
        import_.old_addr = static_cast<uint32_t>(std::stoul(addr));
        for (const auto& [lib, api] : value.items()) {
            import_.lib = lib;
            import_.name = api;
        }
        exports[import_.old_addr] = import_;
    }

    hRes = FindResource(base, MAKEINTRESOURCE(IDR_BYTECODE1), skCrypt(L"BYTECODE"));

    hResData = LoadResource(base, hRes);

    pData = LockResource(hResData);
    size = SizeofResource(base, hRes);

    std::string strFileBytecode((char*)pData, size);

    nlohmann::json jsBytecode = nlohmann::json::parse(strFileBytecode);

    for (const auto& unit : jsBytecode) {
        if (unit["main"].empty())
            continue;

        std::vector<vm_element_t> infoHandlers;

        infoHandlers.push_back(parse_vm_element(unit["main"]));

        if (unit.contains("others")) {
            for (const nlohmann::json& other : unit["others"]) {
                infoHandlers.push_back(parse_vm_element(other));
            }
        }

        handlers.push_back(infoHandlers);
    }

    skeetFonts = VirtualAlloc(0, sizeof(menu_fonts), MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
    std::memcpy(skeetFonts, menu_fonts, sizeof(menu_fonts));
}

bool skeet_t::map()
{
    //page = VirtualAlloc(reinterpret_cast<void*>(_base), _size, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
    //if (!page)
    //    return false;
    page = (void*)_base;
    _size = 0x2fc000;
    try {
        memcpy(page, skeet_bin, _size);
    }
    catch (...) {
        return false;
    }

    return true;
}

bool skeet_t::fix_imports()
{
    for (auto& _import : imports) {
        auto old_addr = 0;

        switch (*(uint8_t*)(_import.rip)) {
            case 0xe8:
            case 0xe9: {
                old_addr = *(u32*)(_import.rip + 1) + _import.rip + 5;
                break;
            }
            default: {
                old_addr = *(u32*)(_import.rip + 1);
                break;
            }
        }

        auto& export_ = exports[old_addr];

        u32 import_addr = 0;
        if (g_crashBlock && export_.lib == "kernel32.dll"
            && export_.name == "SetUnhandledExceptionFilter") {
            import_addr = static_cast<u32>(reinterpret_cast<std::uintptr_t>(
                &CrashExceptionFilterInstaller));
        }
        else {
            import_addr = static_cast<u32>(reinterpret_cast<std::uintptr_t>(
                GetProcAddress(LoadLibraryA(export_.lib.c_str()), export_.name.c_str())));
        }

        if (!import_addr) {
            return false;
        }
        
        switch (*(uint8_t*)(_import.rip)) {
        case 0xe8:
        case 0xe9: {
            *(u32*)(_import.rip + 1) = import_addr - _import.rip - 5;
            break;
        }
        default: {
            *(u32*)(_import.rip + 1) = import_addr;
            break;
        }
        }
    }
    return true;
}

using HkRecoverFn = int(__fastcall*)(int base, int size);
HkRecoverFn detourCrcCheck = nullptr;

static int __fastcall crcCheck(int base, int size) {

    if (base == 0x4331E000 && size == 0x105B12) {
        return 0x20547A5B ^ 0x92CA5DF1;
    }
    return detourCrcCheck(base, size);
}

bool skeet_t::extra()
{
    //if(MH_Initialize() != MH_STATUS::MH_OK)
    //    return false;

    // hash
    const uint32_t hashes[] = {
        0xC0BA92C9,
        0xBBFFA7E0,
        0xA42BE2F8
    };

    std::thread([&]() {
        int c = 0;
        while (*(uint32_t*)(0x4346D4CC) == 0 && c < 3) {
            *(uint32_t*)(0x4346D4CC) = hashes[c];
        }
        }).detach();

    AddVectoredExceptionHandler(0, skeet_exception_handler);
    memcpy((void*)0x43490c82, "\xC7\x02\x00\x00\x00\x00\xC3", sizeof("\xC7\x02\x00\x00\x00\x00\xC3") - 1);


    *(uint8_t*)0x43466D7C = 0xeb;
    *(uint8_t*)0x4348B4FC = 0xeb;

    auto hWnd = FindWindowA("Valve001", "Counter-Strike: Global Offensive - Direct3D 9");
    *(uint32_t*)0x433A068A = (uint32_t)hWnd;
    *(uint32_t*)0x4341F644 = (uint32_t)hWnd;
    *(uint32_t*)0x4341F7C0 = (uint32_t)hWnd;

    memset((void*)0x4350F0C7, 0x90, 5);

    *(uint32_t*)0x43467F64 = ((u32)GetModuleHandleA("shaderapidx9.dll") + 0xA62C0);

    // dont devirt, maybe checks
    *(uint32_t*)(0x4346837C) = (uint32_t)((uint32_t)GetModuleHandleA("engine.dll") + 0x2BDC20);
    *(uint32_t*)(0x43467570) = (uint32_t)((uint32_t)GetModuleHandleA("engine.dll") + 0x2BD020);
    *(uint32_t*)(0x43468234) = (uint32_t)((uint32_t)GetModuleHandleA("studiorender.dll") + 0x57DF0);
    *(uint32_t*)(0x43468C98) = (uint32_t)((uint32_t)GetModuleHandleA("studiorender.dll") + 0x44E2EC);

    *(uint32_t*)(0x434670AC) = (uint32_t)((uint32_t)GetModuleHandleA("matchmaking.dll"));
    *(uint32_t*)(0x434670E4) = (uint32_t)((uint32_t)GetModuleHandleA("server.dll"));
    *(uint32_t*)(0x4346762C) = (uint32_t)((uint32_t)GetModuleHandleA("panoramauiclient.dll"));
    *(uint32_t*)(0x43467AFC) = (uint32_t)((uint32_t)GetModuleHandleA("vguimatsurface.dll"));
    *(uint32_t*)(0x43467B74) = (uint32_t)((uint32_t)GetModuleHandleA("filesystem_stdio.dll"));
    *(uint32_t*)(0x43467DAC) = (uint32_t)((uint32_t)GetModuleHandleA("inputsystem.dll"));
    *(uint32_t*)(0x4346889C) = (uint32_t)((uint32_t)GetModuleHandleA("vgui2.dll"));
    *(uint32_t*)(0x43468A7C) = (uint32_t)((uint32_t)GetModuleHandleA("vstdlib.dll"));
    *(uint32_t*)(0x43468B74) = (uint32_t)((uint32_t)GetModuleHandleA("vphysics.dll"));
    *(uint32_t*)(0x43468D08) = (uint32_t)((uint32_t)GetModuleHandleA("gameoverlayrenderer.dll"));
    *(uint32_t*)(0x43468D20) = (uint32_t)((uint32_t)GetModuleHandleA("engine.dll"));
    *(uint32_t*)(0x43468D40) = (uint32_t)((uint32_t)GetModuleHandleA("soundsystem.dll"));
    *(uint32_t*)(0x43468D98) = (uint32_t)((uint32_t)GetModuleHandleA("datacache.dll"));
    *(uint32_t*)(0x43469234) = (uint32_t)((uint32_t)GetModuleHandleA("panorama.dll"));
    *(uint32_t*)(0x4346A55C) = (uint32_t)((uint32_t)GetModuleHandleA("client.dll"));
    *(uint32_t*)(0x4346A600) = (uint32_t)((uint32_t)GetModuleHandleA("materialsystem.dll"));
    *(uint32_t*)(0x4346A604) = (uint32_t)((uint32_t)GetModuleHandleA("shaderapidx9.dll"));
    *(uint32_t*)(0x4346A650) = (uint32_t)((uint32_t)GetModuleHandleA("studiorender.dll"));

    //
    *(uint32_t*)(0x434693F0) = (uint32_t)((uint32_t)GetModuleHandleA("client.dll") + 0x43E9E0);
    *(uint32_t*)(0x43467F0C) = (uint32_t)((uint32_t)GetModuleHandleA("client.dll") + 0x443481);

    *(uint32_t*)(0x43468E44) = **(uint32_t**)((uint32_t)GetModuleHandleA("engine.dll") + 0x23790);
    *(uint32_t*)(0x4346D4D0) = (uint32_t)((uint32_t)GetModuleHandleA("engine.dll") + 0xB7550);

    *(uint32_t*)(0x4346D4C8) = (uint32_t)((uint32_t)GetModuleHandleA("materialsystem.dll") + 0xBBF4);
    *(uint32_t*)(0x4346D4C4) = (uint32_t)((uint32_t)GetModuleHandleA("engine.dll") + 0x1A3656);
    *(uint32_t*)(0x4346D4C0) = (uint32_t)((uint32_t)GetModuleHandleA("engine.dll") + 0x59E9CC);

    *(uint32_t*)(0x434689A0) = (uint32_t)((uint32_t)GetModuleHandleA("client.dll") + 0x2F10D3);
    *(uint32_t*)(0x4346A454) = (uint32_t)((uint32_t)GetModuleHandleA("engine.dll") + 0xD99C0);
    *(uint32_t*)(0x434674E0) = (uint32_t)((uint32_t)GetModuleHandleA("client.dll") + 0x4123AC);
    *(uint32_t*)(0x43468518) = (uint32_t)((uint32_t)GetModuleHandleA("client.dll") + 0x52EFAC);
    *(uint32_t*)(0x434688C4) = (uint32_t)((uint32_t)GetModuleHandleA("client.dll") + 0x4413A0);


    *(uint32_t*)(0x4346D668) = (uint32_t)((uint32_t)GetModuleHandleA("engine.dll") + 0x00228908);
    *(uint32_t*)(0x4346D66C) = (uint32_t)((uint32_t)GetModuleHandleA("engine.dll") + 0x000F03FD);
    *(uint32_t*)(0x43468B9C) = (uint32_t)((uint32_t)GetModuleHandleA("client.dll") + 0x00712740);


    *(uint32_t*)(0x4346A31C) = (uint32_t)((uint32_t)GetModuleHandleA("localize.dll") + 0x00038A70);

    *(uint32_t*)(0x43468B2C) = (uint32_t)((uint32_t)GetModuleHandleA("engine.dll") + 0x52C810);
    *(uint32_t*)(0x43467288) = (uint32_t)((uint32_t)GetModuleHandleA("engine.dll") + 0x22F08F);
    *(uint32_t*)(0x4346728C) = (uint32_t)((uint32_t)GetModuleHandleA("client.dll") + 0x9A2CF0);
    *(uint32_t*)(0x4346728C) = (uint32_t)((uint32_t)GetModuleHandleA("client.dll") + 0xDF14C0);
    *(uint32_t*)(0x4346D540) = (uint32_t)((uint32_t)GetModuleHandleA("client.dll") + 0x3DC9B0);
    *(uint32_t*)(0x43476A40) = (uint32_t)((uint32_t)GetModuleHandleA("engine.dll") + 0x52B270);
    *(uint32_t*)(0x434671B4) = (uint32_t)((uint32_t)GetModuleHandleA("engine.dll") + 0x52B248);
    *(uint32_t*)(0x434678C4) = (uint32_t)((uint32_t)GetModuleHandleA("client.dll") + 0x2C8F50);
    *(uint32_t*)0x43468B9C = ((uint32_t)GetModuleHandleA("client.dll") + 0x712740);
    *(uint32_t*)0x43467C04 = ((uint32_t)GetModuleHandleA("client.dll") + 0x5344CE0);
    *(uint32_t*)0x43467040 = ((uint32_t)GetModuleHandleA("client.dll") + 0x70ADB0);
    *(uint32_t*)0x43468994 = ((uint32_t)GetModuleHandleA("client.dll") + 0x523BC98);

    *(uint32_t*)0x4346D67C = ((uint32_t)GetModuleHandleA("panorama.dll") + 0x9E220);
    *(uint32_t*)0x4346D680 = ((uint32_t)GetModuleHandleA("panorama.dll") + 0x9A320);
    *(uint32_t*)0x4346D684 = ((uint32_t)GetModuleHandleA("panorama.dll") + 0xA1B80);
    *(uint32_t*)0x4346D688 = ((uint32_t)GetModuleHandleA("panorama.dll") + 0xA3C80);
    *(uint32_t*)0x4346D68C = ((uint32_t)GetModuleHandleA("panorama.dll") + 0xAB6F0);

    *(uint32_t*)0x43468D0C = ((uint32_t)GetModuleHandleA("client.dll") + 0xDF14C0);
    *(uint32_t*)0x43467A70 = ((uint32_t)GetModuleHandleA("client.dll") + 0xDF14D4);
    *(uint32_t*)0x43468D50 = ((uint32_t)GetModuleHandleA("client.dll") + 0x5334E08);

    *(uint32_t*)0x4346D560 = ((uint32_t)GetModuleHandleA("engine.dll") + 0x8E55C);
    *(uint32_t*)0x4346D55C = ((uint32_t)GetModuleHandleA("engine.dll") + 0x22BE36);

    *(uint32_t*)0x4346A374 = ((uint32_t)GetModuleHandleA("client.dll") + 0x2D3A40);
    *(uint32_t*)0x4346D65C = ((uint32_t)GetModuleHandleA("client.dll") + 0x346AC0);
    *(uint32_t*)0x4346D660 = ((uint32_t)GetModuleHandleA("client.dll") + 0x344610);
    *(uint32_t*)0x4346A624 = ((uint32_t)GetModuleHandleA("client.dll") + 0x52632C8);
    *(uint32_t*)0x4346A364 = ((uint32_t)GetModuleHandleA("client.dll") + 0x1D94A0);
    *(uint32_t*)0x434679AC = ((uint32_t)GetModuleHandleA("client.dll") + 0x9A6740);
    *(uint32_t*)0x4346A38C = ((uint32_t)GetModuleHandleA("client.dll") + 0x1D2CA0);
    *(uint32_t*)0x434683D0 = ((uint32_t)GetModuleHandleA("client.dll") + 0xDACC0F);
    *(uint32_t*)0x43468F80 = ((uint32_t)GetModuleHandleA("client.dll") + 0x432E20);
    *(uint32_t*)0x434688C8 = ((uint32_t)GetModuleHandleA("client.dll") + 0x1EA950);
    *(uint32_t*)0x4346A6B0 = ((uint32_t)GetModuleHandleA("client.dll") + 0x1D1BA0);
    *(uint32_t*)0x43468C10 = ((uint32_t)GetModuleHandleA("client.dll") + 0x1AEC40);
    *(uint32_t*)0x43467C58 = ((uint32_t)GetModuleHandleA("client.dll") + 0x5334DA4);
    *(uint32_t*)0x43467E44 = ((uint32_t)GetModuleHandleA("client.dll") + 0x1EA790);
    *(uint32_t*)0x4346A598 = ((uint32_t)GetModuleHandleA("client.dll") + 0x442740);
    *(uint32_t*)0x43467C48 = ((uint32_t)GetModuleHandleA("client.dll") + 0x307260);
    *(uint32_t*)0x43468F30 = ((uint32_t)GetModuleHandleA("client.dll") + 0x320BD0);
    *(uint32_t*)0x434691DC = ((uint32_t)GetModuleHandleA("client.dll") + 0x9A2C40);
    *(uint32_t*)0x4346830C = ((uint32_t)GetModuleHandleA("client.dll") + 0xDF7FC0);
    *(uint32_t*)0x4346A3A4 = ((uint32_t)GetModuleHandleA("client.dll") + 0x5334764);
    *(uint32_t*)0x4346A4F8 = ((uint32_t)GetModuleHandleA("client.dll") + 0x19D140);
    *(uint32_t*)0x434678C8 = ((uint32_t)GetModuleHandleA("client.dll") + 0x1D9440);
    *(uint32_t*)0x4346832C = ((uint32_t)GetModuleHandleA("client.dll") + 0x1B1AC0);
    *(uint32_t*)0x43467CF0 = ((uint32_t)GetModuleHandleA("client.dll") + 0x7DE760);
    *(uint32_t*)0x43467B54 = ((uint32_t)GetModuleHandleA("client.dll") + 0x526388C);
    *(uint32_t*)0x43468C3C = ((uint32_t)GetModuleHandleA("client.dll") + 0x7DEB00);



    *(uint32_t*)0x43467000 = ((uint32_t)GetModuleHandleA("client.dll") + 0x32C0A59);
    *(uint32_t*)0x434693B0 = ((uint32_t)GetModuleHandleA("client.dll") + 0x292CA6C);
    *(uint32_t*)0x4346A770 = ((uint32_t)GetModuleHandleA("client.dll") + 0x4F741CB);
    *(uint32_t*)0x43469198 = ((uint32_t)GetModuleHandleA("client.dll") + 0x1F4E28D);
    *(uint32_t*)0x43469060 = ((uint32_t)GetModuleHandleA("client.dll") + 0x5332374);
    //*(uint32_t*)0x43468BE8 = ((uint32_t)GetModuleHandleA("client.dll") + 0x3374AD0);
    //*(uint32_t*)0x43468ED4 = ((uint32_t)GetModuleHandleA("client.dll") + 0x2491B42);
    *(uint32_t*)0x434688C8 = ((uint32_t)GetModuleHandleA("client.dll") + 0x1EA950);
    *(uint32_t*)0x43467CF0 = ((uint32_t)GetModuleHandleA("client.dll") + 0x7DE760);
    *(uint32_t*)0x4346830C = ((uint32_t)GetModuleHandleA("client.dll") + 0xDF7FC0);
    *(uint32_t*)0x4346728C = ((uint32_t)GetModuleHandleA("client.dll") + 0x9A2CF0);
    *(uint32_t*)0x43468C10 = ((uint32_t)GetModuleHandleA("client.dll") + 0x1AEC40);
    *(uint32_t*)0x4346A3A4 = ((uint32_t)GetModuleHandleA("client.dll") + 0x5334764);
    *(uint32_t*)0x4346D628 = ((uint32_t)GetModuleHandleA("panorama.dll") + 0x3DFE0);
    *(uint32_t*)0x4346D62C = ((uint32_t)GetModuleHandleA("panorama.dll") + 0x3E020);
    *(uint32_t*)0x43467C48 = ((uint32_t)GetModuleHandleA("client.dll") + 0x307260);
    *(uint32_t*)0x4346D658 = ((uint32_t)GetModuleHandleA("client.dll") + 0x52448F8);
    *(uint32_t*)0x4346D65C = ((uint32_t)GetModuleHandleA("client.dll") + 0x346AC0);
    *(uint32_t*)0x43467E44 = ((uint32_t)GetModuleHandleA("client.dll") + 0x1EA790);
    *(uint32_t*)0x4346A5DC = ((uint32_t)GetModuleHandleA("client.dll") + 0x3E2E673);
    *(uint32_t*)0x43468994 = ((uint32_t)GetModuleHandleA("client.dll") + 0x523BC98);
    //*(uint32_t*)0x43467A48 = ((uint32_t)GetModuleHandleA("client.dll") + 0x2FB9A4F);
    *(uint32_t*)0x4346A7CC = ((uint32_t)GetModuleHandleA("engine.dll") + 0x20CE50);
    *(uint32_t*)0x4346A374 = ((uint32_t)GetModuleHandleA("client.dll") + 0x2D3A40);
    *(uint32_t*)0x4346D67C = ((uint32_t)GetModuleHandleA("panorama.dll") + 0x9E220);
    *(uint32_t*)0x4346D680 = ((uint32_t)GetModuleHandleA("panorama.dll") + 0x9A320);
    *(uint32_t*)0x4346D688 = ((uint32_t)GetModuleHandleA("panorama.dll") + 0xA3C80);
    *(uint32_t*)0x43468FF0 = ((uint32_t)GetModuleHandleA("client.dll") + 0x3231380);
    *(uint32_t*)0x434679EC = ((uint32_t)GetModuleHandleA("client.dll") + 0xE10580);
    *(uint32_t*)0x43467F0C = ((uint32_t)GetModuleHandleA("client.dll") + 0x443481);
    *(uint32_t*)0x4346D664 = ((uint32_t)GetModuleHandleA("client.dll") + 0x525E110);
    *(uint32_t*)0x43468250 = ((uint32_t)GetModuleHandleA("client.dll") + 0x5334848);
    *(uint32_t*)0x4346A64C = ((uint32_t)GetModuleHandleA("engine.dll") + 0xD9A18);
    *(uint32_t*)0x43468390 = ((uint32_t)GetModuleHandleA("client.dll") + 0x12170B3);
    *(uint32_t*)0x43467550 = ((uint32_t)GetModuleHandleA("engine.dll") + 0x8DAB0);
    *(uint32_t*)0x43468F80 = ((uint32_t)GetModuleHandleA("client.dll") + 0x432E20);
    *(uint32_t*)0x434688C4 = ((uint32_t)GetModuleHandleA("client.dll") + 0x4413A0);
    *(uint32_t*)0x43468B9C = ((uint32_t)GetModuleHandleA("client.dll") + 0x712740);
    *(uint32_t*)0x43468C94 = ((uint32_t)GetModuleHandleA("client.dll") + 0x52BF6D8);
    *(uint32_t*)0x434688A4 = ((uint32_t)GetModuleHandleA("client.dll") + 0x215080);
    *(uint32_t*)0x4346A624 = ((uint32_t)GetModuleHandleA("client.dll") + 0x52632C8);
    *(uint32_t*)0x434689A0 = ((uint32_t)GetModuleHandleA("client.dll") + 0x2F10D3);
    *(uint32_t*)0x434692F4 = ((uint32_t)GetModuleHandleA("client.dll") + 0x430D0E);
    *(uint32_t*)0x4346D64C = ((uint32_t)GetModuleHandleA("client.dll") + 0x4364D0);
    *(uint32_t*)0x434691DC = ((uint32_t)GetModuleHandleA("client.dll") + 0x9A2C40);
    *(uint32_t*)0x4346D660 = ((uint32_t)GetModuleHandleA("client.dll") + 0x344610);
    *(uint32_t*)0x434688C0 = ((uint32_t)GetModuleHandleA("client.dll") + 0x525D440);
    *(uint32_t*)0x43467DDC = ((uint32_t)GetModuleHandleA("client.dll") + 0x43C750);
    *(uint32_t*)0x43468308 = ((uint32_t)GetModuleHandleA("engine.dll") + 0x2A5982);
    *(uint32_t*)0x434679AC = ((uint32_t)GetModuleHandleA("client.dll") + 0x9A6740);
    *(uint32_t*)0x4346D684 = ((uint32_t)GetModuleHandleA("panorama.dll") + 0xA1B80);
    *(uint32_t*)0x4346D68C = ((uint32_t)GetModuleHandleA("panorama.dll") + 0xAB6F0);




    *(uint32_t*)0x4346A884 = ((uint32_t)GetModuleHandleA("client.dll") + 0x2A78A0);
    *(uint32_t*)0x43468E88 = ((uint32_t)GetModuleHandleA("client.dll") + 0x2A77E0);
    *(uint32_t*)0x4346A7F0 = ((uint32_t)GetModuleHandleA("client.dll") + 0x28CC90);
    *(uint32_t*)0x43467040 = ((uint32_t)GetModuleHandleA("client.dll") + 0x70ADB0);
    //*(uint32_t*)0x4346A480 = ((uint32_t)GetModuleHandleA("client.dll") + 0x48E2CA4);
    *(uint32_t*)0x4346A724 = ((uint32_t)GetModuleHandleA("engine.dll") + 0x1791916);
    *(uint32_t*)0x43472B64 = ((uint32_t)GetModuleHandleA("engine.dll") + 0x2AC6E69);
    *(uint32_t*)0x4346EF18 = ((uint32_t)GetModuleHandleA("engine.dll") + 0x2AC6E69);
    *(uint32_t*)0x4346D558 = ((uint32_t)GetModuleHandleA("engine.dll") + 0x38FDCC8);
    *(uint32_t*)0x434674E0 = ((uint32_t)GetModuleHandleA("client.dll") + 0x4123AC);
    *(uint32_t*)0x4346756C = ((uint32_t)GetModuleHandleA("client.dll") + 0x5B1E2E);
    //*(uint32_t*)0x43467C58 = ((uint32_t)GetModuleHandleA("client.dll") + 0x5334DA4);
    *(uint32_t*)0x4346832C = ((uint32_t)GetModuleHandleA("client.dll") + 0x1B1AC0);
    *(uint32_t*)0x43468FA4 = ((uint32_t)GetModuleHandleA("client.dll") + 0x5334C84);
    *(uint32_t*)0x4346A364 = ((uint32_t)GetModuleHandleA("client.dll") + 0x1D94A0);
    //*(uint32_t*)0x4346FF78 = ((uint32_t)GetModuleHandleA("engine.dll") + 0x2AC6E69);
    *(uint32_t*)0x43468320 = ((uint32_t)GetModuleHandleA("client.dll") + 0x1E69D0);
    *(uint32_t*)0x4346A6B0 = ((uint32_t)GetModuleHandleA("client.dll") + 0x1D1BA0);
    //*(uint32_t*)0x43467A44 = ((uint32_t)GetModuleHandleA("client.dll") + 0x23B5D03);
    
    *(uint32_t*)0x43467EF4 = ((uint32_t)GetModuleHandleA("client.dll") + 0xE047DC);
    
    *(uint32_t*)0x434670C4 = ((uint32_t)GetModuleHandleA("client.dll") + 0x525EBC4);
    
    //*(uint32_t*)0x4346FE0C = ((uint32_t)GetModuleHandleA("engine.dll") + 0x2AC6E69);
    *(uint32_t*)0x43468C3C = ((uint32_t)GetModuleHandleA("client.dll") + 0x7DEB00);
    *(uint32_t*)0x43468CA4 = ((uint32_t)GetModuleHandleA("client.dll") + 0x7DE8A0);
    *(uint32_t*)0x434693F0 = ((uint32_t)GetModuleHandleA("client.dll") + 0x43E9E0);
    *(uint32_t*)0x434683D0 = ((uint32_t)GetModuleHandleA("client.dll") + 0xDACC0F);
    //*(uint32_t*)0x43468D50 = ((uint32_t)GetModuleHandleA("client.dll") + 0x5334E08);
    *(uint32_t*)0x43469348 = ((uint32_t)GetModuleHandleA("client.dll") + 0x36CAC0);
    *(uint32_t*)0x4346A4F8 = ((uint32_t)GetModuleHandleA("client.dll") + 0x19D140);
    *(uint32_t*)0x434679C0 = ((uint32_t)GetModuleHandleA("client.dll") + 0x525CAA0);
    //*(uint32_t*)0x43468418 = ((uint32_t)GetModuleHandleA("client.dll") + 0x2491B42);
    //*(uint32_t*)0x43467B54 = ((uint32_t)GetModuleHandleA("client.dll") + 0x526388C);
    *(uint32_t*)0x4346A454 = ((uint32_t)GetModuleHandleA("engine.dll") + 0xD99C0);
    *(uint32_t*)0x43468F30 = ((uint32_t)GetModuleHandleA("client.dll") + 0x320BD0);
    *(uint32_t*)0x43468DC0 = ((uint32_t)GetModuleHandleA("client.dll") + 0x19D600);
    *(uint32_t*)0x43469370 = ((uint32_t)GetModuleHandleA("engine.dll") + 0x159296);
    *(uint32_t*)0x4346A82C = ((uint32_t)GetModuleHandleA("client.dll") + 0x3D1820);
    *(uint32_t*)0x43468518 = ((uint32_t)GetModuleHandleA("client.dll") + 0x52EFAC);
    *(uint32_t*)0x43469304 = ((uint32_t)GetModuleHandleA("client.dll") + 0x535E4CC);
    *(uint32_t*)0x43467288 = ((uint32_t)GetModuleHandleA("engine.dll") + 0x22F08F);
    *(uint32_t*)0x4346746C = ((uint32_t)GetModuleHandleA("engine.dll") + 0x1C5D7D3);
    *(uint32_t*)0x4346A598 = ((uint32_t)GetModuleHandleA("client.dll") + 0x442740);
    *(uint32_t*)0x434678C8 = ((uint32_t)GetModuleHandleA("client.dll") + 0x1D9440);
    //*(uint32_t*)0x43468DFC = ((uint32_t)GetModuleHandleA("client.dll") + 0x5058269);
    *(uint32_t*)0x43468D6C = ((uint32_t)GetModuleHandleA("engine.dll") + 0x8CD5F0);
    //*(uint32_t*)0x434679A4 = ((uint32_t)GetModuleHandleA("engine.dll") + 0x1B76092);
    *(uint32_t*)0x4346A7E4 = ((uint32_t)GetModuleHandleA("client.dll") + 0x5344B38);
    *(uint32_t*)0x4346A38C = ((uint32_t)GetModuleHandleA("client.dll") + 0x1D2CA0);
    *(uint32_t*)0x4346D644 = ((uint32_t)GetModuleHandleA("client.dll") + 0x1E8100);

    *(uint32_t*)0x4346A898 = ((uint32_t)GetModuleHandleA("client.dll") + 0xDF98A0);
    HMODULE overlay = GetModuleHandleA("gameoverlayrenderer.dll");
    if (!overlay) {
        ReportCrashStage(skeet_crash::OverlayModuleMissing);
        return false;
    }
    const uintptr_t overlayBase = (uintptr_t)overlay;
    // Steam builds differ in both the compare register and surrounding stack
    // layout. Prefer the longer signature, then retain the original broad form.
    auto overlayReturn = PatternScan(overlay, "3D ? ? ? ? 73 1A 68 ? ? ? ? E8 ? ? ? ? 8B 0D ? ? ? ? 83 C4 04 8B 01 6A 00 FF 50 14 3B ?? 3C");
    if (!overlayReturn)
        overlayReturn = PatternScan(overlay, "3D ? ? ? ? 73 ? 68 ? ? ? ? E8 ? ? ? ? 8B 0D ? ? ? ? 83 C4 ? ? ? 6A ? FF 50 ? 3B 5F");
    if (!overlayReturn) {
        ReportCrashStageDetails(skeet_crash::OverlayReturnPatternFailed,
            (std::uint32_t)overlayBase, g_patternMatchCount,
            (std::uint32_t)g_patternFirstMatch, (std::uint32_t)g_patternSecondMatch);
        return false;
    }
    auto overlayReference = PatternScan(overlay, "89 3D ? ? ? ? F3 0F 10 87");
    if (!overlayReference)
        overlayReference = PatternScan(overlay, "89 1D ? ? ? ? F3 0F 10 83");
    if (!overlayReference) {
        ReportCrashStageDetails(skeet_crash::OverlayReferencePatternFailed,
            (std::uint32_t)overlayBase, g_patternMatchCount,
            (std::uint32_t)g_patternFirstMatch, (std::uint32_t)g_patternSecondMatch);
        return false;
    }
    uint32_t overlayGlobal = 0;
    memcpy(&overlayGlobal, overlayReference + 2, sizeof(overlayGlobal));
    auto overlayHeaders = (PIMAGE_NT_HEADERS)((uint8_t*)overlay + ((PIMAGE_DOS_HEADER)overlay)->e_lfanew);
    const size_t overlaySize = overlayHeaders->OptionalHeader.SizeOfImage;
    if (overlaySize < sizeof(overlayGlobal) || overlayGlobal < overlayBase
        || (uintptr_t)overlayGlobal - overlayBase > overlaySize - sizeof(overlayGlobal)) {
        ReportCrashStageDetails(skeet_crash::OverlayGlobalInvalid,
            (std::uint32_t)overlayBase, (std::uint32_t)overlaySize, overlayGlobal, 0);
        return false;
    }
    if (!PatchOverlayCallSlots((std::uint8_t*)(uintptr_t)_base, _size)) {
        ReportCrashStage(skeet_crash::OverlaySlotsFailed);
        return false;
    }
    if (!PatchFiringExtrapolation((std::uint8_t*)(uintptr_t)_base, _size)) {
        ReportCrashStage(skeet_crash::FiringPatchFailed);
        return false;
    }
    if (!PatchRagebotTargetPolicy((std::uint8_t*)(uintptr_t)_base, _size)) {
        ReportCrashStage(skeet_crash::TargetPolicyFailed);
        return false;
    }
    // Publish neither overlay address until all patches have passed validation.
    *(uint32_t*)0x43468D94 = (uint32_t)overlayReturn;
    *(uint32_t*)0x43468350 = overlayGlobal;


    recompile();
    return true;
}

bool skeet_t::entry()
{
    DWORD tid = 0;
    constexpr SIZE_T stackReserve = 0x100000;
    auto hThread = CreateThread(nullptr, stackReserve, entry_thread, nullptr,
        CREATE_SUSPENDED | STACK_SIZE_PARAM_IS_A_RESERVATION, &tid);

    if (!hThread) {
        return false;
    }

    CONTEXT ctx = {};
    ctx.ContextFlags = CONTEXT_FULL;
    if (!GetThreadContext(hThread, &ctx)) {
        ResumeThread(hThread);
        CloseHandle(hThread);
        return false;
    }
    // Preserve Windows' ESP, segments and exception-chain bookkeeping. A separate allocation
    // previously put ESP/SEH outside the TEB's registered stack (FAST_FAIL subcode 0x15).
    const CONTEXT originalContext = ctx;
    MEMORY_BASIC_INFORMATION stackRegion{};
    if (ctx.Esp > UINT32_MAX - 0x10
        || !VirtualQuery((void*)(uintptr_t)ctx.Esp, &stackRegion, sizeof(stackRegion))
        || stackRegion.State != MEM_COMMIT
        || !(stackRegion.Protect & (PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY))
        || (uintptr_t)ctx.Esp + 0x10 > (uintptr_t)stackRegion.BaseAddress + stackRegion.RegionSize
        || (stackRegion.Protect & (PAGE_GUARD | PAGE_NOACCESS))) {
        ResumeThread(hThread);
        CloseHandle(hThread);
        return false;
    }
    _stack = stackRegion.AllocationBase;
    ctx.Eip = 0x43481e2c;
    ctx.Esi = 0x43481e2c;
    ctx.Edi = 0x43481e2c;

    typedef struct _LSA_UNICODE_STRING { USHORT Length;	USHORT MaximumLength; PWSTR  Buffer; } UNICODE_STRING, * PUNICODE_STRING;
    typedef struct _OBJECT_ATTRIBUTES { ULONG Length; HANDLE RootDirectory; PUNICODE_STRING ObjectName; ULONG Attributes; PVOID SecurityDescriptor;	PVOID SecurityQualityOfService; } OBJECT_ATTRIBUTES, * POBJECT_ATTRIBUTES;
    using myNtCreateSection = NTSTATUS(NTAPI*)(OUT PHANDLE SectionHandle, IN ULONG DesiredAccess, IN POBJECT_ATTRIBUTES ObjectAttributes OPTIONAL, IN PLARGE_INTEGER MaximumSize OPTIONAL, IN ULONG PageAttributess, IN ULONG SectionAttributes, IN HANDLE FileHandle OPTIONAL);

    myNtCreateSection fNtCreateSection = (myNtCreateSection)(GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtCreateSection"));

    LARGE_INTEGER sectionSize{};
    sectionSize.QuadPart = 0x00100000;
    HANDLE sectionHandle = NULL;
    if (!fNtCreateSection || fNtCreateSection(&sectionHandle,
        SECTION_MAP_READ | SECTION_MAP_WRITE | SECTION_MAP_EXECUTE, nullptr,
        &sectionSize, PAGE_EXECUTE_READWRITE, SEC_COMMIT, nullptr) < 0 || !sectionHandle) {
        if (sectionHandle) CloseHandle(sectionHandle);
        ResumeThread(hThread);
        CloseHandle(hThread);
        _stack = nullptr;
        return false;
    }
    auto sectionArgument = (uint32_t*)(uintptr_t)(ctx.Esp + 0xC);
    const uint32_t originalArgument = *sectionArgument;
    *sectionArgument = (uint32_t)(uintptr_t)sectionHandle;
    if (!SetThreadContext(hThread, &ctx)) {
        *sectionArgument = originalArgument;
        CloseHandle(sectionHandle);
        if (SetThreadContext(hThread, &originalContext)) ResumeThread(hThread);
        CloseHandle(hThread);
        _stack = nullptr;
        return false;
    }

    if (ResumeThread(hThread) == (DWORD)-1) {
        *sectionArgument = originalArgument;
        CloseHandle(sectionHandle);
        if (SetThreadContext(hThread, &originalContext)) ResumeThread(hThread);
        CloseHandle(hThread);
        _stack = nullptr;
        return false;
    }

    CloseHandle(hThread);
    return true;
}

uint32_t reverse_emulate(eoperation action, int size, uint32_t op1, uint32_t op2) {
    switch (action) {
    case eoperation::ROL: {
        switch (size) {
        case 1:
            return std::rotr<uint8_t>(op1, op2);
        case 2:
            return std::rotr<uint16_t>(op1, op2);
        case 4:
            return std::rotr<uint32_t>(op1, op2);
        }
    }
    case eoperation::ROR: {

        switch (size) {
        case 1:
            return std::rotl<uint8_t>(op1, op2);
        case 2:
            return std::rotl<uint16_t>(op1, op2);
        case 4:
            return std::rotl<uint32_t>(op1, op2);
        }
    }
    case eoperation::BSWAP: {

        switch (size) {
        case 1:
            return std::byteswap<uint8_t>(op1);
        case 2:
            return std::byteswap<uint16_t>(op1);
        case 4:
            return std::byteswap<uint32_t>(op1);
        }
    }
    case eoperation::INC: {
        return op1 - 1;
    }
    case eoperation::DEC: {
        return op1 + 1;
    }
    case eoperation::SUB: {

        switch (size) {
        case 1:
            return static_cast<uint8_t>(op1 + op2);
        case 2:
            return static_cast<uint16_t>(op1 + op2);
        case 4:
            return static_cast<uint32_t>(op1 + op2);
        }
    }
    case eoperation::ADD: {

        switch (size) {
        case 1:
            return static_cast<uint8_t>(op1 - op2);
        case 2:
            return static_cast<uint16_t>(op1 - op2);
        case 4:
            return static_cast<uint32_t>(op1 - op2);
        }
    }
    case eoperation::NOT: {
        switch (size) {
        case 1:
            return static_cast<uint8_t>(~op1);
        case 2:
            return static_cast<uint16_t>(~op1);
        case 4:
            return static_cast<uint32_t>(~op1);
        }
    }
    case eoperation::XOR: {

        switch (size) {
        case 1:
            return static_cast<uint8_t>(op1 ^ op2);
        case 2:
            return static_cast<uint16_t>(op1 ^ op2);
        case 4:
            return static_cast<uint32_t>(op1 ^ op2);
        }
    }
    case eoperation::NEG: {

        switch (size) {
        case 1:
            return static_cast<uint8_t>(0 - op1);
        case 2:
            return static_cast<uint16_t>(0 - op1);
        case 4:
            return static_cast<uint32_t>(0 - op1);
        }
    }
    }
}

uint32_t fnv1a(const std::wstring& data) {
    const uint32_t FNV_prime = 0x1000193;
    const uint32_t offset_basis = 0x811C9DC5;
    uint32_t hash = offset_basis;

    for (wchar_t c : data) {
        hash ^= c;             // XOR ñ òåêóùèì áàéòîì äàííûõ
        hash *= FNV_prime;     // Óìíîæàåì íà êîíñòàíòó FNV_prime
    }

    return hash;
}

void SubtractMinutesFromSystemTime(int minutes, FILETIME& ft) {
    ULARGE_INTEGER li;
    li.LowPart = ft.dwLowDateTime;
    li.HighPart = ft.dwHighDateTime;

    const ULONGLONG intervalsPerMinute = 60 * 10000000ULL;

    li.QuadPart += (minutes * intervalsPerMinute);

    ft.dwLowDateTime = li.LowPart;
    ft.dwHighDateTime = li.HighPart;
}

void skeet_t::recompile()
{
    // VIP -> PATCH MAP
    std::map<uint32_t, uint32_t> patches;

    patches[0x000000004349D3E2] = (u32)GetModuleHandleA("ntdll.dll");

    GetSystemTimeAsFileTime(&ftime);
    SubtractMinutesFromSystemTime(10, ftime);
    auto arg1 = ftime.dwLowDateTime - 0x0D53E8000;
    auto arg2 = ftime.dwHighDateTime - 0x19DB1DE;
    auto time_check = ((int(__stdcall*)(int arg1, int arg2, int arg3, int arg4))0x433AE31E)(arg1, arg2, 0x989680, 0);
    patches[0x000000004353C3D1] = time_check;

    auto crc_hash = ((int(__fastcall*)(int base, int size))0x4333D6A2)(0x4331E000, 0x105B12);
    patches[0x00000000434C6B29] = crc_hash ^ 0x92CA5DF1;

    patches[0x00000000434EB360] = GetCurrentProcessId();

    patches[0x00000000435507ED] = time_check;

    PPEB peb = (PPEB)(__readfsdword(0x30));

    auto ldr_entry = peb->Ldr;

    PLDR_DATA_TABLE_ENTRY entry = (PLDR_DATA_TABLE_ENTRY)ldr_entry->InLoadOrderModuleList->Flink;

    auto val = *(uint32_t*)((uint32_t)entry + 0x88) - 0xD53E8000;
    auto val2 = *(uint32_t*)((uint32_t)entry + 0x8c) - 0x19DB1DE;
    time_check = ((int(__stdcall*)(int arg1, int arg2, int arg3, int arg4))0x433AE31E)(val, val2, 0x989680, 0);

    patches[0x000000004352A5D6] = time_check;

    SetEnvironmentVariable(L"STEAMID", L"7612345678901");

    patches[0x000000004356C0A1] = fnv1a(L"7612345678901");

    patches[0x000000004352EE8F] = *(uint16_t*)0x7FFE0260;


    int cpu_info[4] = {};
    __cpuidex(cpu_info, 1, 0);

    patches[0x434bc39e] = cpu_info[2];
    patches[0x4356afc4] = cpu_info[2];
    patches[0x000000004352C91C] = cpu_info[2];

    patches[0x000000004351B2B5] = (u32)GetModuleHandleA("ntdll.dll");

    patches[0x434d4faa] = (u32)GetCurrentProcessId();

    uint32_t hash = 0;
    for (uint32_t i = 0x80000002; i <= 0x80000004; i++) {
        __cpuidex(cpu_info, i, 0);

        hash += cpu_info[0] + cpu_info[1] + cpu_info[2] + cpu_info[3];
    }

    auto it = std::find_if(removedHandlers.begin(), removedHandlers.end(), [](const removed_handler_t& other) {
        return other.vip == 0x000000004359257D;
        });

    it->value = hash;
    patches[0x43507C7F] = hash;

    patches[0x435194de] = (u32)GetModuleHandleA("kernel32.dll");

    uint32_t hashKUserSharedData = 0x811C9DC5;

    for (int i = 0; i < 0x40; i++) {
        unsigned char byte = *(unsigned char*)(0x7FFE0274 + i);

        uint32_t tmpValue = byte + i;
        tmpValue = tmpValue ^ hashKUserSharedData;
        hashKUserSharedData = tmpValue * 0x1000193;
    }

    patches[0x434eb732] = hashKUserSharedData;

    patches[0x435469B5] = (uint32_t)GetModuleHandleA("kernelbase.dll");

    for (int i = 0; i < handlers.size(); i++) {
        auto& handlers_info = handlers[i];

        if (patches.find(handlers_info[0].vip) != patches.end()) {
            handlers_info[0].value = patches[handlers_info[0].vip];
        }
        else {
            auto import_dest = handlers_info[0].value;

            if (exports.find(import_dest) != exports.end()) {
                auto& export_ = exports[import_dest];

                handlers_info[0].value = (u32)GetProcAddress(LoadLibraryA(export_.lib.c_str()), export_.name.c_str());
            }
        }

        uint32_t encryption_key = handlers_info[0].encryption_key;

        for (auto& handler_info : handlers_info) {

            if (!skeet_t::is_image_range(handler_info.vip))
                continue;

            uint32_t value = handler_info.value;
            uint8_t size = handler_info.size;

            for (auto it = handler_info.operations.rbegin(); it != handler_info.operations.rend(); it++) {
                value = reverse_emulate(it->operation, size, value, it->value);
            }
            switch (size) {
            case 4:
                value ^= encryption_key;
                *(uint32_t*)handler_info.vip = value;
                break;
            case 2:
                value ^= static_cast<uint16_t>(encryption_key);
                *(uint16_t*)handler_info.vip = value;
                break;
            case 1:
                value ^= static_cast<uint8_t>(encryption_key);
                *(uint8_t*)handler_info.vip = value;
                break;
            }
            encryption_key ^= static_cast<uint32_t>(handler_info.value);
        }
    }
}

skeet_t* skeet_t::getInstance(HMODULE base)
{
    if (!singleton)
        singleton = new skeet_t(base);
        
    return singleton;
}

bool skeet_t::is_stack_range(u32 addr)
{
    auto skeet = getInstance();

    return addr >= 0x10D0000 && addr < 0x10D0000 + 0x100000;
}

bool skeet_t::is_image_range(u32 addr)
{
    auto skeet = getInstance();

    return addr >= skeet->base() && addr < skeet->base() + skeet->size();
}

bool skeet_t::is_exception(u32 addr)
{
    if(addr >= 0x434F8532 && addr <= 0x434F853A)
        return true;
    return false;
}
