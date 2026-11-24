#pragma once
#include "../../../sdk/studiohdr.hpp"
namespace ShotChams {
void Initialize();
void Shutdown();
void Capture(const DrawModelState_t&,const ModelRenderInfo_t&,matrix3x4_t*);
void SceneEnd();
}
