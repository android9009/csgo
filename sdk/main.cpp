#define _SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING

#include <windows.h>

#include "core/cheat/cheat.h"
#include "engine/debug/debug_overlay.h"
#include "engine/hash/hash.h"
#include "engine/input/input.h"
#include "engine/logging/logging.h"
#include "engine/pe/pe.h"
#include "engine/render/render.h"
#include "engine/security/security.h"
#include "engine/security/xorstr.h"

#include <DbgHelp.h>
#include <codecvt>
#include <format>
#include <locale>
#include <winternl.h>

// fuck you duxe u pasting fucking retard i want to fucking slap your fucking mother so god damn hard for not drowning you at birth you
// stupid dumb fucking rat bastard im going to literally shut your fucking power off and then make ur fridge run down the street

// https://i.imgur.com/xD24aJu.jpg

// this cheats going to have my unironic suicide note in it one day

// if this source gets leaked, this is all of our real notes, this is from no one else this is purely our thoughts and nothing else why
// would u even remotely suspect that we would be lying about such things you are a fake and a lame and a snake and a opp u gonna get smoked
// on ur own set if you keep talking shit im going to send duxe after you with his pink egirl gaming chair and he'll run u over in his bmw
// that he somehow bought from pasting a dogshit rust hack that fucking bluescreened my pc 9 times in 1 sitting using that shitty remote
// desktop bypass as well fucking aids as fuck shoutout daum 4 the cerb bypass tho was fun raging w/o any fkn bans for like a week str8 miss
// those days ngl nigga

// i should be writing this hack but i can't think of anything to do rn i cbf to finish inventory changer man
// gonna be on a fbi watchlist if this gets out ngl

/* credits: duxe/laine/aiden */

// saves us fps
// doesnt matter theyre fucked anyways

// laines diary don't open.
// day 1, czapek has drove me insane - day 1 pt2, czapek is fail pasting view_matrix
// day 2, naz has been driving me insane that nigga makes me wanna throw a toaster into his mothers bathtub
// day 3, eternity still hasnt made any progress on the loader and is sending me random webm memes instead of answering stuff
// day 9, can't figure out why the loader is crashing, last ditch effort will be asking duxe
// day 10, loader has been fixed. millionware is a go.
// day 32, cheats kinda coming together, still don't wanna release it or do the skin changer
// day 39, eternity is back.
// day 61, gui coming along, loader coming along. coming along.
// day 369, going to kill myself lmfao.

//⠀⠀⠀⠀⠀⠀⠀⣠⣤⣤⣤⣤⣤⣄⡀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀
//⠀⠀⠀⠀⠀⢰⡿⠋⠁⠀⠀⠈⠉⠙⠻⣷⣄⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀
//⠀⠀⠀⠀⢀⣿⠇⠀⢀⣴⣶⡾⠿⠿⠿⢿⣿⣦⡀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀
//⠀⠀⣀⣀⣸⡿⠀⠀⢸⣿⣇⠀⠀⠀⠀⠀⠀⠙⣷⡀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀
//⠀⣾⡟⠛⣿⡇⠀⠀⢸⣿⣿⣷⣤⣤⣤⣤⣶⣶⣿⠇⠀⠀⠀⠀⠀⠀⠀⣀⠀⠀
//⢀⣿⠀⢀⣿⡇⠀⠀⠀⠻⢿⣿⣿⣿⣿⣿⠿⣿⡏⠀⠀⠀⠀⢴⣶⣶⣿⣿⣿⣆
//⢸⣿⠀⢸⣿⡇⠀⠀⠀⠀⠀⠈⠉⠁⠀⠀⠀⣿⡇⣀⣠⣴⣾⣮⣝⠿⠿⠿⣻⡟
//⢸⣿⠀⠘⣿⡇⠀⠀⠀⠀⠀⠀⠀⣠⣶⣾⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⡿⠁⠉⠀
//⠸⣿⠀⠀⣿⡇⠀⠀⠀⠀⠀⣠⣾⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⡿⠟⠉⠀⠀⠀⠀
//⠀⠻⣷⣶⣿⣇⠀⠀⠀⢠⣼⣿⣿⣿⣿⣿⣿⣿⣛⣛⣻⠉⠁⠀⠀⠀⠀⠀⠀⠀
//⠀⠀⠀⠀⢸⣿⠀⠀⠀⢸⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⡇⠀⠀⠀⠀⠀
//⠀⠀⠀⠀⢸⣿⣀⣀⣀⣼⡿⢿⣿⣿⣿⣿⣿⡿⣿⣿⣿

