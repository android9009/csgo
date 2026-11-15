#include "menu.h"

#include "../cfg.h"
#include "gui/controls/combo_box_agent.h"
#include "macros.h"
#include "../../../sdk/interfaces/interfaces.hpp"
#include "../../../sdk/skin_icon_file.hpp"
#include "../../../sdk/skin_catalog.hpp"
#include "../preview_bridge.hpp"
#include "../../../utils/log.hpp"
#include <algorithm>
#include <array>
#include <map>
#include <vector>

using namespace evo::gui;
using namespace evo::gui::helpers;
using namespace evo::ren;

namespace menu::group
{

    namespace {
    struct WeaponIcon { const char* label; const char* internal; };
    using Column = std::vector<WeaponIcon>;
    // The teams have different exclusive equipment; shared weapons deliberately
    // appear in both loadouts. This is the complete weapon catalogue, not the
    // five-slot CS2 loadout shown in the reference image.
    const std::array<Column, 3> ct_weapons{{
        {{"USP-S", "weapon_usp_silencer"}, {"P2000", "weapon_hkp2000"}, {"Dual Berettas", "weapon_elite"}, {"P250", "weapon_p250"}, {"Five-SeveN", "weapon_fiveseven"}, {"CZ75-Auto", "weapon_cz75a"}, {"Desert Eagle", "weapon_deagle"}, {"R8 Revolver", "weapon_revolver"}},
        {{"MP9", "weapon_mp9"}, {"MP7", "weapon_mp7"}, {"MP5-SD", "weapon_mp5sd"}, {"UMP-45", "weapon_ump45"}, {"P90", "weapon_p90"}, {"PP-Bizon", "weapon_bizon"}, {"Nova", "weapon_nova"}, {"XM1014", "weapon_xm1014"}, {"MAG-7", "weapon_mag7"}, {"M249", "weapon_m249"}, {"Negev", "weapon_negev"}},
        {{"FAMAS", "weapon_famas"}, {"M4A4", "weapon_m4a1"}, {"M4A1-S", "weapon_m4a1_silencer"}, {"AUG", "weapon_aug"}, {"SSG 08", "weapon_ssg08"}, {"AWP", "weapon_awp"}, {"SCAR-20", "weapon_scar20"}}
    }};
    const std::array<Column, 3> t_weapons{{
        {{"Glock-18", "weapon_glock"}, {"Dual Berettas", "weapon_elite"}, {"P250", "weapon_p250"}, {"Tec-9", "weapon_tec9"}, {"CZ75-Auto", "weapon_cz75a"}, {"Desert Eagle", "weapon_deagle"}, {"R8 Revolver", "weapon_revolver"}},
        {{"MAC-10", "weapon_mac10"}, {"MP7", "weapon_mp7"}, {"MP5-SD", "weapon_mp5sd"}, {"UMP-45", "weapon_ump45"}, {"P90", "weapon_p90"}, {"PP-Bizon", "weapon_bizon"}, {"Nova", "weapon_nova"}, {"XM1014", "weapon_xm1014"}, {"Sawed-Off", "weapon_sawedoff"}, {"M249", "weapon_m249"}, {"Negev", "weapon_negev"}},
        {{"Galil AR", "weapon_galilar"}, {"AK-47", "weapon_ak47"}, {"SG 553", "weapon_sg556"}, {"SSG 08", "weapon_ssg08"}, {"AWP", "weapon_awp"}, {"G3SG1", "weapon_g3sg1"}}
    }};
    struct CachedIcon {
        std::vector<std::uint8_t> bytes; // texture keeps a non-owning pointer to this data
        std::shared_ptr<texture> image;
        bool attempted{};
        unsigned long long last_used{};
    };
    std::map<std::string, CachedIcon> icons;
    unsigned long long icon_clock{};
    void trim_icons() {
        // Evict before issuing this frame's draw commands. Texture bytes must
        // survive D3D9 Reset, but browsing thousands of skins must be bounded.
        while (icons.size() > 80) {
            auto oldest = icons.begin();
            for (auto it = icons.begin(); it != icons.end(); ++it)
                if (it->second.last_used < oldest->second.last_used) oldest = it;
            if (oldest->second.image) {
                oldest->second.image->destroy();
                draw.textures.erase(hash(std::string("skins>weapon_icon>") + oldest->first));
            }
            icons.erase(oldest);
        }
    }
    std::shared_ptr<texture> get_weapon_icon(const char* weapon, const std::string& paint, bool& load_budget) {
        const std::string key = std::string(weapon) + "|" + paint;
        auto& item = icons[key];
        item.last_used = ++icon_clock;
        if (!item.attempted && load_budget && Interfaces::BaseFileSystem) {
            load_budget = false; // decode at most one newly visible image per frame
            item.attempted = true;
            const auto path = paint.empty() ? SkinIconPath::Base(weapon) : SkinIconPath::Generated(weapon, paint);
            item.bytes = SkinIconFile::Read(Interfaces::BaseFileSystem, path);
            if (!item.bytes.empty()) {
                item.image = std::make_shared<texture>(item.bytes.data(), static_cast<uint32_t>(item.bytes.size()));
                item.image->create();
                draw.textures[hash(std::string("skins>weapon_icon>") + key)] = item.image;
            }
        }
        return item.image;
    }
    SkinCatalog::Catalog catalog;
    bool catalog_loaded{};
    bool load_catalog() {
        if (catalog_loaded) return true;
        if (!Interfaces::BaseFileSystem) return false;
        const auto data = SkinIconFile::Read(Interfaces::BaseFileSystem, "scripts/items/items_game.txt", 64u * 1024u * 1024u);
        if (data.empty()) {
            LOG_WARN("skin menu: unable to read scripts/items/items_game.txt");
            return false;
        }
        catalog = SkinCatalog::Parse(std::string_view(reinterpret_cast<const char*>(data.data()), data.size()));
        catalog_loaded = !catalog.empty();
        LOG_INFO("skin menu: catalogue loaded for %zu weapons", catalog.size());
        return catalog_loaded;
    }
    std::map<std::string, std::string> selection[2]; // independent CT/T menu state

