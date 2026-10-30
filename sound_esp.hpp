#pragma once

struct sound_marker_t
{
	bool valid{};
	float time{};
	vec3_t origin{};

	INLINE void reset()
	{
		valid = false;
		time = 0.f;
		origin.reset();
	}
};

class c_sound_esp
{
private:
	sound_marker_t markers[65]{};

public:
	void on_sound(c_cs_player* player, const vec3_t& origin);
	void render();

	INLINE void reset()
	{
		for (auto& marker : markers)
			marker.reset();
	}
};

#ifdef _DEBUG
inline auto SOUND_ESP = std::make_unique<c_sound_esp>();
#else
CREATE_DUMMY_PTR(c_sound_esp);
DECLARE_XORED_PTR(c_sound_esp, GET_XOR_KEYUI32);

#define SOUND_ESP XORED_PTR(c_sound_esp)
#endif