//     when million, doesnt million.
// ⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⢀⣀⣀⣀⣀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀
// ⠀⠀⠀⠀⠀⠀⠀⠀⡠⠔⠉⠀⠀⠀⠀⠀⠉⠒⢄⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀
// ⠀⠀⠀⠀⠀⠀⢀⣞⣀⣀⡀⠀⢀⣀⣀⣀⡀⠀⠀⠱⡀⠀⠀⠀⠀⠀⠀⠀⠀⠀
// ⠀⠀⠀⠀⠀⠀⡌⢾⣿⡇⠀⠀⠰⣿⣷⠀⠀⠀⠀⢀⢱⠀⠀⠀⠀⠀⠀⠀⠀⠀
// ⠀⠀⠀⠀⠀⠀⡇⠀⠉⠀⠀⠀⣠⣬⣵⣤⣄⠀⠀⠀⠙⡄⠀⠀⠀⠀⠀⠀⠀⠀
// ⠀⠀⠀⠀⠀⠀⢣⠀⠀⠀⠴⢿⣿⣿⣿⣿⠟⡣⡀⠀⣠⠃⠀⠀⠀⠀⠀⠀⠀⠀
// ⠀⠀⠀⠀⠀⠀⠀⠳⡀⠀⠀⠀⠈⠉⠉⠉⠁⠂⢁⠞⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀
// ⠀⠀⠠⢦⣤⣀⡀⠀⠈⠒⠤⣀⣀⡀⣀⣀⠤⠐⠳⣦⣤⣤⣤⠶⠶⠶⢦⣤⡀⠀
// ⠀⠀⠀⠀⠈⠉⠙⠛⠻⠶⠶⠶⠶⠶⠶⠶⠶⠾⡟⠋⠻⣶⡒⠒⠦⠤⡤⡈⢻⡆
// ⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⡇⢀⠀⠘⣷⠀⠀⠀⡇⡇⣸⠇
// ⠀⠀⠀⣀⣀⣀⣀⣠⣤⣤⣤⣤⣤⣖⣺⢻⣝⣋⣽⣳⣶⣶⣿⣦⡴⣼⢷⡟⠋⠀
// ⠛⠛⠛⠉⠉⠉⠉⠉⠀⠀⠀⠀⠀⠳⣌⠉⠉⠉⣡⠞⠉⠉⠛⠳⢶⣼⢸⠂⠀⠀
// ⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠉⠉⠉⠀⠀⠀⠀⠀⠀⠀⠙⣿⡀⠀⠀
//
//       when million, millions.
// ⠀⠀⠀⠀⠀⠀⠀⣠⣀⣀⣀⣀⣀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀
// ⠀⠀⠀⠀⠀⢀⣀⣕⡋⠘⠐⠂⡑⠃⠉⢲⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀
// ⠀⠀⠀⠀⠀⢻⠃⢀⡤⠰⡎⠈⣉⠀⠀⢸⡄⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀
// ⠀⠀⠀⡀⢀⡎⠀⠠⣀⠀⠈⠉⠁⠠⢄⠈⡗⠲⣤⣤⣀⠀⠀⠀⠀⠀⠀⠀⠀⠀
// ⠀⢥⣦⠶⣗⢩⣰⢈⢁⠀⣀⠀⠀⢀⠨⢠⠇⠀⢹⢳⡉⣓⣶⣄⠀⠀⠀⠀⠀⠀
// ⠀⠀⠀⢀⠀⠉⠙⠢⠥⣄⣀⣀⣀⣤⠴⠋⠀⠀⢸⡎⣿⡀⠀⢹⣏⠉⠉⠒⢦⠀
// ⠀⠀⡀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⣾⠀⢸⣇⣴⠈⣿⡄⠀⢀⠻⠀
// ⣠⠤⠤⠤⠤⠤⠤⠤⠤⠤⠤⠤⠤⠴⣶⣭⣽⣻⣻⣯⣽⣿⣿⣀⣻⡇⠀⠀⡅⠀
// ⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠐⠚⠋⠳⣀⣀⣀⣀⣀⣠⠞⠃⠀⠀⠉⠉⠉⠙⠓

