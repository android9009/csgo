#include "../menu.h"
#include "../../config_vars.h"

using namespace ImGui;

bool c_menu::checkbox(const char* label, bool* v)
{
	const float alpha = get_alpha();
	ImGuiWindow* window = GetCurrentWindow();
	if (window->SkipItems)
		return false;
	ImGuiContext& g = *GImGui;
	const ImGuiStyle& style = g.Style;
	const ImGuiID id = window->GetID(label);
	const ImVec2 label_size = CalcTextSize(label, NULL, true);
	const float w = ImMax(80.f, GetContentRegionAvail().x);
	const float row_h = ImMax(19.f, label_size.y) + style.FramePadding.y * 2.f;
	const ImVec2 pos = window->DC.CursorPos;
	const ImVec2 switch_size(30.f, 19.f);
	const ImRect frame_bb(pos + ImVec2(w - switch_size.x, (row_h - switch_size.y) * 0.5f),
		pos + ImVec2(w, (row_h + switch_size.y) * 0.5f));
	const ImRect total_bb(pos, pos + ImVec2(w, row_h));
	ItemSize(total_bb, style.FramePadding.y);
	if (!ItemAdd(total_bb, id))
		return false;
	bool hovered, held;
	const bool pressed = ButtonBehavior(total_bb, id, &hovered, &held);
	if (pressed)
	{
		*v = !*v;
		MarkItemEdited(id);
	}
	auto& mod = item_animations[CONST_HASH(label)];
	create_animation(mod.hovered_alpha, hovered, 1.f, lerp_animation);
	create_animation(mod.alpha, *v, 0.8f, lerp_animation);
	const auto accent = g_cfg.misc.ui_color.base();
	const auto off = c_color(14, 18, 25, 255.f * alpha);
	const auto body = c_color(
		(std::uint8_t)std::lerp((float)off.r(), (float)accent.r(), mod.alpha),
		(std::uint8_t)std::lerp((float)off.g(), (float)accent.g(), mod.alpha),
		(std::uint8_t)std::lerp((float)off.b(), (float)accent.b(), mod.alpha),
		(std::uint8_t)(255.f * alpha));
	draw_list->AddRectFilled(frame_bb.Min, frame_bb.Max, body.as_imcolor(), 9.5f);
	draw_list->AddRect(frame_bb.Min + ImVec2(0.5f, 0.5f), frame_bb.Max - ImVec2(0.5f, 0.5f),
		c_color(255, 255, 255, 18.f * (1.f - mod.alpha) * alpha).as_imcolor(), 9.f);
	const float knob_x = std::lerp(frame_bb.Min.x + 9.5f, frame_bb.Max.x - 9.5f, mod.alpha);
	const auto knob = c_color(
		(std::uint8_t)std::lerp(127.f, 255.f, mod.alpha),
		(std::uint8_t)std::lerp(135.f, 255.f, mod.alpha),
		(std::uint8_t)std::lerp(142.f, 255.f, mod.alpha),
		(std::uint8_t)(255.f * alpha));
	draw_list->AddCircleFilled(ImVec2(knob_x, frame_bb.GetCenter().y), 7.5f, knob.as_imcolor(), 32);
	const float text_active = ImMax(mod.alpha, mod.hovered_alpha);
	const int text_grey = (int)std::lerp(150.f, 255.f, text_active);
	if (label_size.x > 0.f)
	{
		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(text_grey / 255.f, text_grey / 255.f, text_grey / 255.f, alpha));
		RenderTextClipped(total_bb.Min, ImVec2(frame_bb.Min.x - 8.f, total_bb.Max.y), label, NULL, &label_size, ImVec2(0.f, 0.5f));
		ImGui::PopStyleColor();
	}
	draw_list->AddLine(ImVec2(pos.x, total_bb.Max.y), ImVec2(pos.x + w, total_bb.Max.y),
		c_color(255, 255, 255, 12.f * alpha).as_imcolor());
	IMGUI_TEST_ENGINE_ITEM_INFO(id, label, window->DC.ItemFlags | ImGuiItemStatusFlags_Checkable | (*v ? ImGuiItemStatusFlags_Checked : 0));
	return pressed;
}