    class weapon_grid final : public control {
    public:
        weapon_grid(control_id id, const vec2& size) : control(id, {}, size) {}
        void render() override {
            control::render();
            if (!is_visible) return;
            trim_icons();
            auto& layer = draw.layers[ctx->content_layer];
            const auto r = area_abs();
            if (selected_weapon.internal) { render_skins(layer, r); return; }
            const auto ct = rect(r.tl(), r.tl() + vec2{72.f, 25.f});
            const auto t = rect(r.tl() + vec2{78.f, 0.f}, r.tl() + vec2{150.f, 25.f});
            layer->add_rect_filled_rounded(ct, team_ct ? colors.bg_block_light : colors.bg_bottom, 2.f);
            layer->add_rect_filled_rounded(t, !team_ct ? colors.bg_block_light : colors.bg_bottom, 2.f);
            layer->font = draw.fonts[GUI_HASH("gui_main")];
            layer->add_text(ct.center(), "CT", team_ct ? colors.accent : colors.text_dark, text_params::with_vh(align_center, align_center));
            layer->add_text(t.center(), "T", !team_ct ? colors.accent : colors.text_dark, text_params::with_vh(align_center, align_center));
            const auto& columns = team_ct ? ct_weapons : t_weapons;
            const float gap = 6.f, cell_width = (r.width() - 2.f * gap) / 3.f;
            static constexpr float header_y = 34.f, list_y = 55.f, cell_height = 78.f;
            const char* headings[] = {"Pistols", "Mid-Tier", "Rifles"};
            for (size_t c = 0; c < columns.size(); ++c) {
                const float x = r.mins.x + static_cast<float>(c) * (cell_width + gap);
                layer->add_text(vec2{x + cell_width * .5f, r.mins.y + header_y}, headings[c], colors.text, text_params::with_vh(align_center, align_center));
            }
            const rect viewport{r.mins + vec2{0.f, list_y}, r.maxs};
            const auto old_clip = layer->g.clip_rect;
            layer->override_clip_rect(viewport);
            bool load_budget = true;
            for (size_t c = 0; c < columns.size(); ++c) {
                const float x = r.mins.x + static_cast<float>(c) * (cell_width + gap);
                for (size_t i = 0; i < columns[c].size(); ++i) {
                    const float y = r.mins.y + list_y + static_cast<float>(i) * cell_height - scroll;
                    const rect cell{vec2{x, y}, vec2{x + cell_width, y + cell_height - gap}};
                    if (cell.maxs.y <= viewport.mins.y || cell.mins.y >= viewport.maxs.y) continue;
                    const auto& weapon = columns[c][i];
                    layer->add_rect_filled_rounded(cell, colors.bg_bottom, 2.f);
                    if (auto icon = get_weapon_icon(weapon.internal, selected_paint(weapon.internal), load_budget); icon && icon->obj) {
                        const auto native = icon->get_size();
                        if (native.x > 0.f && native.y > 0.f) {
                            const float scale = (std::min)((cell_width - 10.f) / native.x, 48.f / native.y);
                            const vec2 image_size = native * scale;
                            const vec2 center{x + cell_width * .5f, y + 27.f};
                            layer->g.texture = icon->obj;
                            layer->add_rect_filled(rect{center - image_size * .5f, center + image_size * .5f}, color::white());
                            layer->g.texture = {};
                        }
                    }
                    layer->font = draw.fonts[GUI_HASH("gui_main")];
                    layer->add_text(vec2{x + cell_width * .5f, y + 57.f}, weapon.label, colors.text, text_params::with_vh(align_center, align_center));
                    layer->add_rect_filled(rect{vec2{x + 3.f, y + cell_height - 9.f}, vec2{x + cell_width - 3.f, y + cell_height - 7.f}}, colors.outline);
                }
            }
            layer->g.clip_rect = old_clip;
        }
        void on_mouse_down(char key) override {
            if (key != m_left) return;
            const auto local = input.cursor() - area_abs().tl();
            const float cell_width = (size.x - 12.f) / 3.f;
            if (selected_weapon.internal) {
                if (local.y >= 0.f && local.y < 30.f && local.x >= size.x - 35.f) {
                    close_skins();
                    return;
                }
                if (local.y < 42.f || local.x < 0.f) return;
                const int col = static_cast<int>(local.x / (cell_width + 6.f));
                const int row = static_cast<int>((local.y - 42.f + skin_scroll) / 78.f);
                if (col < 0 || col >= 3 || row < 0 || local.x - col * (cell_width + 6.f) >= cell_width) return;
                const size_t index = static_cast<size_t>(row * 3 + col);
                if (index == 0) { selection[team_ct ? 0 : 1].erase(selected_weapon.internal); close_skins(); }
                else if (index <= paints.size()) {
                    selection[team_ct ? 0 : 1][selected_weapon.internal] = paints[index - 1];
                    close_skins();
                }
                return;
            }
            if (local.y >= 0.f && local.y < 25.f) {
                if (local.x >= 0.f && local.x < 72.f) { team_ct = true; scroll = 0.f; }
                else if (local.x >= 78.f && local.x < 150.f) { team_ct = false; scroll = 0.f; }
                return;
            }
            if (local.y < 55.f || local.x < 0.f) return;
            const int col = static_cast<int>(local.x / (cell_width + 6.f));
            const int row = static_cast<int>((local.y - 55.f + scroll) / 78.f);
            if (col < 0 || col >= 3 || row < 0 || local.x - col * (cell_width + 6.f) >= cell_width) return;
            const auto& columns = team_ct ? ct_weapons : t_weapons;
            if (static_cast<size_t>(row) >= columns[col].size()) return;
            selected_weapon = columns[col][row];
            paints.clear();
            if (load_catalog()) {
                auto it = catalog.find(selected_weapon.internal);
                if (it != catalog.end()) paints = it->second;
            }
            skin_scroll = 0.f;
            FatalityPreviewBridge::skins_selecting.store(true, std::memory_order_relaxed);
        }
        void on_mouse_wheel(float delta) override {
            input.consume_wheel();
            if (selected_weapon.internal) {
                const float rows = static_cast<float>((paints.size() + 3) / 3);
                const float max_scroll = (std::max)(0.f, rows * 78.f - (size.y - 42.f));
                skin_scroll = std::clamp(skin_scroll - delta * 55.f, 0.f, max_scroll);
            } else {
                const float viewport_height = (std::max)(0.f, size.y - 55.f);
                const float max_scroll = (std::max)(0.f, 11.f * 78.f - viewport_height);
                scroll = std::clamp(scroll - delta * 55.f, 0.f, max_scroll);
            }
        }
    private:
        const std::string& selected_paint(const char* weapon) const {
            static const std::string empty;
            const auto& chosen = selection[team_ct ? 0 : 1];
            const auto it = chosen.find(weapon);
            return it == chosen.end() ? empty : it->second;
        }
        void close_skins() {
            selected_weapon = {};
            FatalityPreviewBridge::skins_selecting.store(false, std::memory_order_relaxed);
        }
        void render_skins(std::shared_ptr<evo::ren::layer>& layer, const rect& r) {
            layer->font = draw.fonts[GUI_HASH("gui_main")];
            layer->add_text(r.tl() + vec2{2.f, 12.f}, std::string(selected_weapon.label) + " - SKINS", colors.text, text_params::with_v(align_center));
            const rect close{r.tr() - vec2{30.f, 0.f}, r.tr() + vec2{0.f, 27.f}};
            layer->add_rect_filled_rounded(close, colors.bg_bottom, 2.f);
            layer->add_text(close.center(), "X", color{230, 60, 90}, text_params::with_vh(align_center, align_center));
            if (!catalog_loaded)
                layer->add_text(r.tl() + vec2{0.f, 30.f}, "Skin catalogue unavailable", colors.text_dark);
            const float cell_width = (r.width() - 12.f) / 3.f;
            const rect viewport{r.tl() + vec2{0.f, 42.f}, r.br()};
            const auto old_clip = layer->g.clip_rect;
            layer->override_clip_rect(viewport);
            bool load_budget = true;
            for (size_t i = 0; i <= paints.size(); ++i) {
                const float x = r.mins.x + static_cast<float>(i % 3) * (cell_width + 6.f);
                const float y = r.mins.y + 42.f + static_cast<float>(i / 3) * 78.f - skin_scroll;
                const rect cell{vec2{x, y}, vec2{x + cell_width, y + 72.f}};
                if (cell.maxs.y <= viewport.mins.y || cell.mins.y >= viewport.maxs.y) continue;
                const std::string paint = i ? paints[i - 1] : std::string{};
                layer->add_rect_filled_rounded(cell, colors.bg_bottom, 2.f);
                if (auto icon = get_weapon_icon(selected_weapon.internal, paint, load_budget); icon && icon->obj) {
                    const auto native = icon->get_size();
                    if (native.x > 0.f && native.y > 0.f) {
                        const float scale = (std::min)((cell_width - 10.f) / native.x, 48.f / native.y);
                        const vec2 image_size = native * scale, center{x + cell_width * .5f, y + 27.f};
                        layer->g.texture = icon->obj;
                        layer->add_rect_filled(rect{center - image_size * .5f, center + image_size * .5f}, color::white());
                        layer->g.texture = {};
                    }
                }
                layer->font = draw.fonts[GUI_HASH("gui_main")];
                const auto label = i ? paint : std::string("Default");
                layer->add_text(vec2{x + 4.f, y + 57.f}, label, colors.text, text_params::with_v(align_center));
            }
            layer->g.clip_rect = old_clip;
        }
        bool team_ct{true};
        float scroll{}, skin_scroll{};
        WeaponIcon selected_weapon{};
        std::vector<std::string> paints;
    };
    }