//                       %%.   (%#     %&%     #%/    %&.    (%&/    .%&.   #&#    .%&&
//                       @@.   %@&    &@*@&    &@#    @@.   *@#%@,   .@@    %@%    &@*@&
//                       @@@@@@@@&   %@* *@%   &@@@@@@@@.  ,@@  @@.  .@@@@@@@@%   &@* *@#
//                       @@.   #@&  #@@@@@@@(  &@#    @@. .@@@@@@@@  .@@.   %@%  %@@@@@@@(
//                       @@    #@% *@&     &@* &@#    @@. @@*    ,@& .@@    %@% /@&     &@*
//

//
//                                 .,/#%&@@@@@@@@@@@@@@@@@@@@@@&&&&&&&&&&&&&&&&@@@@@@@@@@@@@@#.
//                         *%@@@@@@@@&(#*,                                                  .%@@&.
//                  .*&@@//*.                                                                  ,&@@/
//               /@@@&,                                                                          ,@@@,
//            /@@@%.                                                                               %@@(
//            &@*   (@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@        (@@(
//           ,@@                   .,,.                     *###/.                                   ,@@(
//          *&@%               .(    .   (.             (/          (.                                 &&@.
//        *&@@*                %   (@&&   &            &    %@@@&    #.                                 *@@@/
//     .&@@&,                   #,      ,#              #    */*.   .(                                     @@@&,
//    &@@%                                                *&/****##                                           *@@%
//   &@@*                                  #@#                                                                  .&@&
//   @@#                                   #@%                                                                    #@@
//   &@#                                .%@@@(                                                                     @@(
//   (@&                              #@@%,                @@@@@@#                                                 &@&
//    (@&                             %@%             ./(/*.    &@*                                               .@@*
//     /@@(                            ,@@(         (&&&%#%&@ (@@&                                               ,@@%
//      ,&@&                             .#@@%%@&              .                                               ,@@@,
//        &@&                                                                                               ,%@@&.
//        *@@,             .@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@.                            .#&@*
//         &@(                &.     %@     @#      /@.    @/         .@.                               .@@@,
//         &@#                 ,@.   %@     @#      /@.    @/        %/                                .@@(
//         &@(                   #(((&@#((((@&((((((%@#(#((@%((((((#@.                                 @@/
//         &@(                     &,%@     @#      /@.    @/     &,                                 *@@*
//         @@.                      .@@     @#      (@.    @/   #(                                 /@@&
//        /@@.                        (#    @#      &@     @/ /&                                .&@@#
//        &@#                           ##  @#      &@    ,@#&.                              /@@@&.
//        &@/                             (#@#      &@    /@,                            /&@@@/
//       *@@.                               &&.     &@  #%.                         ,%@@@&*
//       /@@.                                  ,&%((@&(                        *%@@@@(.
//       (@@                                                             .(&@@@%/
//       /@@.                                                       .#@@@@#.
//        %@&.                                                 .*&@@@#.
//         ,&@&,                                   .,(&@@@@@@@@&(,
//            #@@@&/.                     ..*#&@@@@@@&&%(..
//                /&@@@@@@@@@@@@@@@@@@@@@@&%(.
//
//
//
//           .@@,   (@@@@@#  #@@@@@%    /@,  .&#  ,%@@&*   @/   &@       #@%    @@,  *@   *%@@&/  (@@@@@# .@#   (@,
//          .@*,@.  (@.   &% #@*,,,       &#/@,  &%    (@  @/   &@      #& &%   @.&/ *@ .@*       (@.   @%  *@.&#
//          @%/*#@. (@,,%@.  #@            &@    &#    (@  @/   &@     (@/**@%  @. ##*@ .@,  .,&@ (@*,%@.    .@*
//        .@#    (@.(@.   &% #@@@@@&.      %@     /@@@@(   *&@@@%.    (@.    @# @.  /@@   #@@@@%/ (@.   &%   .@*   #@
//                                                                                                                 ,,
//
//         *@&&&@#  (@.   @* #@&&&@(  %@&&&&( ,@@&@%
//         /@.   && (@.   @* #@    @( &@%%%%.     (@,
//         /@.   &# /@.   @* #@    @/ &@.       %&
//         *&&&&#.   ,#&&#.  #&&&&(.  #&&&&&@   %&

