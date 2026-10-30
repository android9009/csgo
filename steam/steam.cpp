#include <Windows.h>
#include <TlHelp32.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "skCrypter.h"
#include "../shared/crash_report.h"

namespace {

struct Monitor {
    HANDLE mapping = nullptr;
    HANDLE event = nullptr;
    skeet_crash::SharedBlock* block = nullptr;
};

struct ModuleInfo {
    std::uint32_t base;
    std::uint32_t size;
    std::string name;
};

std::ofstream g_log;
std::vector<ModuleInfo> g_modules;

std::string Hex(std::uint32_t value) {
    std::ostringstream text;
    text << "0x" << std::uppercase << std::hex << std::setfill('0')
         << std::setw(8) << value;
    return text.str();
}

std::string Timestamp() {
    SYSTEMTIME time = {};
    GetLocalTime(&time);
    std::ostringstream text;
    text << std::setfill('0') << std::setw(4) << time.wYear << '-'
         << std::setw(2) << time.wMonth << '-' << std::setw(2) << time.wDay
         << ' ' << std::setw(2) << time.wHour << ':' << std::setw(2) << time.wMinute
         << ':' << std::setw(2) << time.wSecond << '.' << std::setw(3) << time.wMilliseconds;
    return text.str();
}

void Log(const std::string& text) {
    std::cout << text;
    std::cout.flush();
    if (g_log.is_open()) {
        g_log << text;
        g_log.flush();
    }
}

void LogLine(const std::string& text) {
    Log('[' + Timestamp() + "] " + text + '\n');
}

std::string WideToUtf8(const wchar_t* value) {
    if (!value || !*value) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, value, -1,
        nullptr, 0, nullptr, nullptr);
    if (size <= 1) return {};
    std::string result(static_cast<std::size_t>(size), '\0');
    if (!WideCharToMultiByte(CP_UTF8, 0, value, -1,
            result.data(), size, nullptr, nullptr)) {
        return {};
    }
    result.pop_back();
    return result;
}

HANDLE GetProcessByName(const wchar_t* name, DWORD* processId) {
    *processId = 0;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return nullptr;

    PROCESSENTRY32W process = {};
    process.dwSize = sizeof(process);
    if (Process32FirstW(snapshot, &process)) {
        do {
            if (_wcsicmp(process.szExeFile, name) == 0) {
                *processId = process.th32ProcessID;
                break;
            }
        } while (Process32NextW(snapshot, &process));
    }
    CloseHandle(snapshot);
    if (!*processId) return nullptr;
    return OpenProcess(PROCESS_ALL_ACCESS, FALSE, *processId);
}

void DestroyMonitor(Monitor& monitor) {
    if (monitor.block) UnmapViewOfFile(monitor.block);
    if (monitor.event) CloseHandle(monitor.event);
    if (monitor.mapping) CloseHandle(monitor.mapping);
    monitor = {};
}

bool CreateMonitor(DWORD processId, Monitor& monitor) {
    wchar_t mappingName[64] = {};
    wchar_t eventName[64] = {};
    if (swprintf_s(mappingName, L"Local\\SkeetCrashMemory_%lu", processId) < 0
        || swprintf_s(eventName, L"Local\\SkeetCrashEvent_%lu", processId) < 0) {
        return false;
    }

    monitor.mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr,
        PAGE_READWRITE, 0, sizeof(skeet_crash::SharedBlock), mappingName);
    if (!monitor.mapping) return false;
    monitor.block = static_cast<skeet_crash::SharedBlock*>(MapViewOfFile(
        monitor.mapping, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0,
        sizeof(skeet_crash::SharedBlock)));
    if (!monitor.block) {
        DestroyMonitor(monitor);
        return false;
    }
    ZeroMemory(monitor.block, sizeof(*monitor.block));

    monitor.event = CreateEventW(nullptr, FALSE, FALSE, eventName);
    if (!monitor.event) {
        DestroyMonitor(monitor);
        return false;
    }
    ResetEvent(monitor.event);
    return true;
}

