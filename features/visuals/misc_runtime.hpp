#pragma once
#include "misc_state.hpp"
namespace VisualMisc {
bool CurrentMatrix(std::array<float,16>& matrix);
void Initialize();
void Shutdown();
void BeforeFrame(int stage);
void Frame(int stage);
bool HideScopePanel(const char* name);
float LocalAlpha(int entity_index);
bool RemovingRagdolls();
bool HideRagdoll(void* entity);
}
