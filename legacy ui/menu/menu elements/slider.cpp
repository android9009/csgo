#include "../menu.h"
#include "../../config_vars.h"

using namespace ImGui;

static const char* patch_formant_string_float_to_int(const char* fmt)
{
	if (fmt[0] == '%' && fmt[1] == '.' && fmt[2] == '0' && fmt[3] == 'f' && fmt[4] == 0) // Fast legacy path for "%.0f" which is expected to be the most common case.
		return "%d";
	const char* fmt_start = ImParseFormatFindStart(fmt);   // Find % (if any, and ignore %%)
	const char* fmt_end = ImParseFormatFindEnd(fmt_start); // Find end of format specifier, which itself is an exercise of confidence/recklessness (because snprintf is dependent on libc or user).
	if (fmt_end > fmt_start && fmt_end[-1] == 'f')
	{
		if (fmt_start == fmt && fmt_end[0] == 0)
			return CXOR("%d");
		ImGuiContext& g = *GImGui;
		ImFormatString(g.TempBuffer, IM_ARRAYSIZE(g.TempBuffer), CXOR("%.*s%%d%s"), (int)(fmt_start - fmt), fmt, fmt_end); // Honor leading and trailing decorations, but lose alignment/precision.
		return g.TempBuffer;
	}
	return fmt;
}

