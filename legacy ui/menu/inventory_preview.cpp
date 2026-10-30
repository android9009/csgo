#include "../../globals.hpp"
#include "../config_vars.h"
#include "../../skins.hpp"
#include "inventory_preview.h"
#include "menu_resources.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <unordered_map>

namespace inventory_preview
{
	namespace
	{
		struct texture_entry_t
		{
			IDirect3DTexture9* texture{};
			std::uint64_t last_used{};
		};

		std::unordered_map<std::string, texture_entry_t> texture_cache{};
		std::unordered_map<std::string, ULONGLONG> unavailable_textures{};
		std::uint64_t cache_clock{};
		IDirect3DDevice9* cache_device{};
		constexpr std::size_t max_cached_textures = 96;
		constexpr unsigned int max_image_size = 16 * 1024 * 1024;

		constexpr const char* embedded_paths[] = {
			"resource/flash/econ/weapons/base_weapons/weapon_knife.png",
			"resource/flash/econ/weapons/base_weapons/weapon_bayonet.png",
			"resource/flash/econ/weapons/base_weapons/weapon_knife_flip.png",
			"resource/flash/econ/weapons/base_weapons/weapon_knife_gut.png",
			"resource/flash/econ/weapons/base_weapons/weapon_knife_karambit.png",
			"resource/flash/econ/weapons/base_weapons/weapon_knife_m9_bayonet.png",
			"resource/flash/econ/weapons/base_weapons/weapon_knife_tactical.png",
			"resource/flash/econ/weapons/base_weapons/weapon_knife_falchion.png",
			"resource/flash/econ/weapons/base_weapons/weapon_knife_survival_bowie.png",
			"resource/flash/econ/weapons/base_weapons/weapon_knife_butterfly.png",
			"resource/flash/econ/weapons/base_weapons/weapon_knife_push.png",
			"resource/flash/econ/weapons/base_weapons/weapon_knife_css.png",
			"resource/flash/econ/weapons/base_weapons/weapon_knife_skeleton.png",
			"resource/flash/econ/weapons/base_weapons/weapon_knife_outdoor.png",
			"resource/flash/econ/weapons/base_weapons/weapon_knife_cord.png",
			"resource/flash/econ/weapons/base_weapons/weapon_knife_canis.png",
			"resource/flash/econ/weapons/base_weapons/weapon_knife_ursus.png",
			"resource/flash/econ/weapons/base_weapons/weapon_knife_gypsy_jackknife.png",
			"resource/flash/econ/weapons/base_weapons/weapon_knife_stiletto.png",
			"resource/flash/econ/weapons/base_weapons/weapon_knife_widowmaker.png",
			"resource/flash/econ/weapons/base_weapons/weapon_deagle.png",
			"resource/flash/econ/weapons/base_weapons/weapon_elite.png",
			"resource/flash/econ/weapons/base_weapons/weapon_fiveseven.png",
			"resource/flash/econ/weapons/base_weapons/weapon_glock.png",
			"resource/flash/econ/weapons/base_weapons/weapon_ak47.png",
			"resource/flash/econ/weapons/base_weapons/weapon_aug.png",
			"resource/flash/econ/weapons/base_weapons/weapon_awp.png",
			"resource/flash/econ/weapons/base_weapons/weapon_famas.png",
			"resource/flash/econ/weapons/base_weapons/weapon_g3sg1.png",
			"resource/flash/econ/weapons/base_weapons/weapon_galilar.png",
			"resource/flash/econ/weapons/base_weapons/weapon_m249.png",
			"resource/flash/econ/weapons/base_weapons/weapon_m4a1.png",
			"resource/flash/econ/weapons/base_weapons/weapon_m4a1_silencer.png",
			"resource/flash/econ/weapons/base_weapons/weapon_mac10.png",
			"resource/flash/econ/weapons/base_weapons/weapon_p90.png",
			"resource/flash/econ/weapons/base_weapons/weapon_mp5sd.png",
			"resource/flash/econ/weapons/base_weapons/weapon_ump45.png",
			"resource/flash/econ/weapons/base_weapons/weapon_xm1014.png",
			"resource/flash/econ/weapons/base_weapons/weapon_bizon.png",
			"resource/flash/econ/weapons/base_weapons/weapon_mag7.png",
			"resource/flash/econ/weapons/base_weapons/weapon_negev.png",
			"resource/flash/econ/weapons/base_weapons/weapon_sawedoff.png",
			"resource/flash/econ/weapons/base_weapons/weapon_tec9.png",
			"resource/flash/econ/weapons/base_weapons/weapon_hkp2000.png",
			"resource/flash/econ/weapons/base_weapons/weapon_mp7.png",
			"resource/flash/econ/weapons/base_weapons/weapon_mp9.png",
			"resource/flash/econ/weapons/base_weapons/weapon_nova.png",
			"resource/flash/econ/weapons/base_weapons/weapon_p250.png",
			"resource/flash/econ/weapons/base_weapons/weapon_scar20.png",
			"resource/flash/econ/weapons/base_weapons/weapon_sg556.png",
			"resource/flash/econ/weapons/base_weapons/weapon_ssg08.png",
			"resource/flash/econ/weapons/base_weapons/weapon_usp_silencer.png",
			"resource/flash/econ/weapons/base_weapons/weapon_cz75a.png",
			"resource/flash/econ/weapons/base_weapons/weapon_revolver.png",
			"resource/flash/econ/weapons/base_weapons/weapon_taser.png",
			"resource/flash/econ/weapons/base_weapons/ct_gloves.png",
			"resource/flash/econ/weapons/base_weapons/t_gloves.png",
			"resource/flash/econ/characters/customplayer_ct_map_based.png",
			"resource/flash/econ/characters/customplayer_t_map_based.png",
			"resource/flash/econ/characters/customplayer_ctm_sas.png",
			"resource/flash/econ/characters/customplayer_tm_phoenix.png",
		};
		static_assert(IM_ARRAYSIZE(embedded_paths) == IDR_PREVIEW_LAST - IDR_PREVIEW_FIRST + 1, "Preview resource map is out of sync");