static void *cheat_module_base = 0;

static std::pair<uint64_t, std::string> get_containing_module(uint64_t address) {
    struct _LDR_DATA_TABLE_ENTRY_ {
        PVOID Reserved1[2];
        LIST_ENTRY InMemoryOrderLinks;
        PVOID Reserved2[2];
        PVOID DllBase;
        PVOID Reserved3[2];
        UNICODE_STRING FullDllName;
        UNICODE_STRING BaseDllName;
    };

    const auto peb = reinterpret_cast<PEB *>(reinterpret_cast<TEB *>(__readfsdword(0x18))->ProcessEnvironmentBlock);
    const auto module_list = &peb->Ldr->InMemoryOrderModuleList;

    for (auto i = module_list->Flink; i != module_list; i = i->Flink) {
        auto entry = CONTAINING_RECORD(i, _LDR_DATA_TABLE_ENTRY_, InMemoryOrderLinks);

        if (!entry)
            continue;

        const auto module_start = (uint64_t) entry->DllBase;
        const auto dos_headers = (IMAGE_DOS_HEADER *) module_start;
        const auto nt_headers = (IMAGE_NT_HEADERS *) (module_start + dos_headers->e_lfanew);

        if (address >= module_start && address <= module_start + nt_headers->OptionalHeader.SizeOfImage) {
            std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;

            return std::make_pair(module_start,
                                  converter.to_bytes(entry->BaseDllName.Buffer, entry->BaseDllName.Buffer + entry->BaseDllName.Length / 2));
        }
    }

    const auto dos = (IMAGE_DOS_HEADER*) cheat_module_base;
    if (dos && dos->e_magic == IMAGE_DOS_SIGNATURE) {
        const auto nt = (IMAGE_NT_HEADERS*) ((uintptr_t) cheat_module_base + dos->e_lfanew);
        const auto image_size = nt->OptionalHeader.SizeOfImage;
        if ((DWORD64) address >= (DWORD64) cheat_module_base && (DWORD64) address <= (DWORD64) cheat_module_base + image_size) {
            return std::make_pair((uint64_t) cheat_module_base, xs("<cheat>"));
        }
    }

    const DWORD64 base_addr = (DWORD64) cheat_module_base;
    return std::make_pair((uint64_t) base_addr, std::vformat(std::string_view(xs("<unknown module {:#x}>")), std::make_format_args(base_addr)));

    return std::make_pair(0, xs("<unknown module>"));
}

