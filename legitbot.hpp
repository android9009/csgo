#pragma once

#include "lagcomp.hpp"

struct legit_point_t
{
	c_cs_player* player{};
	anim_record_t* record{};

	int hitbox{ -1 };
	float fov{};
	float distance{};
	float score{ -FLT_MAX };

	vec3_t position{};

	INLINE bool valid() const
	{
		return player && hitbox >= 0;
	}
};

class c_legitbot
{
private:
	legit_weapon_t weapon_config{};

	c_cs_player* target{};
	anim_record_t* target_record{};

	int target_index{ -1 };
	int target_hitbox{ -1 };
	int trigger_index{ -1 };

	float target_since{};
	float trigger_since{};

	vec3_t target_point{};
	vec3_t last_punch{};

	bool aiming{};
	bool view_changed{};
	bool punch_seeded{};

	INLINE float now() const
	{
		return HACKS->global_vars ? HACKS->global_vars->realtime : 0.f;
	}

	int get_min_damage(c_cs_player* player) const;
	bool is_hitbox_enabled(int hitbox, unsigned int mask) const;
	bool passes_smoke_check(const vec3_t& start, const vec3_t& end) const;
	bool validate_point(const legit_point_t& point);
	void scan_player(c_cs_player* player, const vec3_t& eye_pos, std::vector<legit_point_t>& points);
	legit_point_t find_target(const vec3_t& eye_pos);

	void run_aim(const vec3_t& eye_pos);
	void run_trigger(const vec3_t& eye_pos);
	void apply_standalone_rcs();

public:
	void run();
	void render();
	void reset();
};

#ifdef _DEBUG
inline auto LEGITBOT = std::make_unique<c_legitbot>();
#else
CREATE_DUMMY_PTR(c_legitbot);
DECLARE_XORED_PTR(c_legitbot, GET_XOR_KEYUI32);

#define LEGITBOT XORED_PTR(c_legitbot)
#endif
