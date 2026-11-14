#pragma once

#include "dependencies/source.hpp"

namespace sound_esp {
// Called by the existing IEngineSound hook. Positions are copied immediately;
// the renderer never retains engine-owned pointers.
void on_sound(int entity_index, int speaker_entity, const char* sound_entry,
              bool has_origin, float x, float y, float z);
void render(const visuals_source::frame_data& frame);
} // namespace sound_esp
