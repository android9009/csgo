#include "../../globals.hpp"
#include "menu.h"
#include "snow.h"

#include "../config_system.h"
#include "../config_vars.h"

#include <ShlObj.h>
#include <algorithm>
#include <map>
#include <d3d9.h>
#include <d3dx9.h>

#define add_texture_to_memory D3DXCreateTextureFromFileInMemory

std::vector< std::string > key_strings = { XOR("None"), XOR("M1"), XOR("M2"), XOR("Ctrl+brk"), XOR("M3"), XOR("M4"), XOR("M5"), XOR(" "), XOR("Back"), XOR("Tab"),
  XOR(" "), XOR(" "), XOR(" "), XOR("Enter"), XOR(" "), XOR(" "), XOR("Shift"), XOR("Ctrl"), XOR("Alt"), XOR("Pause"), XOR("Caps"), XOR(" "), XOR(" "),
  XOR(" "), XOR(" "), XOR(" "), XOR(" "), XOR("Esc"), XOR(" "), XOR(" "), XOR(" "), XOR(" "), XOR("Spacebar"), XOR("Pgup"), XOR("Pgdwn"), XOR("End"),
  XOR("Home"), XOR("Left"), XOR("Up"), XOR("Right"), XOR("Down"), XOR(" "), XOR("Print"), XOR(" "), XOR("Prtsc"), XOR("Insert"), XOR("Delete"), XOR(" "),
  XOR("0"), XOR("1"), XOR("2"), XOR("3"), XOR("4"), XOR("5"), XOR("6"), XOR("7"), XOR("8"), XOR("9"), XOR(" "), XOR(" "), XOR(" "), XOR(" "),
  XOR(" "), XOR(" "), XOR(" "), XOR("A"), XOR("B"), XOR("C"), XOR("D"), XOR("E"), XOR("F"), XOR("G"), XOR("H"), XOR("I"), XOR("J"), XOR("K"),
  XOR("L"), XOR("M"), XOR("N"), XOR("O"), XOR("P"), XOR("Q"), XOR("R"), XOR("S"), XOR("T"), XOR("U"), XOR("V"), XOR("W"), XOR("X"), XOR("Y"),
  XOR("Z"), XOR("Lw"), XOR("Rw"), XOR(" "), XOR(" "), XOR(" "), XOR("Num 0"), XOR("Num 1"), XOR("Num 2"), XOR("Num 3"), XOR("Num 4"), XOR("Num 5"),
  XOR("Num 6"), XOR("Num 7"), XOR("Num 8"), XOR("Num 9"), XOR("*"), XOR("+"), XOR("_"), XOR("-"), XOR("."), XOR("/"), XOR("F1"), XOR("F2"), XOR("F3"),
  XOR("F4"), XOR("F5"), XOR("F6"), XOR("F7"), XOR("F8"), XOR("F9"), XOR("F10"), XOR("F11"), XOR("F12"), XOR("F13"), XOR("F14"), XOR("F15"), XOR("F16"),
  XOR("F17"), XOR("F18"), XOR("F19"), XOR("F20"), XOR("F21"), XOR("F22"), XOR("F23"), XOR("F24"), XOR(" "), XOR(" "), XOR(" "), XOR(" "), XOR(" "),
  XOR(" "), XOR(" "), XOR(" "), XOR("Num lock"), XOR("Scroll lock"), XOR(" "), XOR(" "), XOR(" "), XOR(" "), XOR(" "), XOR(" "), XOR(" "), XOR(" "),
  XOR(" "), XOR(" "), XOR(" "), XOR(" "), XOR(" "), XOR(" "), XOR("Lshift"), XOR("Rshift"), XOR("Lcontrol"), XOR("Rcontrol"), XOR("Lmenu"), XOR("Rmenu"),
  XOR(" "), XOR(" "), XOR(" "), XOR(" "), XOR(" "), XOR(" "), XOR(" "), XOR(" "), XOR(" "), XOR(" "), XOR("Next track"), XOR("Previous track"),
  XOR("Stop"), XOR("Play/pause"), XOR(" "), XOR(" "), XOR(" "), XOR(" "), XOR(" "), XOR(" "), XOR(";"), XOR("+"), XOR("),"), XOR("-"), XOR("."),
  XOR("/?"), XOR("~"), XOR(" "), XOR(" "), XOR(" "), XOR(" "), XOR(" "), XOR(" "), XOR(" "), XOR(" "), XOR(" "), XOR(" "), XOR(" "), XOR(" "),
  XOR(" "), XOR(" "), XOR(" "), XOR(" "), XOR(" "), XOR(" "), XOR(" "), XOR(" "), XOR(" "), XOR(" "), XOR(" "), XOR(" "), XOR(" "), XOR(" "),
  XOR("[{"), XOR("\\|"), XOR("}]"), XOR("'\""), XOR(" "), XOR(" "), XOR(" "), XOR(" "), XOR(" "), XOR(" "), XOR(" "), XOR(" "), XOR(" "),
  XOR(" "), XOR(" "), XOR(" "), XOR(" "), XOR(" "), XOR(" "), XOR(" "), XOR(" "), XOR(" "), XOR(" "), XOR(" "), XOR(" "), XOR(" "), XOR(" "),
  XOR(" "), XOR(" "), XOR(" "), XOR(" "), XOR(" "), XOR(" "), XOR(" "), XOR(" "), XOR(" "), XOR(" ") };

