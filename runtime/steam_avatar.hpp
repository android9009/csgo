#pragma once

namespace steam_avatar {
// Render-thread only. Returns nullptr while Steam or the image is unavailable.
void* texture();

// D3DPOOL_DEFAULT textures must be recreated after a device reset.
void on_device_reset();
}
