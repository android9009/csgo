#include "../menu.h"
#include "../../config_vars.h"

using namespace ImGui;

// ---------------------------------------------------------------------------
// Key binds for any menu control
//
// Every control (checkbox / multibox / slider) can own a bind. Binds are stored
// in g_cfg.item_binds keyed by the stripped control label, so adding a control
// never needs a new binds_idx_t entry. Resolution happens in
// c_key_states::update_item_binds().
// ---------------------------------------------------------------------------

// "Label##id" -> "Label"
static std::string bind_key_name(const char* label)
{
	std::string name = label != nullptr ? label : "";

	auto hash = name.find("##");
	if (hash != std::string::npos)
		name.erase(hash);

	return name;
}

key_binds_t& c_menu::item_bind(const char* label)
{
	auto name = bind_key_name(label);

	{
		const std::lock_guard<std::mutex> lock(item_binds_mutex);

		auto it = g_cfg.item_binds.find(name);
		if (it != g_cfg.item_binds.end())
			return it->second;

		auto& bind = g_cfg.item_binds[name];
		bind.reset();
		bind.name = name;
		return bind;
	}
}

bind_state_t c_menu::item_state(const char* label, bool enabled)
{
	return c_key_states::resolve(bind_key_name(label).c_str(), enabled);
}

// ---------------------------------------------------------------------------
// bind dropdown
// ---------------------------------------------------------------------------
void c_menu::open_item_bind(const char* label, ImVec2 anchor)
{
	item_bind_label = bind_key_name(label);
	item_bind_anchor = anchor;
	item_bind_open = true;
	item_bind_capturing = false;

	close_settings_popup();
}

void c_menu::close_item_bind()
{
	item_bind_open = false;
	item_bind_capturing = false;
}

void c_menu::draw_item_bind_popup()
{
	if (!item_bind_open)
		return;

	auto alpha = this->get_alpha();
	auto& bind = item_bind(item_bind_label.c_str());

	const ImVec2 pos = item_bind_anchor + ImVec2(0.f, 20.f);
	const ImVec2 size = ImVec2(148.f, item_bind_capturing ? 132.f : 124.f);

	item_bind_pos = pos;
	item_bind_size = size;

	ImGui::SetNextWindowPos(pos, ImGuiCond_Always);
	ImGui::SetNextWindowSize(size, ImGuiCond_Always);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6.f, 6.f));
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(2.f, 2.f));
	ImGui::PushStyleColor(ImGuiCol_WindowBg, ImColor(18, 18, 20, (int)(248 * alpha)).Value);
	ImGui::PushStyleColor(ImGuiCol_Border, ImColor(255, 255, 255, (int)(26 * alpha)).Value);

	// draw on top of the menu so the controls it covers do not swallow the clicks
	if (ImGui::GetCurrentContext()->Windows.Size > 0)
		ImGui::SetNextWindowFocus();

	ImGui::Begin(CXOR("##item_bind_popup"), nullptr,
		ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove
		| ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoNav);

	item_bind_pos = ImGui::GetWindowPos();
	item_bind_size = ImGui::GetWindowSize();

	// The control itself is the on/off switch (a checked checkbox is active), the
	// bind only adds a key on top of it: hold while pressed or toggle on press.
	auto mode_row = [&](const char* text, int type)
	{
		if (this->selectable(text, bind.type == type, alpha))
		{
			bind.type = type;

			// a fresh bind waits for its key right away
			if (type == 2 && bind.key == -1)
				item_bind_capturing = true;
		}
	};

	mode_row(CXOR("Hold"), 1);
	mode_row(CXOR("Toggle"), 2);

	// key capture: click the row, press a key
	std::string key_text = CXOR("Key: ");

	if (item_bind_capturing)
		key_text += CXOR("...");
	else if (bind.key == -1)
		key_text += CXOR("None");
	else if (bind.key >= 0 && bind.key < (int)key_strings.size())
		key_text += key_strings[bind.key];
	else
		key_text += CXOR("None");

	if (this->selectable(key_text.c_str(), item_bind_capturing, alpha))
		item_bind_capturing = !item_bind_capturing;

	if (bind.type == 1 || bind.type == 2 || bind.key != -1)
	{
		if (this->selectable(CXOR("Clear"), false, alpha))
		{
			bind.reset2();
			bind.type = -1;
			bind.key = -1;
			item_bind_capturing = false;
		}
	}

	if (item_bind_capturing)
	{
		auto& io = ImGui::GetIO();
		int captured = -1;

		static const int mouse_keys[5] = { VK_LBUTTON, VK_RBUTTON, VK_MBUTTON, VK_XBUTTON1, VK_XBUTTON2 };

		for (int i = 0; i < 5; i++)
		{
			if (io.MouseDown[i])
				captured = mouse_keys[i];
		}

		if (captured == -1)
		{
			for (int i = VK_BACK; i <= VK_RMENU; i++)
			{
				if (io.KeysDown[i])
				{
					captured = i;
					break;
				}
			}
		}

		if (ImGui::IsKeyPressedMap(ImGuiKey_Escape))
		{
			bind.key = -1;
			item_bind_capturing = false;
		}
		else if (captured != -1)
		{
			bind.key = captured;

			if (bind.type == -1)
				bind.type = 1;

			item_bind_capturing = false;
		}
	}

	ImGui::End(false);
	ImGui::PopStyleColor(2);
	ImGui::PopStyleVar(2);

	const auto mouse = ImGui::GetIO().MousePos;
	const bool over = mouse.x >= item_bind_pos.x && mouse.y >= item_bind_pos.y
		&& mouse.x <= item_bind_pos.x + item_bind_size.x && mouse.y <= item_bind_pos.y + item_bind_size.y;

	if (ImGui::GetIO().MouseClicked[0] && !over && !item_bind_capturing)
		item_bind_open = false;
}