    void skins_weapon_grid(std::shared_ptr<layout>& e) {
        e->add(MAKE("skins>weapon_grid", weapon_grid, vec2{490.f, 440.f}));
    }

	void skins_weapons(std::shared_ptr<layout> &e)
	{
		//VIRTUALIZER_TIGER_WHITE_START;
		const auto search = MAKE("skins>weapons>search", text_input);
		search->size.x += 50.f;
		search->placeholder = XOR("Start typing...");
		search->callback = [](std::string &v)
		{
			if (!men.skins_loaded)
				return;

			const auto wl = ctx->find<list>(GUI_HASH("skins>weapons>list"));
			if (!wl || !wl->is_taking_input)
				return;

			wl->filter_items(v);
		};

		const auto weps = MAKE("skins>weapons>list", list, cfg.menu.sel_weapon, vec2{}, vec2{ 218.f, 210.f });
		weps->legacy_mode = true;
		weps->margin = {2.f, 0.f, 2.f, 0.f};
		/*weps->callback = [](bits& v)
		{
			if (!men.skins_loaded)
				return;

			const auto pk_s = ctx->find<text_input>(GUI_HASH("skins>paintkit>search"));
			auto s = pk_s ? pk_s->value : std::string{};
			inventorychanger::fill_skin_list(s);
		};*/

		ADD_LINE(
			"skins>weapons>search_line", search,
			MAKE("skins>weapons>search>settings", settings, [](auto e) { ADD("Autoselect held weapon", "skins>weapons>search>settings>autoselect held weapon", checkbox,
				cfg.menu.autoselect_weapon); }));
		ADD_LINE("skins>weapons>list_line", weps);
		//VIRTUALIZER_TIGER_WHITE_END;
	}