std::array< std::string, max_tabs > tabs = { XOR("Rage"), XOR("Legit"), XOR("Anti-hit"), XOR("Visuals"), XOR("Misc"), XOR("Skins"), XOR("Configs") };

void c_menu::create_animation(float& mod, bool cond, float speed_multiplier, unsigned int animation_flags)
{
	float time = (RENDER->get_animation_speed() * 5.f) * speed_multiplier;

	if ((animation_flags & skip_enable) && cond)
		mod = 1.f;
	else if ((animation_flags & skip_disable) && !cond)
		mod = 0.f;

	if (animation_flags & lerp_animation)
		mod = std::lerp(mod, (float)cond, time);
	else
	{
		if (cond && mod <= 1.f)
			mod += time;
		else if (!cond && mod >= 0.f)
			mod -= time;
	}

	mod = std::clamp(mod, 0.f, 1.f);
}

void c_menu::update_alpha()
{
	this->create_animation(alpha, g_cfg.misc.menu, 0.3f);
}

float c_menu::get_alpha()
{
	return alpha;
}

void c_menu::set_window_pos(const ImVec2& pos)
{
	window_pos = pos;
}

ImVec2 c_menu::get_window_pos()
{
	return window_pos;
}

void c_menu::init_textures()
{
	if (!logo_texture)
		add_texture_to_memory(RENDER->get_device(), cheatLogo, sizeof(cheatLogo), &logo_texture);

	if (!keyboard_texture)
		add_texture_to_memory(RENDER->get_device(), keyboard_icon, sizeof(keyboard_icon), &keyboard_texture);

	if (!warning_texture)
		add_texture_to_memory(RENDER->get_device(), warning_icon, sizeof(warning_icon), &warning_texture);

	if (!spectator_texture)
		add_texture_to_memory(RENDER->get_device(), spectators_icon, sizeof(spectators_icon), &spectator_texture);

	if (!bomb_texture)
		add_texture_to_memory(RENDER->get_device(), bomb_indicator, sizeof(bomb_indicator), &bomb_texture);

	if (!icon_textures[0])
		add_texture_to_memory(RENDER->get_device(), rage_icon, sizeof(rage_icon), &icon_textures[0]);

	if (!icon_textures[1])
		add_texture_to_memory(RENDER->get_device(), legit_icon, sizeof(legit_icon), &icon_textures[1]);

	if (!icon_textures[2])
		add_texture_to_memory(RENDER->get_device(), antihit_icon, sizeof(antihit_icon), &icon_textures[2]);

	if (!icon_textures[3])
		add_texture_to_memory(RENDER->get_device(), visuals_icon, sizeof(visuals_icon), &icon_textures[3]);

	if (!icon_textures[4])
		add_texture_to_memory(RENDER->get_device(), misc_icon, sizeof(misc_icon), &icon_textures[4]);

	if (!icon_textures[5])
		add_texture_to_memory(RENDER->get_device(), skins_icon, sizeof(skins_icon), &icon_textures[5]);

	if (!icon_textures[6])
		add_texture_to_memory(RENDER->get_device(), cfg_icon, sizeof(cfg_icon), &icon_textures[6]);

	//MessageBoxA(0, g_cheat_info->user_avatar.c_str(), 0, 0);

	if (!avatar && !HACKS->cheat_info.user_avatar.empty() && HACKS->cheat_info.user_avatar.size())
		add_texture_to_memory(RENDER->get_device(), HACKS->cheat_info.user_avatar.data(), HACKS->cheat_info.user_avatar.size(), &avatar);
}