// ---------------------------------------------------------------------------
// settings flyout (the gear)
// ---------------------------------------------------------------------------
void c_menu::open_settings_popup(int popup_id, ImVec2 anchor)
{
	settings_popup_id = popup_id;
	settings_popup_anchor = anchor;
	settings_popup_open = true;

	close_item_bind();
}

void c_menu::close_settings_popup()
{
	settings_popup_open = false;
	settings_popup_id = 0;
}

bool c_menu::settings_popup_hovered() const
{
	if (!settings_popup_open)
		return false;

	auto mouse = ImGui::GetIO().MousePos;
	return mouse.x >= settings_popup_pos.x && mouse.y >= settings_popup_pos.y
		&& mouse.x <= settings_popup_pos.x + settings_popup_size.x
		&& mouse.y <= settings_popup_pos.y + settings_popup_size.y;
}

void c_menu::settings_popup_begin(const char* id, float width, float height)
{
	auto alpha = this->get_alpha();

	// the menu is drawn inside a 720 x 520 clip rect
	const ImVec2 window_pos = this->get_window_pos();
	const ImVec2 menu_max = window_pos + ImVec2(720.f, 520.f);

	ImVec2 pos = settings_popup_anchor + ImVec2(0.f, 20.f);

	if (pos.y + height > menu_max.y)
		pos.y = settings_popup_anchor.y - height - 6.f;

	if (pos.x + width > menu_max.x)
		pos.x = menu_max.x - width - 6.f;

	if (pos.x < window_pos.x)
		pos.x = window_pos.x + 6.f;

	settings_popup_pos = pos;
	settings_popup_size = ImVec2(width, height);

	ImGui::SetNextWindowPos(pos, ImGuiCond_Always);
	ImGui::SetNextWindowSize(settings_popup_size, ImGuiCond_Always);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.f, 8.f));
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.f, 4.f));
	ImGui::PushStyleColor(ImGuiCol_WindowBg, ImColor(18, 18, 20, (int)(248 * alpha)).Value);
	ImGui::PushStyleColor(ImGuiCol_Border, ImColor(255, 255, 255, (int)(26 * alpha)).Value);

	ImGui::Begin(id, nullptr,
		ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove
		| ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoNav);
}