	void skins_paintkit(std::shared_ptr<layout> &e)
	{
		//VIRTUALIZER_TIGER_WHITE_START;
		const auto search = MAKE("skins>paintkit>search", text_input);
		search->size.x += 50.f;
		search->placeholder = XOR("Start typing...");
		/*search->callback = [](std::string& v)
		{
			if (!men.skins_loaded)
				return;

			inventorychanger::fill_skin_list(v);
		};*/

		const auto skins = MAKE("skins>paintkit>list", list, cfg.menu.sel_skin, vec2{}, vec2{ 218.f, 414.f });
		skins->legacy_mode = true;
		skins->margin = {2.f, 0.f, 2.f, 0.f};

		ADD_LINE(
			"skins>paintkit>search_line", search,
			MAKE("skins>weapons>search>settings", settings, [](auto e) { const auto c = MAKE("skins>paintkit>search>settings>show all skins", checkbox, cfg.menu.
					show_all_skins);
				/*c->callback = [](bool v) {
					if (!men.skins_loaded)
						return;

					std::string s{};
					inventorychanger::fill_skin_list(s);
				};*/

				ADD_INLINE("Show all skins", c); }));
		ADD_LINE("skins>paintkit>list_line", skins);
		//VIRTUALIZER_TIGER_WHITE_END;
	}

