#include "../core/cheat/cheat.h"
#include "../core/hooks/hooks.h"
#include "../core/interfaces/interfaces.h"

void __fastcall hooks::calc_view(uintptr_t ecx, uintptr_t edx, vector_t &eye_origin, vector_t &eye_angles, float &z_near, float &z_far, float &fov) {
    const auto player = reinterpret_cast<c_player *>(ecx);

    if (!player || player != cheat::local_player)
        return calc_view_original(ecx, edx, eye_origin, eye_angles, z_near, z_far, fov);

    calc_view_original(ecx, edx, eye_origin, eye_angles, z_near, z_far, fov);
}