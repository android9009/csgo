#pragma once
#include "state.hpp"
namespace MiscRuntime {
void Initialize();
void Shutdown();
void CreateMove(CUserCmd*);
void Frame();
}
