#pragma once

namespace world_visuals {
// Called once the existing client-mode/input/cvar interfaces are acquired.
// Installs the real OverrideView camera/FOV path and keeps client-side viewmodel/world cvars in sync.
bool initialize(void* client_mode, void* input, void* cvar_interface,
                void* engine_client, void* entity_list, void* panel_interface,
                void* client_module);
} // namespace world_visuals
