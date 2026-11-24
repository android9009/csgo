#include "../../visuals.hpp"

namespace menu::group
{
	void visuals_misc_world(std::shared_ptr<layout> &e)
	{
		auto &w = cfg.visuals.world_effects;
		ADD_INLINE("Nightmode", MAKE("visuals>world>nightmode", checkbox, w.nightmode), MAKE("visuals>world>nightmode>color", color_picker, w.nightmode_color));
		ADD_INLINE("Fullbright", MAKE("visuals>world>fullbright", checkbox, w.fullbright), MAKE("visuals>world>fullbright>color", color_picker, w.fullbright_color));
		ADD_INLINE("Sunlight", MAKE("visuals>world>sunlight", checkbox, w.sunlight), MAKE("visuals>world>sunlight>color", color_picker, w.sunlight_color));
		ADD_INLINE("Sky", MAKE("visuals>world>sky", checkbox, w.sky), MAKE("visuals>world>sky>color", color_picker, w.sky_color));
		ADD_INLINE("Fog", MAKE("visuals>world>fog", checkbox, w.fog), MAKE("visuals>world>fog>color", color_picker, w.fog_color));
		ADD_INLINE("Vignette", MAKE("visuals>world>vignette", checkbox, w.vignette), MAKE("visuals>world>vignette>color", color_picker, w.vignette_color));
		ADD_INLINE("Exposure", MAKE("visuals>world>exposure", checkbox, w.exposure_enabled), MAKE("visuals>world>exposure>settings", settings, [](auto e) {
			ADD("Exposure", "visuals>world>exposure>amount", slider<float>, 0.f, 100.f, cfg.visuals.world_effects.exposure, "%.0f%%");
		}));
		ADD_INLINE("Bloom", MAKE("visuals>world>bloom", checkbox, w.bloom), MAKE("visuals>world>bloom>settings", settings, [](auto e) {
			auto &b = cfg.visuals.world_effects;
			ADD("Strength", "visuals>world>bloom>strength", slider<float>, 0.f, 100.f, b.bloom_strength, "%.0f%%");
			ADD("Screen strength", "visuals>world>bloom>screen", slider<float>, 0.f, 100.f, b.bloom_screen_strength, "%.0f%%");
			ADD("Blur strength", "visuals>world>bloom>blur", slider<float>, 0.f, 100.f, b.bloom_blur_strength, "%.0f%%");
			ADD("Threshold", "visuals>world>bloom>threshold", slider<float>, 0.f, 100.f, b.bloom_threshold, "%.0f%%");
			ADD("Threshold width", "visuals>world>bloom>threshold width", slider<float>, 0.f, 100.f, b.bloom_threshold_width, "%.0f%%");
			for (int i = 0; i < 5; ++i) {
				const auto key = std::string("visuals>world>bloom>weight>") + std::to_string(i);
				const auto label_text = std::string("Blur weight ") + std::to_string(i + 1);
				auto weight_slider = MAKE_RUNTIME(key, slider<float>, 0.f, 100.f, b.bloom_weights[i], "%.0f%%", 1.f, vec2{70.f, 14.f});
				auto picker = MAKE_RUNTIME(key + ">color", color_picker, b.bloom_weight_colors[i]);
				e->add(make_inlined(hash(key + ">row"), [&](std::shared_ptr<layout> &row) {
					row->add(std::make_shared<label>(control_id{hash(key + ">label")}, label_text)); row->add(weight_slider); row->add(picker);
				}));
			}
		}));
		ADD_INLINE("Local contrast", MAKE("visuals>world>local contrast", checkbox, w.local_contrast), MAKE("visuals>world>local contrast>settings", settings, [](auto e) {
			auto &v = cfg.visuals.world_effects;
			ADD("Strength", "visuals>world>local contrast>strength", slider<float>, 0.f, 100.f, v.contrast_strength, "%.0f%%");
			ADD("Edge strength", "visuals>world>local contrast>edge", slider<float>, 0.f, 100.f, v.contrast_edge_strength, "%.0f%%");
			ADD("Vignette start", "visuals>world>local contrast>start", slider<float>, 0.f, 100.f, v.contrast_vignette_start, "%.0f%%");
			ADD("Vignette end", "visuals>world>local contrast>end", slider<float>, 0.f, 100.f, v.contrast_vignette_end, "%.0f%%");
			ADD("Vignette blur", "visuals>world>local contrast>blur", slider<float>, 0.f, 100.f, v.contrast_vignette_blur, "%.0f%%");
		}));
		ADD_INLINE("Color correction", MAKE("visuals>world>correction", checkbox, w.color_correction), MAKE("visuals>world>correction>settings", settings, [](auto e) {
			auto &v = cfg.visuals.world_effects;
			const auto curve = MAKE("visuals>world>correction>curve", combo_box, v.tone_curve);
			curve->add({MAKE("visuals>world>correction>curve>linear", selectable, "Linear", 0), MAKE("visuals>world>correction>curve>filmic", selectable, "Filmic", 1)});
			ADD_INLINE("Tone curve", curve);
			ADD("Contrast", "visuals>world>correction>contrast", slider<float>, -100.f, 100.f, v.correction_contrast, "%+.0f%%");
			ADD("Highlights", "visuals>world>correction>highlights", slider<float>, -100.f, 100.f, v.highlights, "%+.0f%%");
			ADD("Shadows", "visuals>world>correction>shadows", slider<float>, -100.f, 100.f, v.shadows, "%+.0f%%");
			ADD("Saturation", "visuals>world>correction>saturation", slider<float>, -100.f, 100.f, v.saturation, "%+.0f%%");
			ADD("Vibrance", "visuals>world>correction>vibrance", slider<float>, -100.f, 100.f, v.vibrance, "%+.0f%%");
			ADD("Temperature", "visuals>world>correction>temperature", slider<float>, -100.f, 100.f, v.temperature, "%+.0f%%");
			ADD("Tint", "visuals>world>correction>tint", slider<float>, -100.f, 100.f, v.tint, "%+.0f%%");
			ADD("Grayscale", "visuals>world>correction>grayscale", checkbox, v.grayscale);
		}));
		ADD_INLINE("Depth of field", MAKE("visuals>world>dof", checkbox, w.depth_of_field), MAKE("visuals>world>dof>settings", settings, [](auto e) {
			auto &v = cfg.visuals.world_effects;
			const auto mode = MAKE("visuals>world>dof>mode", combo_box, v.dof_mode);
			mode->add({MAKE("visuals>world>dof>mode>manual", selectable, "Manual", 0), MAKE("visuals>world>dof>mode>crosshair", selectable, "Crosshair", 1)});
			ADD_INLINE("Mode", mode);
			const auto quality = MAKE("visuals>world>dof>quality", combo_box, v.dof_quality);
			quality->add(MAKE("visuals>world>dof>quality>balanced", selectable, "Balanced", 0));
			ADD_INLINE("Quality", quality);
			const auto distance = make_control("Focus distance", MAKE("visuals>world>dof>distance", slider<float>, 0.f, 100.f, v.focus_distance, "%.0f%%"));
			const auto speed = make_control("Focus speed", MAKE("visuals>world>dof>speed", slider<float>, 0.f, 20.f, v.focus_speed, "%.0fx"));
			distance->is_visible = !v.dof_mode->test(1);
			speed->is_visible = v.dof_mode->test(1);
			mode->callback = [distance = std::weak_ptr<layout>(distance), speed = std::weak_ptr<layout>(speed)](bits &value) {
				if (const auto row = distance.lock()) row->set_visible(!value.test(1));
				if (const auto row = speed.lock()) row->set_visible(value.test(1));
			};
			e->add(distance); e->add(speed);
			ADD("Focus range", "visuals>world>dof>range", slider<float>, 0.f, 100.f, v.focus_range, "%.0f%%");
		}));
		{
			const auto weather = MAKE("visuals>world>weather", combo_box, w.weather);
			const auto none = MAKE("visuals>world>weather>none", selectable, "None", 0);
			const auto thunder = MAKE("visuals>world>weather>thunder", selectable, "Thunder", 1);
			const auto rain = MAKE("visuals>world>weather>rain", selectable, "Rain", 2);
			const auto snow = MAKE("visuals>world>weather>snow", selectable, "Snow", 3);
			thunder->swatch = MAKE("visuals>world>weather>thunder>color", color_picker, w.weather_colors[1]);
			rain->swatch = MAKE("visuals>world>weather>rain>color", color_picker, w.weather_colors[2]);
			snow->swatch = MAKE("visuals>world>weather>snow>color", color_picker, w.weather_colors[3]);
			weather->add({none, thunder, rain, snow});
			ADD_INLINE("Weather", weather);
		}
	}
}