void c_menu::set_draw_list(ImDrawList* list)
{
	if (draw_list)
		return;

	draw_list = list;
}

ImDrawList* c_menu::get_draw_list()
{
	return draw_list;
}

namespace darkside_ui
{
	constexpr float window_w = 800.f;
	constexpr float window_h = 600.f;
	constexpr float content_w = 600.f;

	// Ported from Darkside's AddSquircleFilled/AddSquircle. The 1.5x
	// superellipse radius and 16 segments are intentional: ordinary rounded
	// rectangles do not have the same silhouette as the reference menu.
	void squircle_path(ImDrawList* draw, const ImVec2& min, const ImVec2& max, float radius)
	{
		float length = radius * 1.5f;
		length = std::min(length, std::min((max.x - min.x) * 0.5f, (max.y - min.y) * 0.5f));
		const ImVec2 corners[4] = {
			{ max.x - length, max.y - length },
			{ min.x + length, max.y - length },
			{ min.x + length, min.y + length },
			{ max.x - length, min.y + length }
		};
		for (int corner = 0; corner < 4; ++corner)
		{
			for (int i = 0; i <= 16; ++i)
			{
				const float angle = corner * IM_PI * 0.5f + (float)i / 16.f * IM_PI * 0.5f;
				const float ca = cosf(angle), sa = sinf(angle);
				const float cx = powf(fabsf(ca), 0.4f) * (ca > 0.f ? 1.f : -1.f);
				const float cy = powf(fabsf(sa), 0.4f) * (sa > 0.f ? 1.f : -1.f);
				draw->PathLineTo(corners[corner] + ImVec2(cx * length, cy * length));
			}
		}
	}

	void squircle_filled(ImDrawList* draw, const ImVec2& min, const ImVec2& max, ImU32 color, float radius)
	{
		if (radius <= 0.f || max.x <= min.x || max.y <= min.y)
		{
			draw->AddRectFilled(min, max, color);
			return;
		}
		draw->PathClear();
		squircle_path(draw, min, max, radius);
		draw->PathFillConvex(color);
	}

	void squircle(ImDrawList* draw, const ImVec2& min, const ImVec2& max, ImU32 color, float radius, float thickness = 1.f)
	{
		if (radius <= 0.f || max.x <= min.x || max.y <= min.y)
		{
			draw->AddRect(min, max, color, 0.f, 0, thickness);
			return;
		}
		draw->PathClear();
		squircle_path(draw, min, max, radius);
		draw->PathStroke(color, true, thickness);
	}
}

void c_menu::window_begin()
{
	ImGui::GetMousePos();
	static auto opened = true;
	const auto window_size = ImVec2(darkside_ui::window_w, darkside_ui::window_h);
	ImGui::SetNextWindowPos(ImVec2(RENDER->screen.x * 0.5f - window_size.x * 0.5f,
		RENDER->screen.y * 0.5f - window_size.y * 0.5f), ImGuiCond_Once);
	ImGui::SetNextWindowSize(window_size, ImGuiCond_Once);
	ImGui::PushFont(RENDER->fonts.main.get());
	ImGui::SetNextWindowBgAlpha(0.f);
	ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.f, 0.f, 0.f, 0.f));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.f, 0.f));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 6.f);
	ImGui::Begin(CXOR("##base_window"), &opened, ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoResize |
		ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoScrollbar);
	ImGui::PushItemWidth(276);
	set_draw_list(ImGui::GetWindowDrawList());
	set_window_pos(ImGui::GetWindowPos());
	auto list = get_draw_list();
	list->Flags |= ImDrawListFlags_AntiAliasedFill | ImDrawListFlags_AntiAliasedLines;
	const auto pos = get_window_pos();
	list->PushClipRect(pos, pos + window_size);
	ImGui::PushClipRect(pos, pos + window_size, false);
}