		void evict_oldest_texture()
		{
			if (texture_cache.empty())
				return;

			auto oldest = std::min_element(texture_cache.begin(), texture_cache.end(), [](const auto& a, const auto& b) {
				return a.second.last_used < b.second.last_used;
			});

			if (oldest->second.texture)
				oldest->second.texture->Release();
			texture_cache.erase(oldest);
		}

		bool recently_unavailable(const std::string& path)
		{
			const auto it = unavailable_textures.find(path);
			if (it == unavailable_textures.end())
				return false;

			constexpr ULONGLONG retry_delay_ms = 5000;
			if (GetTickCount64() - it->second < retry_delay_ms)
				return true;

			unavailable_textures.erase(it);
			return false;
		}

		void mark_unavailable(const std::string& path)
		{
			unavailable_textures[path] = GetTickCount64();
		}

		bool read_image(const std::string& path, std::vector<std::uint8_t>& data)
		{
			if (HACKS->file_system)
			{
				auto handle = HACKS->file_system->open(path.c_str(), CXOR("rb"), CXOR("GAME"));
				if (!handle)
					handle = HACKS->file_system->open(path.c_str(), CXOR("rb"), nullptr);

				if (handle)
				{
					const unsigned int size = HACKS->file_system->size(handle);
					if (size && size <= max_image_size)
					{
						data.resize(size);
						const int bytes_read = HACKS->file_system->read(data.data(), size, handle);
						HACKS->file_system->close(handle);
						if (bytes_read == static_cast<int>(size))
							return true;
					}
					else
					{
						HACKS->file_system->close(handle);
					}
				}
			}

			data.clear();
			if (HACKS->exe_path.empty())
				return false;

			std::string disk_path = HACKS->exe_path + path;
			std::replace(disk_path.begin(), disk_path.end(), '/', '\\');
			std::ifstream input(disk_path, std::ios::binary | std::ios::ate);
			if (!input)
				return false;

			const std::streamoff size = input.tellg();
			if (size <= 0 || size > static_cast<std::streamoff>(max_image_size))
				return false;

			data.resize(static_cast<std::size_t>(size));
			input.seekg(0, std::ios::beg);
			input.read(reinterpret_cast<char*>(data.data()), size);
			return input.good();
		}

