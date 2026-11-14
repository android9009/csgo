#include "steam_avatar.hpp"

#include <Windows.h>

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <vector>

#include "../sdk/includes/imgui/imgui_impl_dx9.h"

namespace {
using interface_accessor_t = void*(__cdecl*)();
using create_interface_t = void*(__cdecl*)(const char*);
using get_steam_id_t = std::uint64_t(__cdecl*)(void*);
using get_large_avatar_t = int(__cdecl*)(void*, std::uint64_t);
using get_image_size_t = bool(__cdecl*)(void*, int, std::uint32_t*, std::uint32_t*);
using get_image_rgba_t = bool(__cdecl*)(void*, int, unsigned char*, int);

struct steam_api_t {
    void* user = nullptr;
    void* friends = nullptr;
    void* utils = nullptr;
    get_steam_id_t get_steam_id = nullptr;
    get_large_avatar_t get_large_avatar = nullptr;
    get_image_size_t get_image_size = nullptr;
    get_image_rgba_t get_image_rgba = nullptr;
};

steam_api_t api;
bool api_ready = false;
ImTextureID avatar_texture = nullptr;
std::uint64_t next_attempt_ms = 0;

template<typename T>
T proc_address(HMODULE module, const char* name) {
    return reinterpret_cast<T>(GetProcAddress(module, name));
}

void* find_interface(HMODULE module, const char* legacy_name,
                     std::initializer_list<const char*> accessor_names,
                     std::initializer_list<const char*> interface_names) {
    if (const auto accessor = proc_address<interface_accessor_t>(module, legacy_name)) {
        if (void* instance = accessor())
            return instance;
    }

    for (const char* name : accessor_names) {
        if (const auto accessor = proc_address<interface_accessor_t>(module, name)) {
            if (void* instance = accessor())
                return instance;
        }
    }

    const auto create_interface = proc_address<create_interface_t>(module, "SteamInternal_CreateInterface");
    if (!create_interface)
        return nullptr;

    for (const char* name : interface_names) {
        if (void* instance = create_interface(name))
            return instance;
    }
    return nullptr;
}

bool resolve_steam_api() {
    if (api_ready)
        return true;

    const HMODULE module = GetModuleHandleW(L"steam_api.dll");
    if (!module)
        return false;

    steam_api_t candidate;
    candidate.user = find_interface(module, "SteamUser",
        { "SteamAPI_SteamUser_v023", "SteamAPI_SteamUser_v022", "SteamAPI_SteamUser_v021",
          "SteamAPI_SteamUser_v020", "SteamAPI_SteamUser_v019", "SteamAPI_SteamUser_v018" },
        { "SteamUser023", "SteamUser022", "SteamUser021", "SteamUser020", "SteamUser019", "SteamUser018" });
    candidate.friends = find_interface(module, "SteamFriends",
        { "SteamAPI_SteamFriends_v017", "SteamAPI_SteamFriends_v016", "SteamAPI_SteamFriends_v015",
          "SteamAPI_SteamFriends_v014", "SteamAPI_SteamFriends_v013", "SteamAPI_SteamFriends_v012" },
        { "SteamFriends017", "SteamFriends016", "SteamFriends015", "SteamFriends014",
          "SteamFriends013", "SteamFriends012" });
    candidate.utils = find_interface(module, "SteamUtils",
        { "SteamAPI_SteamUtils_v010", "SteamAPI_SteamUtils_v009", "SteamAPI_SteamUtils_v008",
          "SteamAPI_SteamUtils_v007" },
        { "SteamUtils010", "SteamUtils009", "SteamUtils008", "SteamUtils007" });

    candidate.get_steam_id = proc_address<get_steam_id_t>(module, "SteamAPI_ISteamUser_GetSteamID");
    candidate.get_large_avatar = proc_address<get_large_avatar_t>(module, "SteamAPI_ISteamFriends_GetLargeFriendAvatar");
    candidate.get_image_size = proc_address<get_image_size_t>(module, "SteamAPI_ISteamUtils_GetImageSize");
    candidate.get_image_rgba = proc_address<get_image_rgba_t>(module, "SteamAPI_ISteamUtils_GetImageRGBA");

    if (!candidate.user || !candidate.friends || !candidate.utils ||
        !candidate.get_steam_id || !candidate.get_large_avatar ||
        !candidate.get_image_size || !candidate.get_image_rgba)
        return false;

    api = candidate;
    api_ready = true;
    return true;
}

void retry_after(std::uint64_t delay_ms) {
    next_attempt_ms = static_cast<std::uint64_t>(GetTickCount64()) + delay_ms;
}
} // namespace

namespace steam_avatar {
void* texture() {
    if (avatar_texture)
        return avatar_texture;

    const std::uint64_t now = static_cast<std::uint64_t>(GetTickCount64());
    if (next_attempt_ms != 0 && now < next_attempt_ms)
        return nullptr;

    if (!resolve_steam_api()) {
        retry_after(2000);
        return nullptr;
    }

    const std::uint64_t steam_id = api.get_steam_id(api.user);
    if (steam_id == 0) {
        retry_after(1000);
        return nullptr;
    }

    const int avatar_handle = api.get_large_avatar(api.friends, steam_id);
    if (avatar_handle <= 0) {
        // -1 means that Steam is downloading the avatar; 0 means no avatar is set.
        retry_after(avatar_handle == -1 ? 250 : 5000);
        return nullptr;
    }

    std::uint32_t width = 0;
    std::uint32_t height = 0;
    if (!api.get_image_size(api.utils, avatar_handle, &width, &height) || width == 0 || height == 0) {
        retry_after(500);
        return nullptr;
    }

    constexpr std::uint32_t max_avatar_dimension = 1024;
    if (width > max_avatar_dimension || height > max_avatar_dimension ||
        width > std::numeric_limits<std::uint32_t>::max() / height / 4) {
        retry_after(2000);
        return nullptr;
    }

    const std::size_t rgba_size = static_cast<std::size_t>(width) * height * 4;
    if (rgba_size > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        retry_after(2000);
        return nullptr;
    }

    std::vector<unsigned char> rgba(rgba_size);
    if (!api.get_image_rgba(api.utils, avatar_handle, rgba.data(), static_cast<int>(rgba_size))) {
        retry_after(500);
        return nullptr;
    }

    avatar_texture = ImGui_CreateTextureRGBA(static_cast<int>(width), static_cast<int>(height), rgba.data());
    if (!avatar_texture)
        retry_after(1000);
    return avatar_texture;
}

void on_device_reset() {
    if (avatar_texture) {
        ImGui_DestroyTexture(avatar_texture);
        avatar_texture = nullptr;
    }
    next_attempt_ms = 0;
}
} // namespace steam_avatar
