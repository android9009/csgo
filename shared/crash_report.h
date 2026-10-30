#pragma once

#include <Windows.h>
#include <cstddef>
#include <cstdint>

// Fixed-width, x86-compatible records shared by skeet.dll and the external
// mapper. The mapper owns the mapping and event, so the last record remains
// readable even after the game process has terminated.
namespace skeet_crash {

inline constexpr std::uint32_t kMagic = 0x534B4352; // "S K C R"
inline constexpr std::uint32_t kVersion = 1;
inline constexpr std::uint32_t kRecordStatus = 1;
inline constexpr std::uint32_t kRecordException = 2;
inline constexpr std::uint32_t kImageBase = 0x43310000;
inline constexpr std::uint32_t kImageSize = 0x2fc000;

enum Stage : std::uint32_t {
    ThreadStarted = 1,
    ImageConstructed = 2,
    MapSucceeded = 3,
    ImportsSucceeded = 4,
    PatchesSucceeded = 5,
    Ready = 6,

    MapFailed = 0x1001,
    ImportsFailed = 0x1002,
    PatchesFailed = 0x1003,
    EntryFailed = 0x1004,
    OverlayModuleMissing = 0x1005,
    OverlayReturnPatternFailed = 0x1006,
    OverlayReferencePatternFailed = 0x1007,
    OverlayGlobalInvalid = 0x1008,
    OverlaySlotsFailed = 0x1009,
    FiringPatchFailed = 0x100A,
    TargetPolicyFailed = 0x100B
};

struct Record {
    std::uint32_t magic;
    std::uint32_t version;
    std::uint32_t size;
    std::uint32_t kind;
    std::uint32_t processId;
    std::uint32_t threadId;
    std::uint32_t stage;
    std::uint32_t tickCount;

    std::uint32_t exceptionCode;
    std::uint32_t exceptionFlags;
    std::uint32_t exceptionAddress;
    std::uint32_t instructionPointer;
    std::uint32_t stackPointer;
    std::uint32_t framePointer;

    std::uint32_t eax;
    std::uint32_t ebx;
    std::uint32_t ecx;
    std::uint32_t edx;
    std::uint32_t esi;
    std::uint32_t edi;
    std::uint32_t eflags;

    std::uint32_t numberParameters;
    std::uint32_t exceptionInformation[2];
    std::uint32_t reporterBase;
    std::uint32_t reporterSize;
    std::uint32_t imageBase;
    std::uint32_t imageSize;
    std::uint32_t reserved[4];
};

struct SharedBlock {
    volatile LONG writeLock;
    volatile LONG sequence;
    std::uint32_t lastStage;
    std::uint32_t reserved;
    Record record;
};

static_assert(sizeof(Record) == 128, "Crash record layout must remain stable");
static_assert(offsetof(SharedBlock, record) == 16, "Crash header layout must remain stable");
static_assert(sizeof(SharedBlock) == 144, "Crash shared block layout must remain stable");

} // namespace skeet_crash