	void skins_inventory(std::shared_ptr<layout> &e)
	{
		//VIRTUALIZER_TIGER_WHITE_START;
		const auto skins = MAKE("skins>inventory>list", list, cfg.menu.sel_inv, vec2{}, vec2{ 218.f, 270.f });
		skins->legacy_mode = true;
		skins->margin = {2.f, 0.f, 2.f, 0.f};
		/*skins->callback = [](bits& v)
		{
			if (!men.skins_loaded)
				return;

			inventorychanger::update_selected(true);
		};*/

		ADD_LINE("skins>inventory>list_line", skins);

		const auto btn_remove = MAKE("skins>inventory>remove", button, XOR("Remove"));
		/*btn_remove->callback = []()
		{
			inventorychanger::remove();
		};*/

		const auto btn_reload = MAKE("skins>inventory>reload", button, XOR("Reload"));
		/*btn_reload->callback = []()
		{
			inventorychanger::load();
		};*/

		e->add(
			make_inlined(
				GUI_HASH("skins_remove_reload"), [&](auto ee)
				{
					ee->add(btn_remove);
					ee->add(btn_reload);
				}));
		//VIRTUALIZER_TIGER_WHITE_END;
	}

	std::vector<std::string> player_model = {XOR("Default"), XOR("The Elite Mr. Muhlik | Elite Crew"), XOR("Prof. Shahmat | Elite Crew"), XOR("Osiris | Elite Crew"),
											 XOR("Ground Rebel | Elite Crew"), XOR("Mr. Muhlik | Elite Crew"), XOR("Special Agent Ava | FBI"),
											 XOR("Michael Syfers | FBI Sniper"), XOR("Markus Delrow | FBI HRT"), XOR("Operator | FBI SWAT"),
											 XOR("Seal Team 6 Soldier | NSWC SEAL"), XOR("'Two Times' McCoy | USAF TACP"), XOR("Buckshot | NSWC SEAL"),
											 XOR("3rd Commando Company | KSK"), XOR("Lt. Commander Ricksaw | NSWC SEAL"), XOR("'Blueberries' Buckshot | NSWC SEAL"),
											 XOR("'Two Times' McCoy | TACP Cavalry"), XOR("Primeiro Tenente | Brazilian 1st Battalion"),
											 XOR("Cmdr. Mae 'Dead Cold' Jamison | SWAT"), XOR("1st Lieutenant Farlow | SWAT"), XOR("John 'Van Healen' Kask | SWAT"),
											 XOR("Bio-Haz Specialist | SWAT"), XOR("Sergeant Bombson | SWAT"), XOR("Chem-Haz Specialist | SWAT"),
											 XOR("Lieutenant 'Tree Hugger' Farlow | SWAT"), XOR("Maximus | Sabre"), XOR("Dragomir | Sabre"),
											 XOR("'The Doctor' Romanov | Sabre"), XOR("Rezan The Ready | Sabre"), XOR("Blackwolf | Sabre"),
											 XOR("Rezan the Redshirt | Sabre"), XOR("Dragomir | Sabre Footsoldier"), XOR("D Squadron Officer | NZSAS"),
											 XOR("B Squadron Officer | SAS"), XOR("Soldier | Phoenix"), XOR("Enforcer | Phoenix"), XOR("Slingshot | Phoenix"),
											 XOR("Street Soldier | Phoenix"), XOR("Sir Bloody Miami Darryl | The Professionals"),
											 XOR("Sir Bloody Silent Darryl | The Professionals"), XOR("Sir Bloody Skullhead Darryl | The Professionals"),
											 XOR("Sir Bloody Darryl Royale | The Professionals"), XOR("Sir Bloody Loudmouth Darryl | The Professionals"),
											 XOR("Bloody Darryl The Strapped | The Professionals"), XOR("Safecracker Voltzmann | The Professionals"),
											 XOR("Little Kev | The Professionals"), XOR("Number K | The Professionals"), XOR("Getaway Sally | The Professionals"),
											 XOR("Trapper | Guerrilla Warfare"), XOR("Trapper Aggressor | Guerrilla Warfare"),
											 XOR("Vypa Sista of the Revolution | Guerrilla Warfare"), XOR("Col. Mangos Dabisi | Guerrilla Warfare"),
											 XOR("Arno The Overgrown | Guerrilla Warfare"), XOR("'Medium Rare' Crasswater | Guerrilla Warfare"),
											 XOR("Crasswater The Forgotten | Guerrilla Warfare"), XOR("Elite Trapper Solman | Guerrilla Warfare"),
											 XOR("Cmdr. Davida 'Goggles' Fernandez | SEAL Frogman"), XOR("Cmdr. Frank 'Wet Sox' Baroud | SEAL Frogman"),
											 XOR("Lieutenant Rex Krikey | SEAL Frogman"), XOR("Sous-Lieutenant Medic | Gendarmerie Nationale"),
											 XOR("Chem-Haz Capitaine | Gendarmerie Nationale"), XOR("Chef d'Escadron Rouchard | Gendarmerie Nationale"),
											 XOR("Aspirant | Gendarmerie Nationale"), XOR("Officer Jacques Beltram | Gendarmerie Nationale"), XOR("Danger Zone A"),
											 XOR("Danger Zone B"), XOR("Danger Zone C"), XOR("Pirate"), XOR("Pirate A"), XOR("Pirate B"), XOR("Pirate C"),
											 XOR("Pirate D"), XOR("Separatist"), XOR("Separatist A"), XOR("Separatist B"), XOR("Separatist C"), XOR("Separatist D"),
											 XOR("Heavy Phoenix"), XOR("Phoenix A"), XOR("Phoenix B"), XOR("Phoenix C"), XOR("Phoenix D"), XOR("Leet A"), XOR("Leet B"),
											 XOR("Leet C"), XOR("Leet D"), XOR("Leet E"), XOR("Professional"), XOR("Professional 1"), XOR("Professional 2"),
											 XOR("Professional 3"), XOR("Professional 4"),};