long __stdcall unhandledExceptionFilter(EXCEPTION_POINTERS *info) {
    if (cheat::panic)
        return EXCEPTION_CONTINUE_SEARCH;

    if (info->ExceptionRecord->ExceptionCode != EXCEPTION_ACCESS_VIOLATION)
        return EXCEPTION_CONTINUE_SEARCH;

    DWORD64 displacement;

    const auto symbol = (SYMBOL_INFO *) std::malloc(sizeof(SYMBOL_INFO) + 256);

    if (symbol != nullptr) {
        const auto [module_base, module_name] = get_containing_module((uint64_t) info->ExceptionRecord->ExceptionAddress);

        symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
        symbol->MaxNameLen = 255;

        std::string symbol_info;

        if (SymFromAddr(GetCurrentProcess(), (DWORD64) info->ExceptionRecord->ExceptionAddress, &displacement, symbol)) {
            const char* sym_name = symbol->Name;
            symbol_info = std::vformat(std::string_view(xs("{}!{} + {:#x}")), std::make_format_args(module_name, sym_name, displacement));
        } else {
            const DWORD64 offset = (DWORD64) info->ExceptionRecord->ExceptionAddress - module_base;
            symbol_info = std::vformat(std::string_view(xs("{} + {:#x}")), std::make_format_args(module_name, offset));
        }

        const uintptr_t exc_code = (uintptr_t) info->ExceptionRecord->ExceptionCode;
        std::string message = std::vformat(std::string_view(xs("Exception code: {:#x}\nException information: {}\n")),
                                   std::make_format_args(exc_code, symbol_info));

        if (info->ContextRecord->Ebp != 0) {
            message += xs("\n");

            auto ebp = info->ContextRecord->Ebp;

            while (true) {
                const auto eip_ebp = ebp + 4;
                const auto eip = *(uint32_t *) eip_ebp;

                if (eip == 0)
                    break;

                ebp = *(uint32_t *) ebp;

                const auto [module_base, module_name] = get_containing_module((uint64_t) eip);

                symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
                symbol->MaxNameLen = 255;

                if (SymFromAddr(GetCurrentProcess(), (DWORD64) eip, &displacement, symbol)) {
                    const char* sym_name = symbol->Name;
                    message += std::vformat(std::string_view(xs("> {}!{} + {:#x}\n")), std::make_format_args(module_name, sym_name, displacement));
                } else {
                    const DWORD64 offset = (DWORD64) eip - module_base;
                    message += std::vformat(std::string_view(xs("> {} + {:#x}\n")), std::make_format_args(module_name, offset));
                }
            }
        }

        message += xs("\nCopy this information using CTRL+C and report it to the developers");

        MessageBoxA(nullptr, message.data(), nullptr, MB_ICONERROR | MB_OK);
        ExitProcess(0);
    }

    return EXCEPTION_EXECUTE_HANDLER;
}

unsigned long __stdcall initial_thread(void *base_pointer) {
    if (!security::run_security_measures())
        return 0;

    cheat_module_base = base_pointer;

    AddVectoredExceptionHandler(true, unhandledExceptionFilter);
    SymInitialize(GetCurrentProcess(), nullptr, true);

    logging::init();

#ifdef _DEBUG
    debug_overlay::init();
#endif

    auto i = 0;

    while (pe::get_module(xs("serverbrowser.dll")) == 0u) {
        if (++i >= 60) {
            logging::error(xs("couldn't find the 'serverbrowser.dll' module"));

            goto load_failed;
        }

        Sleep(500);
    }

    if (!cheat::init())
        goto load_failed;

#ifdef _DEBUG
    while (!input::is_key_down(VK_DELETE) || !input::is_key_down(VK_END))
        Sleep(100);
#else
    while (true)
        Sleep(100);
#endif

load_failed:
    cheat::undo();
    SymCleanup(GetCurrentProcess());

    HMODULE h_mod = nullptr;
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR) base_pointer, &h_mod) && h_mod != nullptr) {
        FreeLibraryAndExitThread((HMODULE) base_pointer, 0);
    } else {
        ExitThread(0);
    }

    return 0;
}

int __stdcall DllMain(HMODULE base_pointer, unsigned int reason_to_call, void *) {
    if (reason_to_call == 1) {
        DisableThreadLibraryCalls((HMODULE) base_pointer);
        CreateThread(nullptr, 0, &initial_thread, base_pointer, 0, nullptr);
    }
    return 1;
}
