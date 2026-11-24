#pragma once
// Per-tab header for the Visuals tab. The tab is split into menu/tabs/visuals/{esp,chams,misc}/
// (one file per section); this declares the shared model-preview widget, the cross-section
// helpers and the section builders tabs.hpp does not already cover, so the per-section files
// can reference each other.
#include "../tabs.hpp"
#include <gui/controls/image.h>
#include <array>
#include <algorithm>
#include <cmath>
#include "../../config.hpp"
#include "../../preview/bridge.hpp"
#include "../../controls/macros.hpp"
#include "../../preview/esp_preview.hpp"
#include "../../../features/visuals/chams_passes.hpp"

using namespace evo::gui;
using namespace evo::gui::helpers;
using namespace evo::ren;

namespace menu::group
{
	class model_preview_viewport final : public control
	{
	public:
		model_preview_viewport(control_id id, const vec2 &size, bool skins = false, bool chams = false) : control(id, {}, size), skins_preview(skins), chams_preview(chams) { }

		void render() override
		{
			control::render();
			if (!is_visible) return;
			auto &layer = draw.layers[ctx->content_layer];
			FatalityPreviewBridge::render_ct.store(!skins_preview || FatalityPreviewBridge::skins_team_ct.load(std::memory_order_relaxed), std::memory_order_relaxed);
			FatalityPreviewBridge::render_skins.store(skins_preview, std::memory_order_relaxed);
			if (!skins_preview)
				FatalityPreviewBridge::visuals_subject.store(std::clamp(static_cast<int>(cfg.visuals.preview.get()),0,2), std::memory_order_relaxed);
			if (!skins_preview && size.y > 0.f)
                FatalityPreviewBridge::visuals_aspect.store(size.x / size.y, std::memory_order_relaxed);
			if (skins_preview && size.y > 0.f)
				FatalityPreviewBridge::skins_aspect.store(size.x / size.y, std::memory_order_relaxed);
			VisualPreview::Material appearance{};
			if(chams_preview) {
				const int subject=std::clamp(static_cast<int>(cfg.visuals.preview.get()),0,2);
				auto& chams=cfg.visuals.chams;
				auto& selected=subject==2 ? chams.local_pl : subject==1 ? chams.team_pl : chams.enemy_pl;
				appearance.enabled=chams.enable.get();appearance.kind=ChamsPasses::Selection(selected.mat.get().first_set_bit().value_or(0));
				appearance.ignore_z=selected.ignorez.get();appearance.primary=selected.col1.get().rgba();
				appearance.secondary=selected.col2.get().rgba();appearance.hidden=selected.col_z.get().rgba();
			}
			VisualPreview::SetMaterial(appearance);
			if(!is_locked_by_me())overlay.cancel();
			FatalityPreviewBridge::visible.store(true, std::memory_order_relaxed);
			if (auto texture = FatalityPreviewBridge::texture.load(std::memory_order_acquire)) {
				layer->g.texture = texture;
                if(chams_preview && cfg.visuals.esp.glow.enabled.get()) {
                    const int subject=std::clamp(static_cast<int>(cfg.visuals.preview.get()),0,2);
                    auto& glow=cfg.visuals.esp.glow;
                    const bool enabled=subject==0 ? glow.enemy.get() : subject==1 ? glow.team.get() : glow.local.get();
                    const auto c=subject==0 ? glow.enemy_color.get() : subject==1 ? glow.team_color.get() : glow.local_color.get();
                    if(enabled) {
                        const auto previous=layer->g.frag_shader;layer->g.frag_shader=draw.shaders[GUI_HASH("preview_silhouette")]->obj;
                        for(const auto& offset:std::array<vec2,8>{vec2{-2.f,0.f},{2.f,0.f},{0.f,-2.f},{0.f,2.f},{-2.f,-2.f},{2.f,-2.f},{-2.f,2.f},{2.f,2.f}})
                            layer->add_rect_filled(area_abs().translate(offset),c.mod_a(.32f));
                        layer->g.frag_shader=previous;
                    }
                }
				layer->add_rect_filled(area_abs(), color::white());
				layer->g.texture = {};
				if(!skins_preview && !chams_preview)overlay.render(layer,area_abs());
			} else {
				layer->font = draw.fonts[GUI_HASH("gui_main")];
				layer->add_text(area_abs().center(), "Model preview unavailable", colors.text_dark,
					text_params::with_vh(align_center, align_center));
			}
		}

		void on_mouse_down(char key) override
		{
			if(!is_mouse_on_me)return;
			if(!skins_preview && !chams_preview && overlay.down(key)) {
				if(key==m_left){lock_input();is_caring_about_hover=false;is_caring_about_parent=false;}
				return;
			}
			if (key == m_left) {lock_input();is_caring_about_hover=false;is_caring_about_parent=false;}
		}

		void on_mouse_up(char key) override
		{
			if (key == m_left && is_locked_by_me()) {overlay.up();unlock_input();is_caring_about_hover=true;is_caring_about_parent=true;}
		}

		void on_mouse_move(const vec2 &, const vec2 &delta) override
		{
			if (is_locked_by_me()) {
				if(!skins_preview && !chams_preview && overlay.move(delta,size))return;
				const auto yaw = FatalityPreviewBridge::yaw.load(std::memory_order_relaxed);
				FatalityPreviewBridge::yaw.store(std::remainder(yaw + delta.x * 0.5f, 360.f), std::memory_order_relaxed);
			}
		}

		void on_mouse_wheel(float) override
		{
			// Preview cameras have a fixed distance; the wheel must never zoom them.
			input.consume_wheel();
		}

	private:
		bool skins_preview{},chams_preview{};
		EspPreview::Overlay overlay;
	};

	constexpr int removal_ragdolls = 7;
    // Cross-section helpers + the two builders tabs.hpp does not declare (the rest are
    // already declared there; this header only adds what is missing so the files link up).
    std::shared_ptr<settings> preview_settings(const std::string &id);
    void insert_chams(const std::string &id, const std::shared_ptr<combo_box> &c);
    void update_ragdoll_chams_visibility(bool removed);
    void visuals_esp_world(std::shared_ptr<layout> &e, int category);
    void visuals_misc_world(std::shared_ptr<layout> &e);
}
