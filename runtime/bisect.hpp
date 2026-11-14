#pragma once

// Temporary fault isolation for the "team select ignores clicks" report.
//
// Put a text file named bisect.txt next to the injected DLL and list the parts
// to switch off, separated by spaces or new lines. The file is read once, on
// first use, so it must exist before the DLL is injected. Without the file
// nothing changes. Remove this header and its call sites once the faulty hook
// has been identified.
//
//   panel        VGUI PaintTraverse hook (HUD removals + FocusOverlayPanel)
//   focus        only the FocusOverlayPanel mouse-input writes
//   wndproc      window procedure passes everything to the game (Insert still toggles)
//   createmove   ClientMode::CreateMove hook
//   overrideview ClientMode::OverrideView hook (thirdperson, cvar overrides)
//   sound        IEngineSound::EmitSound hook
//   events       misc + chams game event listeners
//   filters      console / chat filter commands
//   framestage   CHLClient::FrameStageNotify hook
//   viewmodel    CalcViewModelView hook
//   radar        IVEngineClient::IsHLTV hook
//   chams        DrawModelExecute / StudioRender hooks
//   render       the whole Present path: no ImGui, no window procedure hook, no
//                game hooks at all (only the D3D9 Present/Reset detours remain)
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>

namespace bisect {
namespace detail {
inline std::string load() {
    HMODULE self = nullptr;
    char path[MAX_PATH] = {};
    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
            GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCSTR>(&load), &self) ||
        !GetModuleFileNameA(self, path, MAX_PATH))
        return {};
    char* slash = std::strrchr(path, '\\');
    if (!slash || static_cast<std::size_t>(slash - path) + 12 >= MAX_PATH)
        return {};
    std::strcpy(slash + 1, "bisect.txt");
    std::string text = " ";
    if (FILE* file = std::fopen(path, "rb")) {
        char buffer[512];
        std::size_t read = 0;
        while ((read = std::fread(buffer, 1, sizeof(buffer), file)) > 0)
            text.append(buffer, read);
        std::fclose(file);
    }
    for (char& c : text) {
        if (c == '\r' || c == '\n' || c == '\t' || c == ',' || c == ';')
            c = ' ';
        else if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c - 'A' + 'a');
    }
    text.push_back(' ');
    return text;
}

inline const std::string& tokens() {
    static std::string value;
    static std::once_flag once;
    std::call_once(once, [] { value = load(); });
    return value;
}
} // namespace detail

// True when `name` is listed in bisect.txt.
inline bool off(const char* name) {
    const std::string& list = detail::tokens();
    if (list.size() <= 2)
        return false;
    return list.find(" " + std::string(name) + " ") != std::string::npos;
}

// The raw list, for the startup log.
inline const char* listed() {
    return detail::tokens().c_str();
}
} // namespace bisect