		bool read_embedded_image(const std::string& path, std::vector<std::uint8_t>& data)
		{
			int resource_id = 0;
			for (int i = 0; i < static_cast<int>(IM_ARRAYSIZE(embedded_paths)); ++i)
			{
				if (path == embedded_paths[i])
				{
					resource_id = IDR_PREVIEW_FIRST + i;
					break;
				}
			}
			if (!resource_id)
				return false;

			static const unsigned char module_anchor = 0;
			HMODULE module = nullptr;
			GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
				reinterpret_cast<LPCSTR>(&module_anchor), &module);
			const HRSRC resource = module ? FindResourceW(module, MAKEINTRESOURCEW(resource_id), MAKEINTRESOURCEW(10)) : nullptr;
			const HGLOBAL loaded = resource ? LoadResource(module, resource) : nullptr;
			const auto* bytes = loaded ? static_cast<const std::uint8_t*>(LockResource(loaded)) : nullptr;
			const DWORD size = resource ? SizeofResource(module, resource) : 0;
			if (!bytes || !size || size > max_image_size)
				return false;

			data.assign(bytes, bytes + size);
			return true;
		}

		IDirect3DTexture9* load_texture(const std::string& path)
		{
			if (path.empty())
				return nullptr;

			auto device = RENDER->get_device();
			if (!device)
				return nullptr;

			if (cache_device != device)
			{
				shutdown();
				cache_device = device;
			}

			auto cached = texture_cache.find(path);
			if (cached != texture_cache.end())
			{
				cached->second.last_used = ++cache_clock;
				return cached->second.texture;
			}

			if (recently_unavailable(path))
				return nullptr;

			std::vector<std::uint8_t> data;
			if (!read_image(path, data) && !read_embedded_image(path, data))
			{
				mark_unavailable(path);
				return nullptr;
			}

			D3DXIMAGE_INFO image_info{};
			if (FAILED(D3DXGetImageInfoFromFileInMemory(data.data(), static_cast<UINT>(data.size()), &image_info)) || !image_info.Width || !image_info.Height)
			{
				mark_unavailable(path);
				return nullptr;
			}

			const float resize = std::min(1.f, 256.f / static_cast<float>(std::max(image_info.Width, image_info.Height)));
			const UINT width = std::max(1u, static_cast<UINT>(image_info.Width * resize));
			const UINT height = std::max(1u, static_cast<UINT>(image_info.Height * resize));

			IDirect3DTexture9* texture = nullptr;
			const HRESULT result = D3DXCreateTextureFromFileInMemoryEx(
				device, data.data(), static_cast<UINT>(data.size()), width, height, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT,
				D3DX_DEFAULT, D3DX_DEFAULT, 0, nullptr, nullptr, &texture);

			if (FAILED(result) || !texture)
			{
				mark_unavailable(path);
				return nullptr;
			}

			if (texture_cache.size() >= max_cached_textures)
				evict_oldest_texture();

			texture_cache[path] = { texture, ++cache_clock };
			return texture;
		}

		std::string paint_kit_name(int paint_kit)
		{
			const auto it = skin_changer::paint_kit_names_by_id.find(paint_kit);
			return it == skin_changer::paint_kit_names_by_id.end() ? std::string() : it->second;
		}

		const char* knife_econ_name(int model)
		{
#ifdef LEGACY
			static constexpr const char* names[] = {
				"weapon_knife", "weapon_bayonet", "weapon_knife_flip", "weapon_knife_gut",
				"weapon_knife_karambit", "weapon_knife_m9_bayonet", "weapon_knife_tactical",
				"weapon_knife_falchion", "weapon_knife_survival_bowie", "weapon_knife_butterfly",
				"weapon_knife_push"
			};
#else
			static constexpr const char* names[] = {
				"weapon_knife", "weapon_bayonet", "weapon_knife_css", "weapon_knife_skeleton",
				"weapon_knife_outdoor", "weapon_knife_cord", "weapon_knife_canis", "weapon_knife_flip",
				"weapon_knife_gut", "weapon_knife_karambit", "weapon_knife_m9_bayonet",
				"weapon_knife_tactical", "weapon_knife_falchion", "weapon_knife_survival_bowie",
				"weapon_knife_butterfly", "weapon_knife_push", "weapon_knife_ursus",
				"weapon_knife_gypsy_jackknife", "weapon_knife_stiletto", "weapon_knife_widowmaker"
			};
#endif
			const int index = std::clamp(model, 0, static_cast<int>(IM_ARRAYSIZE(names)) - 1);
			return names[index];
		}