void c_menu::settings_popup_end()
{
	ImGui::End(false);
	ImGui::PopStyleColor(2);
	ImGui::PopStyleVar(2);
}

bool c_menu::settings_checkbox(const char* label, bool* v)
{
	ImGui::PushID(label);
	const bool ret = this->checkbox(label, v);
	ImGui::PopID();

	return ret;
}

void c_menu::settings_slider(const char* label, int* v, int v_min, int v_max, const char* format)
{
	ImGui::PushID(label);
	this->slider_int(label, v, v_min, v_max, format != nullptr ? format : CXOR("%d"));
	ImGui::PopID();
}

bool c_menu::settings_selectable(const char* label, bool selected)
{
	return this->selectable(label, selected, this->get_alpha());
}

// ---------------------------------------------------------------------------
// widgets that own a bind (right click) and optionally a gear (left click)
// ---------------------------------------------------------------------------
void c_menu::draw_gear(const char* label, float x, float y, int popup_id)
{
	auto alpha = this->get_alpha();
	auto list = ImGui::GetWindowDrawList();

	const ImVec2 min = ImVec2(x, y);
	const ImVec2 center = min + ImVec2(6.5f, 8.f);

	// the owning control spans the whole 256px row, so drive the button through
	// ButtonBehavior directly to make the gear win the hover test on top of it
	ImGuiWindow* window = ImGui::GetCurrentWindow();
	const ImGuiID id = window->GetID((void*)(intptr_t)(CONST_HASH(label) + 0x51ED));

	bool hovered = false, held = false;
	const bool pressed = ImGui::ButtonBehavior(ImRect(min, min + ImVec2(15.f, 16.f)), id, &hovered, &held);

	gear_rect = ImRect(min, min + ImVec2(15.f, 16.f));

	c_color tint = c_color(120, 120, 120).new_alpha(255.f * alpha);

	if (hovered)
		tint = c_color(235, 235, 235).new_alpha(255.f * alpha);

	// gear: ring + hub + four teeth
	list->AddCircle(center, 5.2f, tint.as_imcolor(), 16, 1.2f);
	list->AddCircleFilled(center, 1.7f, tint.as_imcolor(), 8);

	for (int i = 0; i < 4; ++i)
	{
		const float a = (float)i * (3.14159265f / 2.f) + 0.785f;
		const ImVec2 dir = ImVec2(cosf(a), sinf(a));
		list->AddLine(center + dir * 3.8f, center + dir * 6.8f, tint.as_imcolor(), 1.4f);
	}

	if (pressed)
		this->open_settings_popup(popup_id, min);
}

// right click on the row the mouse is over opens the bind dropdown of that control
void c_menu::bind_right_click(const char* label, const ImVec2& min, float height)
{
	if (ImGui::IsMouseHoveringRect(min, min + ImVec2(256.f, height)) && ImGui::GetIO().MouseClicked[1])
		this->open_item_bind(label, min);
}

bool c_menu::bind_checkbox(const char* scope, const char* label, bool* v, int popup_id)
{
	ImGui::PushID(scope);

	ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 1.f, 1.f, 0.85f * this->get_alpha()));
	const bool changed = this->checkbox(label, v);
	ImGui::PopStyleColor();

	const ImVec2 min = ImGui::GetItemRectMin();

	this->bind_right_click(label, min, 32.f);

	if (popup_id != 0)
	{
		// label ... toggle at the far right, the gear follows right after the toggle
		this->draw_gear(label, min.x + 251.f, min.y + 9.f, popup_id);
	}

	ImGui::PopID();

	return changed;
}

void c_menu::bind_multi_combo(const char* scope, const char* label, unsigned int& var, std::vector< std::string > elements, int popup_id)
{
	ImGui::PushID(scope);

	this->multi_combo(label, var, elements);

	const ImVec2 min = ImGui::GetItemRectMin();

	this->bind_right_click(label, min, 48.f);

	if (popup_id != 0)
	{
		// label ... combo preview / arrow on the right, the gear follows it
		this->draw_gear(label, min.x + 251.f, min.y + 16.f, popup_id);
	}

	ImGui::PopID();
}

