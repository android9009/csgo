#pragma once

namespace world_visuals {
// Installs the OverrideView camera/FOV path (HACKS->client_mode) and keeps the
// client-side viewmodel/world cvars in sync. Requires HACKS to be initialized.
bool initialize();
} // namespace world_visuals