	std::vector<std::string> player_mask = {XOR("Default"), XOR("Hoxton"), XOR("Porcelain Doll"), XOR("Skull"), XOR("Samurai"), XOR("Evil Clown"), XOR("Wolf"),
											XOR("Sheep"), XOR("Bunny Gold"), XOR("Anaglyph"), XOR("Porcelain Doll Kabuki"), XOR("Dallas"), XOR("Pumpkin"),
											XOR("Sheep Bloody"), XOR("Devil Plastic"), XOR("Boar"), XOR("Chains"), XOR("Tiki"), XOR("Bunny"), XOR("Sheep Gold"),
											XOR("Zombie Fortune Plastic"), XOR("Chicken"), XOR("Skull Gold"), XOR("TF2 Demoman"), XOR("TF2 Engineer"), XOR("TF2 Heavy"),
											XOR("TF2 Medic"), XOR("TF2 Pyro"), XOR("TF2 Scout"), XOR("TF2 Sniper"), XOR("TF2 Soldier"), XOR("TF2 Spy"),};

	void skins_agents(std::shared_ptr<layout> &e)
	{
		//VIRTUALIZER_TIGER_WHITE_START;
		const auto agent_t = MAKE("skins>agents>agent t", combo_box_agent, cfg.visuals.model_t, vec2{ 300.f, 460.f });
		agent_t->legacy_mode = true;
		const auto agent_ct = MAKE("skins>agents>agent ct", combo_box_agent, cfg.visuals.model_ct, vec2{ 300.f, 460.f });
		agent_ct->legacy_mode = true;

		for (int i{}; i < player_model.size(); ++i)
		{
			auto &m = player_model[i];
			agent_t->add(MAKE_RUNTIME(XOR( "skins>agents>agent t>" ) + m, selectable, m, i));
			agent_ct->add(MAKE_RUNTIME(XOR( "skins>agents>agent ct>" ) + m, selectable, m, i));
		}

		const auto facemask_t = MAKE("skins>agents>facemask t", combo_box_agent, cfg.visuals.mask_t, vec2{ 140.f, 300.f });
		facemask_t->legacy_mode = true;
		const auto facemask_ct = MAKE("skins>agents>facemask ct", combo_box_agent, cfg.visuals.mask_ct, vec2{ 140.f, 300.f });
		facemask_ct->legacy_mode = true;

		for (int i{}; i < player_mask.size(); ++i)
		{
			auto &m = player_mask[i];
			facemask_t->add(MAKE_RUNTIME(XOR( "skins>agents>facemask t>" ) + m, selectable, m, i));
			facemask_ct->add(MAKE_RUNTIME(XOR( "skins>agents>facemask ct>" ) + m, selectable, m, i));
		}

		ADD_INLINE("Agent CT", agent_ct);
		ADD_INLINE("Agent T", agent_t);
		ADD_INLINE("Facemask CT", facemask_ct);
		ADD_INLINE("Facemask T", facemask_t);
		//VIRTUALIZER_TIGER_WHITE_END;
	}

