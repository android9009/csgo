#include "displacement.hpp"
#include "../offsets/offsets.hpp"
#include "../patterns/patterns.hpp"
namespace Displacement {
data_t data{};
void Init() {
    data.entity_list = offsets::Get("dwEntityList");
    data.client_state = offsets::Get("dwClientState");
    data.local_player = offsets::Get("dwLocalPlayer");
    data.global_vars = offsets::Get("dwGlobalVars");
    data.glow_manager = offsets::Get("dwGlowObjectManager");
    data.input = offsets::Get("dwInput");
    data.view_matrix = offsets::Get("dwViewMatrix");
    data.force_jump = offsets::Get("dwForceJump");
    data.force_attack = offsets::Get("dwForceAttack");
    data.d3d_device = offsets::Get("dwppDirect3DDevice9");
    data.set_abs_origin = Patterns::Get("set_abs_origin");
    data.set_abs_angles = Patterns::Get("set_abs_angles");
}
}