void c_menu::bind_combo(const char* scope, const char* label, int* var, const char* const items[], int items_count, int height)
{
	ImGui::PushID(scope);
	this->combo(label, var, items, items_count, height);

	const ImVec2 min = ImGui::GetItemRectMin();

	this->bind_right_click(label, min, 32.f);

	ImGui::PopID();
}

void c_menu::bind_combo(const char* scope, const char* label, int& var, std::vector< std::string > elements, int popup_id)
{
	ImGui::PushID(scope);

	this->multi_combo_exclusive(label, var, elements);

	const ImVec2 min = ImGui::GetItemRectMin();

	this->bind_right_click(label, min, 48.f);

	if (popup_id != 0)
	{
		this->draw_gear(label, min.x + 251.f, min.y + 16.f, popup_id);
	}

	ImGui::PopID();
}

void c_menu::bind_slider(const char* scope, const char* label, int* v, int v_min, int v_max, const char* format)
{
	ImGui::PushID(scope);

	// the slider label carries the id, keep it untouched so the animation keys and
	// the bind name stay stable
	this->slider_int(label, v, v_min, v_max, format);

	const ImVec2 min = ImGui::GetItemRectMin();

	this->bind_right_click(label, min, 40.f);

	ImGui::PopID();
}

// ---------------------------------------------------------------------------
// per frame: both flyouts are drawn once from c_menu::draw()
// ---------------------------------------------------------------------------
void c_menu::draw_menu_popups()
{
	this->draw_settings_body();

	this->draw_item_bind_popup();

	if (!settings_popup_open)
		return;

	const auto mouse = ImGui::GetIO().MousePos;
	const bool over_gear = gear_rect.Contains(mouse);

	if (ImGui::GetIO().MouseClicked[0] && !settings_popup_hovered() && !over_gear)
		settings_popup_open = false;
}

// settings flyouts opened by the gears of the rage tab
void c_menu::draw_settings_body()
{
	if (!settings_popup_open)
		return;

	switch (settings_popup_id)
	{
	case settings_main:
	{
		settings_popup_begin(CXOR("##settings_main"), 200.f, 132.f);

		settings_checkbox(CXOR("Auto fire"), &g_cfg.rage.auto_fire);
		settings_checkbox(CXOR("Anti aim correction"), &g_cfg.rage.resolver);
		settings_checkbox(CXOR("Dormant aimbot"), &g_cfg.rage.dormant_aim);

		settings_popup_end();
	}
	break;
	case settings_double_tap:
	{
		settings_popup_begin(CXOR("##settings_double_tap"), 200.f, 214.f);

		auto alpha = this->get_alpha();

		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 1.f, 1.f, 0.5f * alpha));
		ImGui::Text(CXOR("Defensive mode"));
		ImGui::PopStyleColor();

		if (this->selectable(CXOR("On peek"), g_cfg.rage.dt_mode == 1, alpha))
			g_cfg.rage.dt_mode = 1;

		if (this->selectable(CXOR("Always"), g_cfg.rage.dt_mode == 2, alpha))
			g_cfg.rage.dt_mode = 2;

		settings_slider(CXOR("Double tap speed"), &g_cfg.rage.dt_speed, 0, 100, CXOR("%d%%"));

		settings_popup_end();
	}
	break;
	case settings_multipoint:
	{
		settings_popup_begin(CXOR("##settings_multipoint"), 200.f, 164.f);

		auto& weapon = g_cfg.rage.weapon[g_cfg.rage.group_type];

		settings_slider(CXOR("Head point"), &weapon.head_point, 0, 100, CXOR("%d%%"));
		settings_slider(CXOR("Body point"), &weapon.body_point, 0, 100, CXOR("%d%%"));

		settings_popup_end();
	}
	break;
	}
}