	void skins_options(std::shared_ptr<layout> &e)
	{
		//VIRTUALIZER_TIGER_WHITE_START;
		ADD("Seed", "skins>options>seed", slider<int>, 0, 1000, cfg.menu.seed);
		ADD("Wear", "skins>options>wear", slider<int>, 0, 100, cfg.menu.wear, XOR("%d%%"));
		ADD("StatTrak", "skins>options>stattrak", slider<int>, -1, 10000, cfg.menu.stat_trak, XOR("%d kills"));

		ADD("Equip CT", "skins>options>equip ct", checkbox, cfg.menu.equip_ct);
		ADD("Equip T", "skins>options>equip t", checkbox, cfg.menu.equip_t);

		const auto btn_add = MAKE("skins>options>add", button, XOR("Add"));
		/*btn_add->callback = []()
		{
			inventorychanger::add();
		};*/

		const auto btn_save = MAKE("skins>options>save", button, XOR("Save"));
		/*btn_save->callback = []()
		{
			inventorychanger::save();
		};*/

		e->add(
			make_inlined(
				GUI_HASH("skins_add_save"), [&](auto ee)
				{
					ee->add(btn_save);
					ee->add(btn_add);
				}));
		//VIRTUALIZER_TIGER_WHITE_END;
	}
}