void c_menu::window_end()
{
	auto list = ImGui::GetWindowDrawList();
	set_draw_list(list);
	list->Flags &= ~(ImDrawListFlags_AntiAliasedFill | ImDrawListFlags_AntiAliasedLines);
	list->PopClipRect();
	ImGui::PopClipRect();
	ImGui::PopItemWidth();
	ImGui::End(false);
	ImGui::PopStyleVar(2);
	ImGui::PopStyleColor();
	ImGui::PopFont();
}

void c_menu::draw_ui_background()
{
	auto list = get_draw_list();
	const float a = get_alpha();
	const auto pos = get_window_pos();
	const auto max = pos + ImVec2(darkside_ui::window_w, darkside_ui::window_h);
	const auto accent = g_cfg.misc.ui_color.base();

	// Darkside geometry: 800x600, 170px side plate, 60px top plate,
	// content beginning at x=185 and a 150px-wide tab column.
	imgui_blur::create_blur(list, pos, max, c_color(11, 14, 22, 244.f * a).as_imcolor(), 6.f);
	list->AddRectFilled(pos, pos + ImVec2(170, 600), c_color(18, 20, 29, 216.f * a).as_imcolor(), 6.f, ImDrawCornerFlags_Left);
	list->AddRectFilled(pos + ImVec2(170, 0), pos + ImVec2(800, 61), c_color(11, 15, 23, 232.f * a).as_imcolor(), 6.f, ImDrawCornerFlags_Top);
	list->AddRectFilled(pos + ImVec2(170, 61), max, c_color(11, 14, 22, 226.f * a).as_imcolor(), 6.f, ImDrawCornerFlags_Bot);
	list->AddRect(pos, max, c_color(30, 33, 44, 255.f * a).as_imcolor(), 6.f);
	list->AddLine(pos + ImVec2(185, 60), pos + ImVec2(785, 60), c_color(30, 33, 44, 255.f * a).as_imcolor());
	list->AddLine(pos + ImVec2(10, 60), pos + ImVec2(160, 60), c_color(30, 33, 44, 255.f * a).as_imcolor());

	ImGui::PushFont(RENDER->fonts.menu_logo.get());
	list->AddText(pos + ImVec2(12, 15) + ImVec2(1, 1), accent.new_alpha(96.f * a).as_imcolor(), CXOR("AIRFLOW"));
	list->AddText(pos + ImVec2(12, 15), c_color(235, 235, 240, 255.f * a).as_imcolor(), CXOR("AIRFLOW"));
	ImGui::PopFont();

	const auto top_title = tabs[tab_selector].c_str();
	list->AddText(pos + ImVec2(185, 24), c_color(235, 235, 240, 255.f * a).as_imcolor(), top_title);
	const auto status = XOR("DARKSIDE UI  /  RECODE");
	const auto status_size = ImGui::CalcTextSize(status.c_str());
	list->AddText(pos + ImVec2(785 - status_size.x, 24), c_color(130, 133, 143, 230.f * a).as_imcolor(), status.c_str());

	// Bottom-left user plate from the reference (10, height-48)..(160, height-8).
	const auto profile_min = pos + ImVec2(10, 552);
	const auto profile_max = pos + ImVec2(160, 592);
	list->AddLine(profile_min, ImVec2(profile_max.x, profile_min.y), c_color(30, 33, 44, 255.f * a).as_imcolor());
	list->AddCircleFilled(profile_min + ImVec2(23, 20), 15.f, c_color(25, 28, 38, 255.f * a).as_imcolor(), 32);
	list->AddText(profile_min + ImVec2(46, 6), c_color(235, 235, 240, 255.f * a).as_imcolor(), CXOR("airflow"));
	list->AddText(profile_min + ImVec2(46, 22), c_color(130, 133, 143, 255.f * a).as_imcolor(), CXOR("recode"));
	list->AddText(profile_min + ImVec2(137, 14), c_color(130, 133, 143, 255.f * a).as_imcolor(), CXOR(">"));
}

