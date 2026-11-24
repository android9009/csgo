#include "../../visuals.hpp"

namespace menu::group
{
	void visuals_misc_other(std::shared_ptr<layout> &e)
	{
		ADD("Texture transparency", "visuals>misc>other>texture transparency", slider<float>, 0.f, 100.f, cfg.misc.texture_transparency, "%.0f%%");
		ADD("Prop transparency", "visuals>misc>other>prop transparency", slider<float>, 0.f, 100.f, cfg.visuals.prop_transparency, "%.0f%%");
		{
			const auto tracer = MAKE("visuals>misc>other>tracers", combo_box, cfg.misc.tracer_targets);
			tracer->allow_multiple = true;
			const auto local = MAKE("visuals>misc>other>tracers>local", selectable, "Local", 0);
			const auto enemy = MAKE("visuals>misc>other>tracers>enemy", selectable, "Enemy", 1);
			const auto team = MAKE("visuals>misc>other>tracers>team", selectable, "Teammates", 2);
			local->swatch = MAKE("visuals>misc>other>tracers>local>color", color_picker, cfg.misc.tracer_colors[0]);
			enemy->swatch = MAKE("visuals>misc>other>tracers>enemy>color", color_picker, cfg.misc.tracer_colors[1]);
			team->swatch = MAKE("visuals>misc>other>tracers>team>color", color_picker, cfg.misc.tracer_colors[2]);
			tracer->add({local, enemy, team});
			const auto gear = MAKE("visuals>misc>other>tracers>settings", settings, [](auto e) {
				const std::array<const char *, 3> names{"Local", "Enemy", "Teammates"};
				for (int i = 0; i < 3; ++i) {
					const auto key = std::string("visuals>misc>other>tracers>settings>") + std::to_string(i);
					auto style = MAKE_RUNTIME(key + ">style", combo_box, cfg.misc.tracer_style[i], vec2{}, vec2{70.f, 20.f});
					style->add({MAKE_RUNTIME(key + ">laser", selectable, "Laser", 0), MAKE_RUNTIME(key + ">line", selectable, "Line", 1)});
					const auto row = make_inlined(hash(key + ">row"), [&](std::shared_ptr<layout> &r) {
						r->add(std::make_shared<label>(control_id{hash(key + ">label")}, names[i]));
						r->add(style);
					});
					row->is_visible = cfg.misc.tracer_targets->test(i);
					e->add(row);
				}
			});
			gear->is_visible = cfg.misc.tracer_targets->test(0) || cfg.misc.tracer_targets->test(1) || cfg.misc.tracer_targets->test(2);
			tracer->callback = [weak = std::weak_ptr<settings>(gear)](bits &v) {
				if (const auto g = weak.lock()) {
					const bool show = v.test(0) || v.test(1) || v.test(2);
					if (g->is_visible != show) g->set_visible(show);
				}
				for (int i = 0; i < 3; ++i) {
					const auto id = hash(std::string("visuals>misc>other>tracers>settings>") + std::to_string(i) + ">row");
					if (const auto row = ctx->find(id); row && row->is_visible != v.test(i)) row->set_visible(v.test(i));
				}
			};
			ADD_INLINE("Bullet tracers", tracer, gear);
		}
		{
			const auto c = MAKE("visuals>misc>other>hitmarker", combo_box, cfg.misc.hitmarker);
			c->allow_multiple = true;
			const auto world = MAKE("visuals>misc>other>hitmarker>world", selectable, "World", cfg_t::hitmarker_world);
			const auto screen = MAKE("visuals>misc>other>hitmarker>screen", selectable, "Screen", cfg_t::hitmarker_cross);
			const auto kibit = MAKE("visuals>misc>other>hitmarker>kibit", selectable, "Kibit", cfg_t::hitmarker_kibit);
			world->swatch = MAKE("visuals>misc>other>hitmarker>world>color", color_picker, cfg.misc.hitmarker_world_color);
			screen->swatch = MAKE("visuals>misc>other>hitmarker>screen>color", color_picker, cfg.misc.hitmarker_screen_color);
			kibit->swatch = MAKE("visuals>misc>other>hitmarker>kibit>color", color_picker, cfg.misc.hitmarker_kibit_color);
			c->add({world, screen, kibit});
			const auto gear = MAKE("visuals>misc>other>hitmarker>settings", settings, [](auto e) {
				const std::array<const char *, 3> names{"World", "Screen", "Kibit"};
				const std::array<int, 3> indices{cfg_t::hitmarker_world, cfg_t::hitmarker_cross, cfg_t::hitmarker_kibit};
				for (int i = 0; i < 3; ++i) {
					const auto key = std::string("visuals>misc>other>hitmarker>settings>") + std::to_string(indices[i]);
					const auto row = make_control(names[i], MAKE_RUNTIME(key + ">duration", slider<float>, 0.f, 10.f, cfg.misc.hitmarker_duration[indices[i]], "%.1f s", .1f));
					row->is_visible = cfg.misc.hitmarker->test(indices[i]);
					e->add(row);
				}
			});
			gear->is_visible = cfg.misc.hitmarker->test(cfg_t::hitmarker_world) || cfg.misc.hitmarker->test(cfg_t::hitmarker_cross) || cfg.misc.hitmarker->test(cfg_t::hitmarker_kibit);
			c->callback = [weak = std::weak_ptr<settings>(gear)](bits &v) {
				const std::array<int, 3> indices{cfg_t::hitmarker_world, cfg_t::hitmarker_cross, cfg_t::hitmarker_kibit};
				const bool show = v.test(indices[0]) || v.test(indices[1]) || v.test(indices[2]);
				if (const auto g = weak.lock(); g && g->is_visible != show) g->set_visible(show);
				for (const auto i : indices) {
					const auto id = hash(std::string("visuals>misc>other>hitmarker>settings>") + std::to_string(i) + ">duration");
					if (const auto slider = ctx->find(id))
						if (const auto row = slider->get_parent(); row && row->is_visible != v.test(i)) row->set_visible(v.test(i));
				}
			};
			ADD_INLINE("Hitmarker", c, gear);
		}
		ADD_INLINE("Show impacts", MAKE("visuals>misc>other>impacts", checkbox, cfg.misc.impacts),
			MAKE("visuals>misc>other>impacts>color1", color_picker, cfg.misc.impacts_color), MAKE("visuals>misc>other>impacts>color2", color_picker, cfg.misc.impacts_color2));
		ADD_INLINE("Out of FOV", MAKE("visuals>misc>other>out of fov", checkbox, cfg.visuals.out_of_fov),
			MAKE("visuals>misc>other>out of fov>settings", settings, [](auto e) {
				ADD("Color", "visuals>misc>other>out of fov>color", color_picker, cfg.visuals.out_of_fov_col);
				ADD("Distance", "visuals>misc>other>out of fov>distance", slider<float>, 1.f, 100.f, cfg.visuals.out_of_fov_dst, "%.0f%%");
				ADD("Size", "visuals>misc>other>out of fov>size", slider<float>, 1.f, 50.f, cfg.visuals.out_of_fov_size, "%.0fpx");
			}));
		{
			const auto c = MAKE("visuals>misc>other>removals", combo_box, cfg.misc.removals);
			c->allow_multiple = true;
			c->callback = [](bits &v) { update_ragdoll_chams_visibility(v.test(removal_ragdolls)); };
			const std::array<const char *, 10> names{"Flashbang Effect", "Smoke Rendering", "Particle", "Post Processing", "Water Effects", "Blood Effects", "Decals", "Ragdolls", "Visual punch", "Shadows"};
			for (int i = 0; i < static_cast<int>(names.size()); ++i) {
				const auto item = MAKE_RUNTIME(std::string("visuals>misc>other>removals>") + std::to_string(i), selectable, names[i], i);
				c->add(item);
			}
			ADD_INLINE("Removals", c);
		}
	}

}
