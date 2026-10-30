#pragma once

struct IDirect3DTexture9;
struct ImVec2;

namespace inventory_preview
{
	bool weapon_card(int slot, const char* label, bool selected, float alpha, int team = 3);
	void draw_weapon(int slot, const ImVec2& size, float alpha, bool force_skin = false, int team = 3);
	void draw_gloves(const ImVec2& size, float alpha);
	void draw_agent(int team, const ImVec2& size, float alpha);
	void shutdown();
}