bool c_menu::checkbox_columns(const char* label, unsigned int& v, const std::vector< std::string >& conditions)
{
	float alpha = this->get_alpha();
	ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 1.f, 1.f, 0.5f * alpha));

	ImGuiWindow* window = GetCurrentWindow();
	if (window->SkipItems)
		return false;

	ImGuiContext& g = *GImGui;
	const ImGuiStyle& style = g.Style;
	const ImGuiID id = window->GetID(label);
	const ImVec2 label_size = CalcTextSize(label, NULL, true);

	int cond_size = conditions.size();
	auto back_size = ImVec2(256, (43.75f * cond_size) + 13.f);
	const ImVec2 pos = window->DC.CursorPos;
	const ImRect total_bb(pos, pos + back_size);
	ItemSize(total_bb, style.FramePadding.y);
	if (!ItemAdd(total_bb, id))
		return false;

	auto& mod = item_animations[CONST_HASH(label)];

	// background
	c_color back_clr = c_color(217, 217, 217).new_alpha(20 * alpha);
	draw_list->AddRectFilled(pos, pos + back_size, back_clr.as_imcolor(), 4.f);

	PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 1.f, 1.f, 1.f * alpha));
	if (label_size.x > 0.0f)
		RenderText(ImVec2(pos.x + 12, pos.y + 8), label);
	PopStyleColor();

	auto square_sz = 16.f;
	const float pad = ImMax(1.0f, IM_FLOOR(square_sz / 6.0f));

	auto clr = g_cfg.misc.ui_color.base();

	// separator
	auto separator_pos = pos + ImVec2(0, 31);
	draw_list->AddLine(separator_pos, separator_pos + ImVec2(256, 0), c_color(0, 0, 0, 80 * alpha).as_imcolor());

	// checkboxes for changing condition
	auto checkboxes_pos = separator_pos + ImVec2(0, 17);
	for (int i = 0; i < cond_size; ++i)
	{
		// checkbox name
		RenderText(ImVec2(checkboxes_pos.x + 12, checkboxes_pos.y + 32 * i), conditions[i].c_str());

		// get checkbox bounds for render, etc.
		auto checkbox_bounds = [&](int idx)
		{
			ImVec2 checkbox_size = ImVec2(16, 16);
			ImVec2 checkbox_base = checkboxes_pos + ImVec2(200 + (24 * idx), (checkbox_size.y * i) * 2);
			ImRect checkbox_bounds = ImRect(checkbox_base, checkbox_base + checkbox_size);

			return checkbox_bounds;
		};

		// hover and press logic
		auto checkbox_active = [&](int idx) -> std::pair< bool, bool >
		{
			const auto id_str = conditions[i] + std::to_string(idx);
			const ImGuiID check_id = window->GetID(id_str.c_str());

			bool hovered, held;
			bool pressed = ButtonBehavior(checkbox_bounds(idx), check_id, &hovered, &held);

			return std::make_pair(pressed, hovered);
		};

		auto render_checkbox = [&](float hovered, float active, int idx)
		{
			auto bounds = checkbox_bounds(idx);

			// background
			auto checkbox_alpha = (80 + (30 * hovered)) * alpha;
			c_color checkbox_back_clr = c_color(0, 0, 0).increase(50 * hovered).new_alpha(checkbox_alpha);

			draw_list->AddRectFilled(bounds.Min, bounds.Max, checkbox_back_clr.as_imcolor(), 3.f);

			// active body
			auto checkbox_active_clr = c_color().multiply(clr, active).new_alpha(255 * alpha * active);
			draw_list->AddRectFilled(bounds.Min, bounds.Max, checkbox_active_clr.as_imcolor(), 3.f);

			// checkmark
			auto checkmark_clr = c_color().new_alpha(255 * alpha * active);
			draw_list->AddLine(ImVec2(bounds.Min.x + 3.f, bounds.Min.y + 8.f), ImVec2(bounds.Min.x + 6.f, bounds.Min.y + 11.f), checkmark_clr.as_imcolor(), 1.5f);

			draw_list->AddLine(ImVec2(bounds.Min.x + 6.f, bounds.Min.y + 11.f), ImVec2(bounds.Min.x + 12.f, bounds.Min.y + 4.f), checkmark_clr.as_imcolor(), 1.5f);
		};

		auto left_active = checkbox_active(0);
		auto& left_mod = item_animations[CONST_HASH(std::string(conditions[i] + XOR("0")).c_str())];

		auto right_active = checkbox_active(1);
		auto& right_mod = item_animations[CONST_HASH(std::string(conditions[i] + XOR("0")).c_str())];

		// states saves from right to left
		// like 1 << (0 << 2) will be here:
		// 0000 0001
		// and 1 << (1 << 2) + 1 will be here
		// 0000 0010
		// credits: ekzi for explaining about it

		int left_flag = 1 << (i << 2);
		int right_flag = 1 << ((i << 2) + 1);

		if (left_active.first)
		{
			if (!(v & left_flag))
				v |= left_flag;
			else
				v &= ~left_flag;

			v &= ~right_flag;
		}
		else if (right_active.first)
		{
			if (!(v & right_flag))
				v |= right_flag;
			else
				v &= ~right_flag;

			v &= ~left_flag;
		}

		// checkbox left
		{
			this->create_animation(left_mod.hovered_alpha, left_active.second, 1.f, lerp_animation);
			this->create_animation(left_mod.alpha, v & left_flag, 0.8f, lerp_animation);
			render_checkbox(left_mod.hovered_alpha, left_mod.alpha, 0);
		}

		// checkbox right
		{
			this->create_animation(right_mod.hovered_alpha, right_active.second, 1.f, lerp_animation);
			this->create_animation(right_mod.alpha, v & right_flag, 0.8f, lerp_animation);
			render_checkbox(right_mod.hovered_alpha, right_mod.alpha, 1);
		}
	}
	return true;
}