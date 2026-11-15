#include "hooks.hpp"
#include "../context.hpp"
#include "../sdk/interfaces/interfaces.hpp"
#include "../sdk/entity/entity.hpp"
#include "../sdk/studiohdr.hpp"
#include "../sdk/material.hpp"
#include <cstring>
namespace HooksInternal {
static bool IsPlayerModel(const char* name) {
    return name && std::strstr(name, "models/player/") != nullptr;
}
static bool IsViewModel(const char* name) {
    return name && (std::strstr(name, "models/weapons/v_") != nullptr ||
                    std::strstr(name, "models/weapons/viewmodels/") != nullptr);
}
void __fastcall DrawModelExecute(void* ecx, void* /*edx*/, void* context,
                                 const DrawModelState_t& state, const ModelRenderInfo_t& info,
                                 matrix3x4_t* bone_to_world) {
    // Resolve model + entity for future chams/glow passes. The hook is hot
    // (called for every model), so skip the lookups outside of a match.
    if (ctx.in_game) {
        const char* model_name = nullptr;
        if (Interfaces::ModelInfo && info.model) model_name = Interfaces::ModelInfo->GetModelName(info.model);
        CBaseEntity* entity = nullptr;
        if (Interfaces::EntityList && info.entity_index > 0)
            entity = static_cast<CBaseEntity*>(Interfaces::EntityList->GetClientEntity(info.entity_index));
        const bool is_player = IsPlayerModel(model_name);
        const bool is_viewmodel = IsViewModel(model_name);
        (void)entity; (void)is_player; (void)is_viewmodel; (void)state;
        // TODO: features/visuals/chams entry point goes here
        // (Interfaces::ModelRender->ForcedMaterialOverride + IMaterialSystem::FindMaterial)
    }
    if (auto original = Hooks::GetDrawModelExecuteOriginal())
        original(ecx, context, state, info, bone_to_world);
}
}
