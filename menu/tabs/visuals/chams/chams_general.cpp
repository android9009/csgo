#include "../visuals.hpp"

namespace menu::group
{
	void visuals_models_general(std::shared_ptr<layout> &e)
	{
		//VIRTUALIZER_TIGER_WHITE_START;
		ADD("Enabled", "visuals>chams>general>enabled", checkbox, cfg.visuals.chams.enable);
		ADD("Skip occlusion", "visuals>chams>general>skip occlusion", checkbox, cfg.visuals.chams.check_occlusion);
		ADD_INLINE(
			"Glow", MAKE("visuals>esp>player>glow", checkbox, cfg.visuals.esp.glow.enabled),
			MAKE("visuals>esp>player>glow>settings", settings, [](auto e) { ADD_INLINE("Team", MAKE("visuals>esp>player>glow>settings>team", checkbox, cfg.visuals.esp.
				glow. team), MAKE("visuals>esp>player>glow>settings>team>color", color_picker, cfg.visuals.esp.glow. team_color)); ADD_INLINE("Enemy", MAKE(
				"visuals>esp>player>glow>settings>enemy", checkbox, cfg.visuals.esp.glow .enemy), MAKE("visuals>esp>player>glow>settings>enemy>color", color_picker, cfg.
				visuals.esp.glow. enemy_color)); ADD_INLINE("Local", MAKE("visuals>esp>player>glow>settings>local", checkbox, cfg.visuals.esp.glow .local), MAKE(
				"visuals>esp>player>glow>settings>local>color", color_picker, cfg.visuals.esp.glow. local_color)); }));

		//VIRTUALIZER_TIGER_WHITE_END;
	}

}