void c_menu::draw_tabs()
{
	auto list = get_draw_list();
	const auto prev_pos = ImGui::GetCursorPos();
	const auto base = get_window_pos();
	const float a = get_alpha();
	const auto accent = g_cfg.misc.ui_color.base();
	ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
	ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0, 0, 0, 0));
	ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0, 0, 0, 0));
	ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 1.f, 1.f, a));

	for (int i = 0; i < tabs.size(); ++i)
	{
		auto& info = tab_info[i];
		const float y = 70.f + 34.f * i;
		ImGui::SetCursorPos(ImVec2(6, y));
		const auto id = CXOR("##tab_") + std::to_string(i);
		info.selected = ImGui::ButtonEx(id.c_str(), ImVec2(144, 30), 0, &info.hovered);
		if (info.selected)
			tab_selector = i;
		create_animation(info.hovered_alpha, info.hovered, 1.f, lerp_animation);
		create_animation(info.alpha, tab_selector == i, 0.8f, lerp_animation);

		const auto min = base + ImVec2(6, 70 + 34 * i);
		const auto max = min + ImVec2(148, 30);
		const float fill = std::clamp(info.alpha + info.hovered_alpha * 0.4f, 0.f, 1.f);
		if (fill > 0.01f)
			list->AddRectFilled(min, max, c_color(39, 43, 54, 255.f * fill * a).as_imcolor(), 6.f);
		const float text_mix = std::clamp(info.alpha + info.hovered_alpha, 0.f, 1.f);
		const int grey = (int)std::lerp(130.f, 235.f, text_mix);
		auto text = c_color(grey, grey, grey + 5, 255.f * a);
		if (icon_textures[i])
		{
			auto icon = info.alpha > 0.01f ? accent.new_alpha(255.f * a) : text;
			list->AddImage((void*)icon_textures[i], min + ImVec2(9, 8), min + ImVec2(23, 22), ImVec2(0, 0), ImVec2(1, 1), icon.as_imcolor());
		}
		list->AddText(min + ImVec2(32, 8), text.as_imcolor(), tabs[i].c_str());
	}
	ImGui::PopStyleColor(4);
	ImGui::SetCursorPos(prev_pos);
}

void c_menu::begin_group_box(const char* name)
{
	auto window = ImGui::GetCurrentWindow();
	if (!window || window->SkipItems)
		return;
	const auto pos = window->DC.CursorPos;
	const ImGuiID id = window->GetID(name);
	const float width = ImGui::GetContentRegionAvail().x;
	auto found = group_box_heights.find(id);
	const float height = found != group_box_heights.end() ? found->second : 80.f;
	group_box_stack.push_back({ id, width, true, draw_list });

	ImGui::BeginChild((std::string(name) + CXOR(".main")).c_str(), ImVec2(width, height), false, ImGuiWindowFlags_NoScrollbar);
	constexpr float header_h = 32.f;
	darkside_ui::squircle_filled(ImGui::GetWindowDrawList(), pos + ImVec2(0, header_h), pos + ImVec2(width, height),
		c_color(18, 20, 29, 255.f * get_alpha()).as_imcolor(), 20.f);
	darkside_ui::squircle(ImGui::GetWindowDrawList(), pos + ImVec2(0, header_h), pos + ImVec2(width, height),
		c_color(30, 33, 44, 255.f * get_alpha()).as_imcolor(), 20.f);
	ImGui::PushFont(RENDER->fonts.menu_header.get());
	ImGui::GetWindowDrawList()->AddText(pos + ImVec2(10, 9), c_color(235, 235, 240, 128.f * get_alpha()).as_imcolor(), name);
	ImGui::PopFont();
	ImGui::SetCursorPos(ImVec2(0, header_h));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12, 8));
	ImGui::BeginChild(name, ImVec2(width, height - header_h), false,
		ImGuiWindowFlags_AlwaysUseWindowPadding | ImGuiWindowFlags_NoScrollbar);
	set_draw_list(ImGui::GetWindowDrawList());
	ImGui::BeginGroup();
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 0));
}

void c_menu::end_group_box()
{
	if (group_box_stack.empty())
		return;
	const auto state = group_box_stack.back();
	group_box_stack.pop_back();
	const float content_h = ImGui::GetCursorPosY() + 8.f;
	group_box_heights[state.id] = content_h + 32.f;
	ImGui::PopStyleVar();
	ImGui::EndGroup();
	ImGui::EndChild(false);
	ImGui::EndChild(false);
	ImGui::PopStyleVar();
	draw_list = state.previous_draw_list;
}

