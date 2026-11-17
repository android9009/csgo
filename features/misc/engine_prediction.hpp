#pragma once
struct CUserCmd;
class CCSPlayer;
namespace EnginePrediction {
// Called on the engine/game thread only. Start leaves the one-tick predicted
// entity state available to post movement; End restores prediction bookkeeping.
bool Start(CCSPlayer* local,CUserCmd& command);
void End();
void Reset();
void Shutdown();
}