void RefreshModules(DWORD processId) {
    HANDLE snapshot = CreateToolhelp32Snapshot(
        TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, processId);
    if (snapshot == INVALID_HANDLE_VALUE) return;

    std::vector<ModuleInfo> modules;
    MODULEENTRY32W entry = {};
    entry.dwSize = sizeof(entry);
    if (Module32FirstW(snapshot, &entry)) {
        do {
            const auto base = reinterpret_cast<std::uintptr_t>(entry.modBaseAddr);
            if (base <= UINT32_MAX) {
                modules.push_back({
                    static_cast<std::uint32_t>(base),
                    static_cast<std::uint32_t>(entry.modBaseSize),
                    WideToUtf8(entry.szModule)
                });
            }
        } while (Module32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    if (!modules.empty()) g_modules = std::move(modules);
}

std::string DescribeAddress(std::uint32_t address,
    const skeet_crash::Record& record) {
    std::ostringstream text;
    text << Hex(address);
    if (record.reporterSize && address >= record.reporterBase
        && address - record.reporterBase < record.reporterSize) {
        text << " (skeet.dll+" << Hex(address - record.reporterBase) << ')';
        return text.str();
    }
    if (record.imageSize && address >= record.imageBase
        && address - record.imageBase < record.imageSize) {
        text << " (skeet.bin+" << Hex(address - record.imageBase) << ')';
        return text.str();
    }
    for (const auto& module : g_modules) {
        if (address >= module.base && address - module.base < module.size) {
            text << " (" << module.name << '+' << Hex(address - module.base) << ')';
            break;
        }
    }
    return text.str();
}

const char* StageName(std::uint32_t stage) {
    switch (stage) {
    case skeet_crash::ThreadStarted: return "reporter armed";
    case skeet_crash::ImageConstructed: return "image constructed";
    case skeet_crash::MapSucceeded: return "image mapped";
    case skeet_crash::ImportsSucceeded: return "imports fixed";
    case skeet_crash::PatchesSucceeded: return "patches applied";
    case skeet_crash::Ready: return "ready";
    case skeet_crash::MapFailed: return "IMAGE MAP FAILED";
    case skeet_crash::ImportsFailed: return "IMPORT FIX FAILED";
    case skeet_crash::PatchesFailed: return "PATCH PREFLIGHT FAILED";
    case skeet_crash::EntryFailed: return "ENTRY START FAILED";
    default: return "unknown stage";
    }
}

const char* ExceptionName(std::uint32_t code) {
    switch (code) {
    case EXCEPTION_ACCESS_VIOLATION: return "ACCESS_VIOLATION";
    case EXCEPTION_ARRAY_BOUNDS_EXCEEDED: return "ARRAY_BOUNDS_EXCEEDED";
    case EXCEPTION_BREAKPOINT: return "BREAKPOINT";
    case EXCEPTION_DATATYPE_MISALIGNMENT: return "DATATYPE_MISALIGNMENT";
    case EXCEPTION_FLT_DENORMAL_OPERAND: return "FLT_DENORMAL_OPERAND";
    case EXCEPTION_FLT_DIVIDE_BY_ZERO: return "FLT_DIVIDE_BY_ZERO";
    case EXCEPTION_FLT_INEXACT_RESULT: return "FLT_INEXACT_RESULT";
    case EXCEPTION_FLT_INVALID_OPERATION: return "FLT_INVALID_OPERATION";
    case EXCEPTION_FLT_OVERFLOW: return "FLT_OVERFLOW";
    case EXCEPTION_FLT_STACK_CHECK: return "FLT_STACK_CHECK";
    case EXCEPTION_FLT_UNDERFLOW: return "FLT_UNDERFLOW";
    case EXCEPTION_ILLEGAL_INSTRUCTION: return "ILLEGAL_INSTRUCTION";
    case EXCEPTION_IN_PAGE_ERROR: return "IN_PAGE_ERROR";
    case EXCEPTION_INT_DIVIDE_BY_ZERO: return "INT_DIVIDE_BY_ZERO";
    case EXCEPTION_INT_OVERFLOW: return "INT_OVERFLOW";
    case EXCEPTION_INVALID_DISPOSITION: return "INVALID_DISPOSITION";
    case EXCEPTION_NONCONTINUABLE_EXCEPTION: return "NONCONTINUABLE_EXCEPTION";
    case EXCEPTION_PRIV_INSTRUCTION: return "PRIV_INSTRUCTION";
    case EXCEPTION_SINGLE_STEP: return "SINGLE_STEP";
    case EXCEPTION_STACK_OVERFLOW: return "STACK_OVERFLOW";
    default: return "UNKNOWN_EXCEPTION";
    }
}

void PrintRecord(const skeet_crash::Record& record, DWORD processId) {
    if (record.magic != skeet_crash::kMagic
        || record.version != skeet_crash::kVersion
        || record.size != sizeof(skeet_crash::Record)) {
        LogLine("received an incompatible crash record; raw logging stopped for this record");
        return;
    }

    if (record.kind == skeet_crash::kRecordStatus) {
        std::ostringstream text;
        text << StageName(record.stage) << " (stage " << Hex(record.stage)
             << ", tid " << record.threadId << ')';
        LogLine(text.str());
        return;
    }
    if (record.kind != skeet_crash::kRecordException) {
        LogLine("unknown record kind " + Hex(record.kind));
        return;
    }

    RefreshModules(processId);
    std::ostringstream text;
    text << '[' << Timestamp() << "] UNHANDLED " << ExceptionName(record.exceptionCode)
         << ' ' << Hex(record.exceptionCode) << " | pid " << record.processId
         << " tid " << record.threadId << " | last stage: " << StageName(record.stage)
         << " | tick " << record.tickCount << "\n";
    text << "  Exception address: "
         << DescribeAddress(record.exceptionAddress, record)
         << " | EIP: " << DescribeAddress(record.instructionPointer, record)
         << " | flags: " << Hex(record.exceptionFlags) << "\n";
    if (record.exceptionCode == EXCEPTION_ACCESS_VIOLATION
        && record.numberParameters >= 2) {
        const char* operation = "access";
        if (record.exceptionInformation[0] == 0) operation = "read";
        if (record.exceptionInformation[0] == 1) operation = "write";
        if (record.exceptionInformation[0] == 8) operation = "execute";
        text << "  Fault: " << operation << ' '
             << DescribeAddress(record.exceptionInformation[1], record) << "\n";
    }
    text << "  EAX=" << Hex(record.eax) << " EBX=" << Hex(record.ebx)
         << " ECX=" << Hex(record.ecx) << " EDX=" << Hex(record.edx) << "\n";
    text << "  ESI=" << Hex(record.esi) << " EDI=" << Hex(record.edi)
         << " ESP=" << Hex(record.stackPointer) << " EBP=" << Hex(record.framePointer)
         << " EFLAGS=" << Hex(record.eflags) << "\n";
    Log(text.str());
}

bool ConsumeRecord(Monitor& monitor, DWORD processId, LONG& lastSequence) {
    for (int attempt = 0; attempt < 8; ++attempt) {
        if (monitor.block->writeLock) {
            Sleep(0);
            continue;
        }
        MemoryBarrier();
        const LONG sequence = InterlockedCompareExchange(&monitor.block->sequence, 0, 0);
        if (sequence == lastSequence) return false;
        const auto record = monitor.block->record;
        MemoryBarrier();
        if (sequence != InterlockedCompareExchange(&monitor.block->sequence, 0, 0)) {
            continue;
        }
        lastSequence = sequence;
        PrintRecord(record, processId);
        return true;
    }
    return false;
}

void MonitorLoop(Monitor& monitor, HANDLE process, DWORD processId) {
    LONG lastSequence = 0;
    HANDLE waitHandles[] = { monitor.event, process };
    for (;;) {
        const DWORD wait = WaitForMultipleObjects(2, waitHandles, FALSE, INFINITE);
        if (wait == WAIT_OBJECT_0) {
            ConsumeRecord(monitor, processId, lastSequence);
            continue;
        }
        if (wait == WAIT_OBJECT_0 + 1) {
            // A final exception can be published immediately before process death.
            ConsumeRecord(monitor, processId, lastSequence);
            DWORD exitCode = 0;
            if (GetExitCodeProcess(process, &exitCode)) {
                LogLine("csgo.exe terminated, exit code " + Hex(exitCode));
            } else {
                LogLine("csgo.exe terminated; failed to query its exit code");
            }
            LogLine("crash details above were preserved; this console will stay open");
            return;
        }
        LogLine("monitor wait failed, Win32 error " + std::to_string(GetLastError()));
        return;
    }
}

[[noreturn]] void PauseForever() {
    std::cout << "\nPress Enter to close the mapper/monitor...";
    std::cout.flush();
    std::cin.get();
    std::exit(0);
}

} // namespace

int main() {
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleTitleA(skCrypt("DONT RENAME skeet.dll! - crash monitor"));
    std::cout << skCrypt("DONT RENAME skeet.dll!\nEnter -insecure in csgo params\nwaiting for csgo...\n");

    const auto path = std::filesystem::current_path();
    const auto pathStr = path.wstring() + L"\\skeet.dll";
    if (!std::filesystem::exists(pathStr)) {
        std::cout << "skeet.dll was not found beside the mapper.\n";
        PauseForever();
    }

    HANDLE process = nullptr;
    DWORD processId = 0;
    while (!(process = GetProcessByName(L"csgo.exe", &processId))) {
        Sleep(500);
    }
    SetConsoleTitleA(skCrypt("DONT RENAME skeet.dll! - crash monitor active"));

    g_log.open("skeet_crash_" + std::to_string(processId) + ".log", std::ios::app);
    LogLine("found csgo.exe, pid " + std::to_string(processId));
    LogLine("logging to skeet_crash_" + std::to_string(processId) + ".log");
    RefreshModules(processId);

    Monitor monitor;
    if (!CreateMonitor(processId, monitor)) {
        LogLine("failed to create crash shared memory, Win32 error " + std::to_string(GetLastError()));
        CloseHandle(process);
        PauseForever();
    }
    LogLine("crash monitor armed before injection");

    void* cheat = VirtualAllocEx(process,
        reinterpret_cast<void*>(static_cast<std::uintptr_t>(skeet_crash::kImageBase)),
        skeet_crash::kImageSize, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!cheat) {
        LogLine("failed to allocate the Skeet image, Win32 error " + std::to_string(GetLastError()));
        DestroyMonitor(monitor);
        CloseHandle(process);
        PauseForever();
    }

    void* argument = VirtualAllocEx(process, nullptr, 4096,
        MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!argument) {
        LogLine("failed to allocate the LoadLibrary argument");
        VirtualFreeEx(process, cheat, 0, MEM_RELEASE);
        DestroyMonitor(monitor);
        CloseHandle(process);
        PauseForever();
    }

    SIZE_T bytesWritten = 0;
    const SIZE_T pathBytes = (pathStr.size() + 1) * sizeof(wchar_t);
    if (!WriteProcessMemory(process, argument, pathStr.c_str(), pathBytes, &bytesWritten)
        || bytesWritten != pathBytes) {
        LogLine("failed to write the skeet.dll path, Win32 error " + std::to_string(GetLastError()));
        VirtualFreeEx(process, argument, 0, MEM_RELEASE);
        VirtualFreeEx(process, cheat, 0, MEM_RELEASE);
        DestroyMonitor(monitor);
        CloseHandle(process);
        PauseForever();
    }

    const auto loadLibrary = GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW");
    HANDLE thread = loadLibrary ? CreateRemoteThread(process, nullptr, 0,
        reinterpret_cast<LPTHREAD_START_ROUTINE>(loadLibrary), argument, 0, nullptr) : nullptr;
    if (!thread) {
        LogLine("CreateRemoteThread failed, Win32 error " + std::to_string(GetLastError()));
        VirtualFreeEx(process, argument, 0, MEM_RELEASE);
        VirtualFreeEx(process, cheat, 0, MEM_RELEASE);
        DestroyMonitor(monitor);
        CloseHandle(process);
        PauseForever();
    }

    LogLine("injection thread started");
    WaitForSingleObject(thread, INFINITE);
    DWORD loadResult = 0;
    if (!GetExitCodeThread(thread, &loadResult) || !loadResult) {
        LogLine("LoadLibraryW failed inside csgo.exe; no Skeet crash reporter was connected");
    } else {
        LogLine("skeet.dll loaded; waiting for initialization and crash records");
    }
    CloseHandle(thread);
    VirtualFreeEx(process, argument, 0, MEM_RELEASE);
    RefreshModules(processId);

    MonitorLoop(monitor, process, processId);
    DestroyMonitor(monitor);
    CloseHandle(process);
    if (g_log.is_open()) g_log.close();
    PauseForever();
}
