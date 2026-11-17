#include <gui/popups/hotkey_overview_popup.h>
#include <gui/gui.h>
#include <gui/controls/button.h>
#include <gui/controls/layout.h>

#define FIND_SAFE(v, n) const auto v = evo::gui::ctx->find(GUI_HASH(n)); if (!v) return;
#define FIND_SAFE_T(v, n, t) const auto v = evo::gui::ctx->find<t>(GUI_HASH(n)); if (!v) return;

#define MAKE_TAB(i, n) std::make_shared<tab>(evo::gui::control_id{GUI_HASH(i), ""}, XOR(n), GUI_HASH(i "_container"), draw.textures[GUI_HASH("icon_" i)])
#define MAKE(h, t, ...) std::make_shared<t>(evo::gui::control_id{GUI_HASH(h),XOR(h)}, __VA_ARGS__)
#define MAKE_RUNTIME(h, t, ...) std::make_shared<t>(evo::gui::control_id{hash((h).c_str()), h}, __VA_ARGS__)

#define ADD(n, h, t, ...) e->add(make_control(XOR(n), MAKE(h, t, __VA_ARGS__)))
#define ADD_RUNTIME(n, h, t, ...) e->add(make_control(XOR(n), MAKE_RUNTIME(h, t, __VA_ARGS__)))
#define ADD_INV(n, h, t, ...) e->add(make_control(XOR(n), MAKE(h, t, __VA_ARGS__))->make_invisible())
#define ADD_DEP(n, h, ht, t, ...) { const auto _ = MAKE(h, t, __VA_ARGS__); _->callback = [](bool v) { static const auto _ = ctx->find(GUI_HASH(ht))->parent.lock(); _->set_visible(v); }; e->add(make_control(XOR(n), _)); }
#define ADD_INLINE(l, ...) e->add(make_inlined(GUI_HASH(l "_content"), [&](std::shared_ptr<layout>& e) { \
	const std::vector<std::shared_ptr<evo::gui::control>> _ { __VA_ARGS__ };                                                           \
	e->add(std::make_shared<label>(control_id{hash(XOR(l)) + _.size()}, XOR(l)));                                                                                 \
	for (const auto& c : _)                                                                                                  \
		e->add(c);																										     \
}))

#define ADD_LINE(l, f, ...)                                                                                            \
	e->add(make_inlined(GUI_HASH(l "_content"),                                                                        \
						[&](std::shared_ptr<layout> &e)                                                                \
						{                                                                                              \
							const std::vector<std::shared_ptr<evo::gui::control>> _{__VA_ARGS__};                                \
							e->add(f);                                                                                 \
							for (const auto &c : _)                                                                    \
								e->add(c);                                                                             \
						}))


#define XOR(s) s

namespace gui::popups
{
	using namespace evo::gui;
	using namespace evo::ren;

	void hotkey_overview_popup::on_first_render_call()
	{
		pos = draw.display * 0.5f - size * 0.5f;

		popup::on_first_render_call();

		const auto close_btn = MAKE("hotkey_overview>close", button, XOR(""), draw.textures[GUI_HASH("icon_close")]);
		close_btn->pos = {size.x - 22.f, 2.f};
		close_btn->size = {16.f, 16.f};
		close_btn->callback = [&]()
		{
			close();
		};

		const auto list = MAKE("hotkey_overview>list", layout, vec2{ 0.f, 24.f }, vec2{ size.x - 5.f, size.y - 28.f });

		for (const auto &[_, c] : ctx->hotkey_list)
		{
			if (c.expired())
				continue;


		}

		add(close_btn);
		add(list);
	}

	void hotkey_overview_popup::render()
	{
		if (!is_visible)
			return;

		begin_render();

		const auto area = area_abs();
		const auto header = area_abs().height(24.f);
		const auto fnt = draw.fonts[GUI_HASH("gui_main")];
		const auto fnt_bold = draw.fonts[GUI_HASH("gui_bold")];

		auto &d = draw.layers[ctx->content_layer];
		d->add_shadow_rect(area, 12.f, true);
		d->g.anti_alias = true;
		d->add_rect_filled_rounded(area, colors.bg_block, 4.f);
		d->add_rect_filled_rounded(header, colors.bg_block_light, 4.f, rnd_t);
		d->add_rect_rounded(area, colors.outline, 4.f);
		d->g.anti_alias = false;

		d->add_line(header.bl(), header.br(), colors.outline);

		d->font = fnt_bold;
		d->add_text(header.tl() + vec2{8.f, 7.f}, XOR("Hotkey overview"), colors.accent);

		popup::render();
	}
}