bool c_menu::slider_scalar(const char* label, ImGuiDataType data_type, void* p_data, const void* p_min, const void* p_max, const char* format, float power)
{
	const float alpha = get_alpha();
	IM_UNUSED(power);
	ImGuiWindow* window = GetCurrentWindow();
	if (window->SkipItems)
		return false;
	ImGuiContext& g = *GImGui;
	const ImGuiStyle& style = g.Style;
	const ImGuiID id = window->GetID(label);
	const ImVec2 pos = window->DC.CursorPos;
	const float w = ImMax(120.f, GetContentRegionAvail().x);
	const ImVec2 label_size = CalcTextSize(label, NULL, true);
	const float row_h = ImMax(label_size.y, 19.f) + style.FramePadding.y * 2.f;
	const float track_right = w - 50.f;
	float track_left = ImMax(ImMin(82.f, w * 0.45f), track_right - 74.f);
	if (track_right < track_left + 36.f)
		track_left = ImMax(0.f, track_right - 36.f);
	const ImRect frame_bb(pos + ImVec2(track_left, (row_h - 4.f) * 0.5f), pos + ImVec2(track_right, (row_h + 4.f) * 0.5f));
	const ImRect total_bb(pos, pos + ImVec2(w, row_h));
	ItemSize(total_bb, style.FramePadding.y);
	if (!ItemAdd(total_bb, id))
		return false;
	if (!format)
		format = DataTypeGetInfo(data_type)->PrintFmt;
	else if (data_type == ImGuiDataType_S32 && strcmp(format, CXOR("%d")) != 0)
		format = patch_formant_string_float_to_int(format);

	auto& mod = item_animations[CONST_HASH(label)];
	const bool row_hovered = IsMouseHoveringRect(total_bb.Min, total_bb.Max);
	create_animation(mod.hovered_alpha, row_hovered, 1.f, lerp_animation);
	const ImRect hit_bb(frame_bb.Min - ImVec2(4, 7), frame_bb.Max + ImVec2(4, 7));
	const bool hovered = ItemHoverable(hit_bb, id);
	if (hovered && g.IO.MouseClicked[0])
	{
		SetActiveID(id, window);
		SetFocusID(id, window);
		FocusWindow(window);
	}
	bool value_changed = false;
	if (g.ActiveId == id)
	{
		if (g.IO.MouseDown[0])
		{
			const float t = std::clamp((g.IO.MousePos.x - frame_bb.Min.x) / frame_bb.GetWidth(), 0.f, 1.f);
			if (data_type == ImGuiDataType_S32)
			{
				const int min = *(const int*)p_min, max = *(const int*)p_max;
				const int next = (int)std::lround(std::lerp((float)min, (float)max, t));
				if (*(int*)p_data != next)
				{
					*(int*)p_data = next;
					value_changed = true;
				}
			}
		}
		else
			ClearActiveID();
	}
	if (g.IO.MouseWheel != 0.f && row_hovered && g.HoveredWindow == window && (!g.ActiveId || g.ActiveId == id) && data_type == ImGuiDataType_S32)
	{
		const int min = *(const int*)p_min, max = *(const int*)p_max;
		const int next = std::clamp(*(int*)p_data + (g.IO.MouseWheel > 0.f ? 1 : -1), min, max);
		value_changed |= next != *(int*)p_data;
		*(int*)p_data = next;
		g.IO.MouseWheel = 0.f;
	}
	if (value_changed)
		MarkItemEdited(id);

	float norm = 0.f;
	if (data_type == ImGuiDataType_S32)
	{
		const int min = *(const int*)p_min, max = *(const int*)p_max;
		if (max > min)
			norm = std::clamp((float)(*(int*)p_data - min) / (float)(max - min), 0.f, 1.f);
	}
	const float target_fill = norm * frame_bb.GetWidth();
	mod.alpha = std::lerp(mod.alpha, target_fill, std::clamp(g.IO.DeltaTime * 15.f, 0.f, 1.f));
	const auto accent = g_cfg.misc.ui_color.base();
	draw_list->AddRectFilled(frame_bb.Min, frame_bb.Max, c_color(25, 27, 36, 255.f * alpha).as_imcolor(), 5.f);
	const float fill_x = std::clamp(frame_bb.Min.x + mod.alpha, frame_bb.Min.x, frame_bb.Max.x);
	if (fill_x > frame_bb.Min.x + 0.1f)
		draw_list->AddRectFilled(frame_bb.Min, ImVec2(fill_x, frame_bb.Max.y), accent.new_alpha(255.f * alpha).as_imcolor(), 5.f);
	draw_list->AddCircleFilled(ImVec2(fill_x, frame_bb.GetCenter().y), 6.f, c_color(255, 255, 255, 255.f * alpha).as_imcolor(), 24);

	char value_buf[64];
	const char* value_buf_end = value_buf + DataTypeFormatString(value_buf, IM_ARRAYSIZE(value_buf), data_type, p_data, format);
	const auto value_size = CalcTextSize(value_buf, value_buf_end);
	const float number_w = ImMax(38.f, value_size.x + 14.f);
	const ImRect number_bb(pos + ImVec2(w - number_w, (row_h - 20.f) * 0.5f), pos + ImVec2(w, (row_h + 20.f) * 0.5f));
	draw_list->AddRectFilled(number_bb.Min, number_bb.Max, c_color(25, 27, 36, 255.f * alpha).as_imcolor(), 6.f);
	draw_list->AddRect(number_bb.Min, number_bb.Max, c_color(30, 33, 44, 255.f * alpha).as_imcolor(), 6.f);
	ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.922f, 0.922f, 0.941f, alpha));
	RenderTextClipped(number_bb.Min, number_bb.Max, value_buf, value_buf_end, &value_size, ImVec2(0.5f, 0.5f));
	const int grey = (int)std::lerp(130.f, 235.f, ImMax(norm > 0.f ? 1.f : 0.f, mod.hovered_alpha));
	ImGui::PopStyleColor();
	ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(grey / 255.f, grey / 255.f, grey / 255.f, alpha));
	if (label_size.x > 0.f)
		RenderTextClipped(total_bb.Min, ImVec2(frame_bb.Min.x - 8.f, total_bb.Max.y), label, NULL, &label_size, ImVec2(0.f, 0.5f));
	ImGui::PopStyleColor();
	draw_list->AddLine(ImVec2(pos.x, total_bb.Max.y), ImVec2(pos.x + w, total_bb.Max.y), c_color(255, 255, 255, 12.f * alpha).as_imcolor());
	IMGUI_TEST_ENGINE_ITEM_INFO(id, label, window->DC.ItemFlags);
	return value_changed;
}

bool c_menu::slider_int(const char* label, int* v, int v_min, int v_max, const char* format)
{
	return this->slider_scalar(label, ImGuiDataType_S32, v, &v_min, &v_max, format);
}