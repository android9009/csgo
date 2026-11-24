#include "hooks.hpp"
#include "../../context.hpp"
#include "../../features/visuals/chams/shot_chams.hpp"
#include "../interfaces/interfaces.hpp"
#include "../../utils/offsets/offsets.hpp"
#include "../../utils/memory.hpp"
#include "../../utils/math/math.hpp"
#include <cstring>
namespace HooksInternal {
void __fastcall SceneEnd(void* ecx, void* /*edx*/) {
    if (auto original = Hooks::GetSceneEndOriginal()) original(ecx);
    ShotChams::SceneEnd();
    __try {
        if (!Interfaces::Engine || !Interfaces::Globals) return;
        int width = 0, height = 0;
        Interfaces::Engine->GetScreenSize(width, height);
        ctx.screen_size = Vector2D(static_cast<float>(width), static_cast<float>(height));
        auto client_base = reinterpret_cast<std::uintptr_t>(MEM::GetModuleBase("client.dll"));
        const auto matrix_offset = offsets::Get("dwViewMatrix");
        // 48-byte matrix3x4 view matrix; row 3 is filled with [0,0,0,1] (math.cpp).
        if (client_base && matrix_offset)
            Math::capture_view_matrix(reinterpret_cast<const void*>(client_base + matrix_offset));
        // TODO: features/visuals world entry points go here
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}
}
