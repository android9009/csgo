#pragma once
#include <atomic>
#include <wtypes.h>
#include <functional>
#include <mutex>

class c_key_states
{
protected:
	std::atomic< short > array_states[256] = {};

public:
	c_key_states();

	struct key_info_t
	{
		WPARAM key{};
		bool state{};
	};

	__forceinline bool at(int index) const
	{
		return array_states[index];
	}

	bool proc_key(int idx, int key, bool state);
	key_info_t get_key_state(UINT uMsg, WPARAM wParam);
	bool key_updated(int key, UINT uMsg, WPARAM wParam);
	void update(UINT uMsg, WPARAM wParam);

	// resolves every per-control bind stored in g_cfg.item_binds here so the
	// menu thread never touches the bind state directly
	void update_item_binds(UINT uMsg, WPARAM wParam);

	// combines the control value with its bind: a bound control is "enabled" as
	// long as the bind is held / toggled, even when the checkbox is off
	static bind_state_t resolve(const char* label, bool enabled);
};

// guards g_cfg.item_binds: the menu adds entries while the input thread walks the
// container every message
extern std::mutex item_binds_mutex;

#ifdef _DEBUG
inline auto KEY_STATES = std::make_unique<c_key_states>();
#else
CREATE_DUMMY_PTR(c_key_states);
DECLARE_XORED_PTR(c_key_states, GET_XOR_KEYUI32);

#define KEY_STATES XORED_PTR(c_key_states)
#endif