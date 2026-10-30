#include "../menu.h"
using namespace ImGui;

bool c_menu::key_bind(const char* label, key_binds_t& var)
{
	float alpha = this->get_alpha();

	const ImVec2 size_arg(ImMax(130.f, GetContentRegionAvail().x), 28.f);

	var.key = std::clamp(var.key, -1, 255);

	ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 1.f, 1.f, 0.5f * alpha));

	ImGuiWindow* window = ImGui::GetCurrentWindow();
	if (window->SkipItems)
	{
		ImGui::PopStyleColor();
		return false;
	}

	ImGuiContext& g = *GImGui;
	ImGuiIO& io = g.IO;
	const ImGuiStyle& style = g.Style;

	const ImGuiID id = window->GetID(label);
	bool popup_open = IsPopupOpen(id);
	const ImVec2 label_size = ImGui::CalcTextSize(label, NULL, true);

	const ImVec2 frame_size(80.f, 20.f);
	const auto pos = window->DC.CursorPos;
	const auto cursor_pos = pos + ImVec2(size_arg.x - frame_size.x, 0.f);
	const ImRect frame_bb(cursor_pos + ImVec2(0.f, 4.f), cursor_pos + frame_size + ImVec2(0.f, 4.f));
	const ImRect total_bb(pos, pos + size_arg);

	ItemSize(total_bb, style.FramePadding.y);
	if (!ItemAdd(total_bb, id, &frame_bb))
	{
		ImGui::PopStyleColor();
		return false;
	}

	const bool focus_requested = ImGui::FocusableItemRegister(window, g.ActiveId == id);

	const bool hovered = ImGui::ItemHoverable(frame_bb, id);

	if (hovered)
	{
		ImGui::SetHoveredID(id);
		g.MouseCursor = ImGuiMouseCursor_TextInput;
	}

	const bool user_clicked = hovered && io.MouseClicked[0];

	if (focus_requested || user_clicked)
	{
		if (g.ActiveId != id)
		{
			// Start edition
			memset(io.MouseDown, 0, sizeof(io.MouseDown));
			memset(io.KeysDown, 0, sizeof(io.KeysDown));
			var.key = 0;
		}
		ImGui::SetActiveID(id, window);
		ImGui::FocusWindow(window);
	}
	else if (io.MouseClicked[0])
	{
		// Release focus when we click outside
		if (g.ActiveId == id)
			ImGui::ClearActiveID();
	}

	bool value_changed = false;
	int key = var.key;

	if (g.ActiveId == id)
	{
		for (auto i = 0; i < 5; i++)
		{
			if (io.MouseDown[i])
			{
				switch (i)
				{
				case 0:
					key = VK_LBUTTON;
					break;
				case 1:
					key = VK_RBUTTON;
					break;
				case 2:
					key = VK_MBUTTON;
					break;
				case 3:
					key = VK_XBUTTON1;
					break;
				case 4:
					key = VK_XBUTTON2;
					break;
				}
				value_changed = true;
				ImGui::ClearActiveID();
			}
		}
		if (!value_changed)
		{
			for (auto i = VK_BACK; i <= VK_RMENU; i++)
			{
				if (io.KeysDown[i])
				{
					key = i;
					value_changed = true;
					ImGui::ClearActiveID();
				}
			}
		}

		if (IsKeyPressedMap(ImGuiKey_Escape))
		{
			var.key = -1;
			ImGui::ClearActiveID();
		}
		else
		{
			var.key = key;
		}
	}

	const auto ctx = GetCurrentContext();

	if (hovered && io.MouseClicked[1])
	{
		OpenPopupEx(id);
		popup_open = true;
	}

	auto& popup_mod = item_animations[CONST_HASH(label) + HASH("popup")];
	this->create_animation(popup_mod.alpha, popup_open, 0.6f, skip_disable | lerp_animation);

	if (popup_open)
	{
		// 3 bind mods
		float max_size = calc_max_popup_height(3);

		auto items_size = ImVec2(80.f, max_size * popup_mod.alpha);
		SetNextWindowSize(items_size);
		SetNextWindowPos(frame_bb.Min + ImVec2(0.f, 24.f));
		SetNextWindowBgAlpha(alpha * popup_mod.alpha);

		// We don't use BeginPopupEx() solely because we have a custom name string, which we could make an argument to BeginPopupEx()
		ImGuiWindowFlags window_flags = ImGuiWindowFlags_Popup | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar;

		char name[16];
		ImFormatString(name, IM_ARRAYSIZE(name), CXOR("##Combo_%02d"), ctx->BeginPopupStack.Size); // Recycle windows based on depth

		// Horizontally align ourselves with the framed text
		PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(7, style.WindowPadding.y));
		bool ret = Begin(name, NULL, window_flags);
		PopStyleVar();
		if (!ret)
		{
			EndPopup();
			ImGui::PopStyleColor();
			return false;
		}

		if (this->selectable(CXOR("Always"), var.type == 0))
			var.type = 0;
		if (this->selectable(CXOR("Hold"), var.type == 1))
			var.type = 1;
		if (this->selectable(CXOR("Toggle"), var.type == 2))
			var.type = 2;

		EndPopup();
	}

	std::string buffer{ CXOR("None") };
	buffer.resize(128);

	auto& mod = item_animations[CONST_HASH(label)];

	const bool hovered_frame = ItemHoverable(ImRect(pos, pos + size_arg), id);
	this->create_animation(mod.hovered_alpha, hovered_frame, 1.f, lerp_animation);

	auto& mod_frame = item_animations[CONST_HASH(label) + HASH("frame")];
	this->create_animation(mod_frame.hovered_alpha, hovered || g.ActiveId == id, 1.f, lerp_animation);

	// Darkside field: compact right-aligned 80x20 key button.
	draw_list->AddRectFilled(frame_bb.Min, frame_bb.Max, c_color(25, 27, 36, 255.f * alpha).as_imcolor(), 8.f);
	draw_list->AddRect(frame_bb.Min, frame_bb.Max, c_color(30, 33, 44, 255.f * alpha).as_imcolor(), 8.f);

	if (var.type == 0)
		buffer = CXOR("Always");
	else if (var.key != -1 && g.ActiveId != id)
		buffer = key_strings[var.key];
	else if (g.ActiveId == id)
		buffer = CXOR("...");

	ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 1.f, 1.f, alpha));
	RenderTextClipped(frame_bb.Min, frame_bb.Max, buffer.c_str(), NULL, NULL, style.ButtonTextAlign, &frame_bb);
	ImGui::PopStyleColor();

	if (label_size.x > 0)
		RenderTextClipped(total_bb.Min, ImVec2(frame_bb.Min.x - 8.f, total_bb.Max.y), label, NULL, &label_size, ImVec2(0.f, 0.5f));
	draw_list->AddLine(ImVec2(pos.x, total_bb.Max.y), ImVec2(pos.x + size_arg.x, total_bb.Max.y), c_color(255, 255, 255, 12.f * alpha).as_imcolor());

	ImGui::PopStyleColor();
	return value_changed;
}