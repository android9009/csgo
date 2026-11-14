#pragma once
#include <cstdint>
namespace Displacement {
    struct data_t {
        std::uintptr_t entity_list = 0;
        std::uintptr_t client_state = 0;
        std::uintptr_t local_player = 0;
        std::uintptr_t global_vars = 0;
        std::uintptr_t glow_manager = 0;
        std::uintptr_t input = 0;
        std::uintptr_t view_matrix = 0;
        std::uintptr_t force_jump = 0;
        std::uintptr_t force_attack = 0;
        std::uintptr_t d3d_device = 0;
        std::uintptr_t set_abs_origin = 0;
        std::uintptr_t set_abs_angles = 0;
    };
    extern data_t data;
    void Init();
}