		std::string weapon_econ_name(int slot, const skin_weapon_t& config)
		{
			static constexpr const char* names[skin_weapon_cfg_max] = {
				"weapon_deagle", "weapon_elite", "weapon_fiveseven", "weapon_glock", "weapon_ak47",
				"weapon_aug", "weapon_awp", "weapon_famas", "weapon_g3sg1", "weapon_galilar",
				"weapon_m249", "weapon_m4a1", "weapon_m4a1_silencer", "weapon_mac10", "weapon_p90",
				"weapon_mp5sd", "weapon_ump45", "weapon_xm1014", "weapon_bizon", "weapon_mag7",
				"weapon_negev", "weapon_sawedoff", "weapon_tec9", "weapon_hkp2000", "weapon_mp7",
				"weapon_mp9", "weapon_nova", "weapon_p250", "weapon_scar20", "weapon_sg556",
				"weapon_ssg08", "weapon_usp_silencer", "weapon_cz75a", "weapon_revolver", "weapon_knife", "weapon_taser"
			};

			if (slot == weapon_cfg_knife)
				return knife_econ_name(config.knife_model);
			return names[std::clamp(slot, 0, skin_weapon_cfg_max - 1)];
		}

		IDirect3DTexture9* weapon_texture(int slot, bool force_skin = false, int team = 3)
		{
			if (slot < 0 || slot >= skin_weapon_cfg_max)
				return nullptr;

			const auto& config = skin_changer::get_weapon_config(slot, team);
			const std::string weapon_name = weapon_econ_name(slot, config);
			const std::string base_path = CXOR("resource/flash/econ/weapons/base_weapons/") + weapon_name + CXOR(".png");

			if ((config.enable || force_skin) && !skin_changer::paint_kits.empty())
			{
				const int skin_index = std::clamp(config.skin, 0, static_cast<int>(skin_changer::paint_kits.size()) - 1);
				const std::string kit_name = paint_kit_name(skin_changer::paint_kits[skin_index].second);
				if (!kit_name.empty())
				{
					const std::string prefix = CXOR("resource/flash/econ/default_generated/") + weapon_name + "_" + kit_name;
					if (auto texture = load_texture(prefix + CXOR("_light_large.png")))
						return texture;
					if (auto texture = load_texture(prefix + CXOR("_light.png")))
						return texture;
				}
			}

			return load_texture(base_path);
		}

		IDirect3DTexture9* glove_texture()
		{
			if (!g_cfg.skins.model_glove)
				return load_texture(CXOR("resource/flash/econ/weapons/base_weapons/ct_gloves.png"));

			static constexpr const char* prefixes[] = {
				"", "studded_bloodhound_gloves", "sporty_gloves", "slick_gloves", "leather_handwraps",
				"motorcycle_gloves", "specialist_gloves", "studded_hydra_gloves"
			};
			const int model = std::clamp(g_cfg.skins.model_glove, 0, static_cast<int>(IM_ARRAYSIZE(prefixes)) - 1);
			const std::string kit_name = paint_kit_name(skin_changer::glove_skins_id());
			if (!kit_name.empty())
			{
				const std::string path = CXOR("resource/flash/econ/default_generated/") + std::string(prefixes[model]) +
					"_" + kit_name + CXOR("_light_large.png");
				if (auto texture = load_texture(path))
					return texture;
			}

			return load_texture(CXOR("resource/flash/econ/weapons/base_weapons/ct_gloves.png"));
		}

