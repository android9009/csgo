#include "../menu.h"
#include "../../config_vars.h"
using namespace ImGui;

bool c_menu::button_wrapper(const char* label, const ImVec2& size_arg, ImGuiButtonFlags flags, bool* hovered_ptr, int button_flags)
{
	bool wait = button_flags & button_wait;
	bool danger = !(button_flags & button_danger);

	float alpha = this->get_alpha();
	float alpha_add = 0.5f + 0.5f * (!wait);

	ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 1.f * danger, 1.f * danger, alpha_add * alpha));

	ImGuiWindow* window = GetCurrentWindow();
	if (window->SkipItems)
	{
		ImGui::PopStyleColor();
		return false;
	}

	ImGuiContext& g = *GImGui;
	const ImGuiStyle& style = g.Style;
	const ImGuiID id = window->GetID(label);
	const ImVec2 label_size = CalcTextSize(label, NULL, true);

	ImVec2 pos = window->DC.CursorPos;
	ImVec2 size = size_arg;

	const ImRect bb(pos, pos + size + ImVec2(0.f, 4.f));
	ItemSize(bb, style.FramePadding.y);
	if (!ItemAdd(bb, id))
	{
		ImGui::PopStyleColor();
		return false;
	}

	ImRect button_rect = ImRect(pos, pos + size_arg);

	bool hovered, held;
	bool pressed = ButtonBehavior(button_rect, id, &hovered, &held, flags);

	auto& mod = item_animations[CONST_HASH(label)];
	this->create_animation(mod.hovered_alpha, hovered, 1.f, lerp_animation);

	const auto accent = g_cfg.misc.ui_color.base();
	const float blend = mod.hovered_alpha * 0.35f;
	const c_color back(
		(int)std::lerp(25.f, (float)accent.r(), blend),
		(int)std::lerp(27.f, (float)accent.g(), blend),
		(int)std::lerp(36.f, (float)accent.b(), blend),
		(int)(255.f * alpha));
	draw_list->AddRectFilled(button_rect.Min, button_rect.Max, back.as_imcolor(), 6.f);
	draw_list->AddRect(button_rect.Min, button_rect.Max, c_color(30, 33, 44, 255.f * alpha).as_imcolor(), 6.f);

	// button name
	RenderTextClipped(button_rect.Min, button_rect.Max, label, NULL, &label_size, style.ButtonTextAlign, &bb);

	if (hovered_ptr)
		*hovered_ptr = hovered;

	IMGUI_TEST_ENGINE_ITEM_INFO(id, label, window->DC.LastItemStatusFlags);

	ImGui::PopStyleColor();
	return pressed;
}

void c_menu::button(const char* label, void (*callback)(), int flags)
{
	auto button_ = this->button_wrapper(label, ImVec2(ImMax(80.f, GetContentRegionAvail().x), 28.f), 0, nullptr, flags);
	if (button_ && !(flags & button_wait))
		std::thread(callback).detach();
}