void c_menu::draw_sub_tabs(int& selector, const std::vector< std::string >& tabs)
{
	set_draw_list(ImGui::GetWindowDrawList());
	auto& style = ImGui::GetStyle();
	const float a = get_alpha();
	const auto child_pos = get_window_pos() + ImVec2(185, 76);
	const auto prev_pos = ImGui::GetCursorPos();
	ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
	ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0, 0, 0, 0));
	ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0, 0, 0, 0));
	ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, a));
	const auto accent = g_cfg.misc.ui_color.base();
	const float step = std::min(94.f, 584.f / std::max<size_t>(1, tabs.size()));
	for (int i = 0; i < tabs.size(); ++i)
	{
		auto& info = subtab_info[tabs[0]][i];
		const auto cursor_pos = ImVec2(10 + step * i, 10);
		ImGui::SetCursorPos(cursor_pos);
		const auto tab_size = ImVec2(step - 6.f, 28.f);
		const auto id = CXOR("##sub_tab_") + tabs[i];
		info.selected = ImGui::ButtonEx(id.c_str(), tab_size, 0, &info.hovered);
		if (info.selected)
			selector = i;
		create_animation(info.hovered_alpha, info.hovered, 1.f, lerp_animation);
		create_animation(info.alpha, selector == i, 0.8f, lerp_animation);
		const auto min = child_pos + ImVec2(10 + step * i, 10);
		const ImRect bb(min, min + tab_size);
		const float fill = std::clamp(info.alpha + info.hovered_alpha * 0.5f, 0.f, 1.f);
		if (fill > 0.01f)
			draw_list->AddRectFilled(bb.Min, bb.Max, c_color(25, 28, 38, 255.f * fill * a).as_imcolor(), 6.f);
		if (selector == i)
			draw_list->AddLine(ImVec2(bb.Min.x + 12, bb.Max.y), ImVec2(bb.Max.x - 12, bb.Max.y), accent.new_alpha(255.f * a).as_imcolor(), 1.5f);
		const int grey = (int)std::lerp(130.f, 235.f, std::clamp(info.alpha + info.hovered_alpha, 0.f, 1.f));
		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(grey / 255.f, grey / 255.f, grey / 255.f, a));
		const auto label_size = ImGui::CalcTextSize(tabs[i].c_str(), NULL, true);
		ImGui::RenderTextClipped(bb.Min, bb.Max, tabs[i].c_str(), NULL, &label_size, style.ButtonTextAlign, &bb);
		ImGui::PopStyleColor();
	}
	ImGui::PopStyleColor(4);
	ImGui::SetCursorPos(prev_pos);
	ImGui::ItemSize(ImVec2(0, 48));
	ImGui::PushClipRect(child_pos + ImVec2(0, 48), child_pos + ImVec2(600, 509), false);
	draw_list->PushClipRect(child_pos + ImVec2(0, 48), child_pos + ImVec2(600, 509));
}

void c_menu::draw_snow()
{
	
}

void c_menu::draw()
{
	this->init_textures();

	if (RENDER->screen.x <= 0.f || RENDER->screen.y <= 0.f)
	{
		g_cfg.misc.watermark_position.x = 0.f;
		g_cfg.misc.watermark_position.y = 0.f;

		g_cfg.misc.keybind_position.x = 0.f;
		g_cfg.misc.keybind_position.y = 0.f;

		g_cfg.misc.bomb_position.x = 0.f;
		g_cfg.misc.bomb_position.y = 0.f;

		g_cfg.misc.spectators_position.x = 0.f;
		g_cfg.misc.spectators_position.y = 0.f;
		return;
	}

	this->update_alpha();
	this->draw_binds();
	this->draw_bomb_indicator();
	this->draw_watermark();
	this->draw_spectators();

	static auto reset = false;
	static auto set_ui_focus = false;

	if (!g_cfg.misc.menu && alpha <= 0.f)
	{
		if (reset)
		{
			for (auto& a : item_animations)
			{
				a.second.reset();
				tab_alpha = 0.f;
				subtab_alpha = 0.f;
				subtab_alpha2 = 0.f;
			}
			reset = false;
		}

		HACKS->loading_config = false;
		set_ui_focus = false;
		return;
	}

	reset = true;

	if (!set_ui_focus)
	{
		ImGui::SetNextWindowFocus();
		set_ui_focus = true;
	}

	ImGui::SetColorEditOptions(ImGuiColorEditFlags_PickerHueBar | ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoOptions | ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_DisplayRGB);

	//this->draw_snow( );
	this->window_begin();
	this->draw_ui_background();
	this->draw_tabs();
	this->draw_ui_items();
	this->window_end();

	HACKS->loading_config = false;
}