		IDirect3DTexture9* agent_texture(int team)
		{
			std::string model = skin_changer::get_model_name(team);
			if (model.empty())
			{
				return load_texture(team == 2
					? CXOR("resource/flash/econ/characters/customplayer_tm_phoenix.png")
					: CXOR("resource/flash/econ/characters/customplayer_ctm_sas.png"));
			}

			std::replace(model.begin(), model.end(), '\\', '/');
			const auto separator = model.find_last_of('/');
			if (separator != std::string::npos)
				model.erase(0, separator + 1);
			const auto extension = model.find_last_of('.');
			if (extension != std::string::npos)
				model.erase(extension);
			std::transform(model.begin(), model.end(), model.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

			if (auto texture = load_texture(CXOR("resource/flash/econ/characters/customplayer_") + model + CXOR(".png")))
				return texture;

			const auto variant = model.find(CXOR("_variant"));
			const auto var = model.find(CXOR("_var"));
			const auto suffix = variant != std::string::npos ? variant : var;
			if (suffix != std::string::npos)
			{
				const std::string family = model.substr(0, suffix);
				if (auto texture = load_texture(CXOR("resource/flash/econ/characters/customplayer_") + family + CXOR(".png")))
					return texture;
			}

			return load_texture(team == 2
				? CXOR("resource/flash/econ/characters/customplayer_tm_phoenix.png")
				: CXOR("resource/flash/econ/characters/customplayer_ctm_sas.png"));
		}

		void draw_image_in_rect(IDirect3DTexture9* texture, const ImVec2& min, const ImVec2& max, float alpha)
		{
			if (!texture)
				return;

			D3DSURFACE_DESC desc{};
			if (FAILED(texture->GetLevelDesc(0, &desc)) || !desc.Width || !desc.Height)
				return;

			const float width = max.x - min.x;
			const float height = max.y - min.y;
			const float scale = std::min(width / desc.Width, height / desc.Height);
			const ImVec2 image_size(desc.Width * scale, desc.Height * scale);
			const ImVec2 image_min(min.x + (width - image_size.x) * 0.5f, min.y + (height - image_size.y) * 0.5f);
			ImGui::GetWindowDrawList()->AddImage(reinterpret_cast<ImTextureID>(texture), image_min, image_min + image_size,
				ImVec2(0, 0), ImVec2(1, 1), IM_COL32(255, 255, 255, std::clamp(alpha, 0.f, 1.f) * 255.f));
		}

		void draw_preview(IDirect3DTexture9* texture, const ImVec2& size, float alpha)
		{
			const ImVec2 min = ImGui::GetCursorScreenPos();
			const ImVec2 max = min + size;
			ImGui::Dummy(size);
			auto draw_list = ImGui::GetWindowDrawList();
			draw_list->AddRectFilled(min, max, IM_COL32(12, 14, 21, std::clamp(alpha, 0.f, 1.f) * 180.f), 8.f);
			if (texture)
				draw_image_in_rect(texture, min + ImVec2(4, 4), max - ImVec2(4, 4), alpha);
			else
			{
				const char* text = CXOR("Preview unavailable");
				const ImVec2 text_size = ImGui::CalcTextSize(text);
				draw_list->AddText(min + (size - text_size) * 0.5f, IM_COL32(150, 153, 165, std::clamp(alpha, 0.f, 1.f) * 255.f), text);
			}
		}
	}

	bool weapon_card(int slot, const char* label, bool selected, float alpha, int team)
	{
		const float width = std::max(90.f, ImGui::GetContentRegionAvail().x);
		const ImVec2 size(width, 76.f);
		ImGui::PushID(slot);
		const bool pressed = ImGui::InvisibleButton(CXOR("##weapon_card"), size);
		const bool hovered = ImGui::IsItemHovered();
		ImGui::PopID();

		const ImVec2 min = ImGui::GetItemRectMin();
		const ImVec2 max = ImGui::GetItemRectMax();
		auto draw_list = ImGui::GetWindowDrawList();
		const int opacity = static_cast<int>(std::clamp(alpha, 0.f, 1.f) * 255.f);
		draw_list->AddRectFilled(min, max, IM_COL32(hovered ? 29 : 20, hovered ? 32 : 23, hovered ? 44 : 33, opacity), 5.f);
		const bool has_label = label && *label;
		draw_image_in_rect(weapon_texture(slot, false, team), min + ImVec2(4, 3), max - ImVec2(4, has_label ? 20 : 5), alpha);

		if (has_label)
		{
			const ImVec2 text_size = ImGui::CalcTextSize(label);
			draw_list->AddText(ImVec2(min.x + (size.x - text_size.x) * 0.5f, max.y - 17.f), IM_COL32(220, 222, 230, opacity), label);
		}
		if (selected)
			draw_list->AddLine(ImVec2(min.x + 3.f, max.y - 1.f), ImVec2(max.x - 3.f, max.y - 1.f), g_cfg.misc.ui_color.base().as_imcolor(), 2.f);
		return pressed;
	}

	void draw_weapon(int slot, const ImVec2& size, float alpha, bool force_skin, int team)
	{
		draw_preview(weapon_texture(slot, force_skin, team), size, alpha);
	}

	void draw_gloves(const ImVec2& size, float alpha)
	{
		draw_preview(glove_texture(), size, alpha);
	}

	void draw_agent(int team, const ImVec2& size, float alpha)
	{
		draw_preview(agent_texture(team), size, alpha);
	}

	void shutdown()
	{
		for (auto& entry : texture_cache)
		{
			if (entry.second.texture)
				entry.second.texture->Release();
		}
		texture_cache.clear();
		unavailable_textures.clear();
		cache_device = nullptr;
	}
}
