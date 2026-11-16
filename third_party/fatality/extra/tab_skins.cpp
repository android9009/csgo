#include "menu.h"

#include "../cfg.h"
#include "gui/controls/combo_box_agent.h"
#include "macros.h"
#include "../../../sdk/interfaces/interfaces.hpp"
#include "../../../sdk/skin_icon_file.hpp"
#include "../../../sdk/skin_catalog.hpp"
#include "../../../sdk/skin_schema.hpp"
#include "../../../features/skins/skin_state.hpp"
#include "../../../features/skins/local_inventory.hpp"
#include "../../../features/skins/preview_state.hpp"
#include "../../../features/skins/equipment_state.hpp"
#include "../preview_bridge.hpp"
#include "../preview_catalog.hpp"
#include "../skin_names.hpp"
#include "../equipment_paints.hpp"
#include "../../../utils/log.hpp"
#include <algorithm>
#include <array>
#include <map>
#include <vector>
#include <cstdio>
#include <cctype>
#include <cmath>
#include <ren/misc.h>

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
    std::shared_ptr<texture> get_asset_icon(const std::string& path, bool& load_budget) {
        const std::string key = path;
        auto& item = icons[key];
        item.last_used = ++icon_clock;
        if (!item.attempted && load_budget && Interfaces::BaseFileSystem) {
            load_budget = false; // decode at most one newly visible image per frame
            item.attempted = true;
            item.bytes = SkinIconFile::Read(Interfaces::BaseFileSystem, path);
            if (!item.bytes.empty()) {
                item.image = std::make_shared<texture>(item.bytes.data(), static_cast<uint32_t>(item.bytes.size()));
                item.image->create();
                draw.textures[hash(std::string("skins>weapon_icon>") + key)] = item.image;
            }
        }
        return item.image;
    }
    bool icon_exists(const std::string& path) {
        if (!Interfaces::BaseFileSystem || path.empty()) return false;
        using Open=void*(__thiscall*)(void*,const char*,const char*,const char*);
        using Close=void(__thiscall*)(void*,void*);
        auto* fs=Interfaces::BaseFileSystem;
        void* file=SkinIconFile::Slot<Open>(fs,2)(fs,path.c_str(),"rb","GAME");
        if (!file) return false;
        SkinIconFile::Slot<Close>(fs,3)(fs,file); return true;
    }
    std::string find_finish_icon(const std::string& item,const std::string& paint) {
        if (!SkinIconPath::ValidName(item) || !SkinIconPath::ValidName(paint)) return {};
        for (const auto* suffix : {"_light_large.png","_light.png","_medium.png","_large.png"}) {
            const auto path="resource/flash/econ/default_generated/"+item+"_"+paint+suffix;
            if (icon_exists(path)) return path;
        }
        return {};
    }
    std::map<std::string,std::string> resolved_icon_paths;
    std::shared_ptr<texture> get_weapon_icon(const char* weapon, const std::string& paint, bool& load_budget) {
        if (!weapon || !Interfaces::BaseFileSystem) return {};
        const std::string item=weapon,key=item+"/"+paint;
        auto it=resolved_icon_paths.find(key);
        if (it == resolved_icon_paths.end()) {
            std::string path;
            if (!paint.empty()) {
                path=find_finish_icon(item,paint);
                if (path.empty()) {
                    for (const auto& pair : EquipmentPaints::entries) if (pair.paint == paint) {
                        path=find_finish_icon(std::string(pair.item),paint);
                        if (!path.empty()) break;
                    }
                }
            }
            else {
                path=SkinIconPath::Base(item);
                if (!icon_exists(path)) {
                    path.clear();
                    // Choose the first PNG actually present in this installation,
                    // not the first (potentially CS2-only) paint in the API list.
                    for (const auto& pair : EquipmentPaints::entries) if (pair.item == item) {
                        path=find_finish_icon(item,std::string(pair.paint));
                        if (!path.empty()) break;
                    }
                }
            }
            if (resolved_icon_paths.size()>4096) resolved_icon_paths.clear();
            it=resolved_icon_paths.emplace(key,std::move(path)).first;
        }
        return it->second.empty() ? std::shared_ptr<texture>{} : get_asset_icon(it->second,load_budget);
    }
    SkinCatalog::Catalog catalog;
    SkinSchema::Metadata schema_metadata;
    const char* filter_labels[] = {"All", "Name A-Z", "Name Z-A", "Consumer", "Industrial", "Mil-Spec", "Restricted", "Classified", "Covert", "Contraband", "Unknown"};
    const char* filter_rarity[] = {"", "", "", "common", "uncommon", "rare", "mythical", "legendary", "ancient", "immortal", "?"};
    bool catalog_loaded{};
    bool show_all_paints{};
    bool load_catalog() {
        if (catalog_loaded) return true;
        if (!Interfaces::BaseFileSystem) return false;
        const auto data = SkinIconFile::Read(Interfaces::BaseFileSystem, "scripts/items/items_game.txt", 64u * 1024u * 1024u);
        if (data.empty()) {
            LOG_WARN("skin menu: unable to read scripts/items/items_game.txt");
            return false;
        }
        const std::string_view schema(reinterpret_cast<const char*>(data.data()), data.size());
        catalog = SkinCatalog::Parse(schema);
        schema_metadata = SkinSchema::Parse(schema);
        catalog_loaded = !catalog.empty();
        LOG_INFO("skin menu: catalogue loaded for %zu weapons, %zu paint kit IDs", catalog.size(), schema_metadata.paint_ids.size());
        return catalog_loaded;
    }
    struct SkinChoice {
        std::string paint, custom_name;
        int seed{}, stattrak_count{};
        float wear{};
        bool stattrak{}, souvenir{};
        int stattrak_mode{}; // 0: original item, 1: static counter, 2: none
        std::array<SkinSchema::Decal, 5> stickers{};
    };
    std::map<int, std::array<SkinSchema::Decal, 5>> agent_patches[2];
    std::map<std::string, SkinChoice> selection[2]; // independent CT/T menu state
    const std::string& selected_equipment_paint(const char* weapon, bool ct) {
        static const std::string empty;
        if (!weapon) return empty;
        const auto& chosen = selection[ct ? 0 : 1];
        const auto it = chosen.find(weapon);
        return it == chosen.end() ? empty : it->second.paint;
    }
    enum class Equipment { None, Agent, Gloves, Knife, Medal, Music };
    Equipment equipment_screen = Equipment::None;
    std::string equipped_knife[2], equipped_gloves[2], equipped_music[2], equipped_medal[2];
    struct EquipmentItem { const char* label; const char* icon; };
    const std::vector<EquipmentItem> knife_items = {
        {"Default", ""}, {"Bayonet", "weapon_bayonet"}, {"Classic Knife", "weapon_knife_css"},
        {"Flip Knife", "weapon_knife_flip"}, {"Gut Knife", "weapon_knife_gut"}, {"Karambit", "weapon_knife_karambit"},
        {"M9 Bayonet", "weapon_knife_m9_bayonet"}, {"Huntsman", "weapon_knife_tactical"}, {"Falchion", "weapon_knife_falchion"},
        {"Bowie", "weapon_knife_survival_bowie"}, {"Butterfly", "weapon_knife_butterfly"}, {"Shadow Daggers", "weapon_knife_push"},
        {"Ursus", "weapon_knife_ursus"}, {"Navaja", "weapon_knife_gypsy_jackknife"}, {"Stiletto", "weapon_knife_stiletto"},
        {"Talon", "weapon_knife_widowmaker"}, {"Skeleton", "weapon_knife_skeleton"}, {"Survival", "weapon_knife_canis"},
        {"Paracord", "weapon_knife_cord"}, {"Nomad", "weapon_knife_outdoor"}
    };
    const std::vector<EquipmentItem> glove_items = {
        {"Default", ""}, {"Bloodhound", "studded_bloodhound_gloves"}, {"Sport", "sporty_gloves"},
        {"Driver", "slick_gloves"}, {"Hand Wraps", "leather_handwraps"}, {"Moto", "motorcycle_gloves"},
        {"Specialist", "specialist_gloves"}, {"Hydra", "studded_hydra_gloves"}
    };
    // Sample CS:GO-era music kits. Paths reference files in the installed game.
    const std::vector<EquipmentItem> music_items = {
        {"Default", ""},
        {"Valve, CS:GO", "resource/flash/econ/music_kits/valve_02.png"},
        {"Daniel Sadowski, Crimson Assault", "resource/flash/econ/music_kits/danielsadowski_01.png"},
        {"Noisia, Sharpened", "resource/flash/econ/music_kits/noisia_01.png"},
        {"Robert Allaire, Insurgency", "resource/flash/econ/music_kits/robertallaire_01.png"},
        {"Sean Murray, A*D*8", "resource/flash/econ/music_kits/seanmurray_01.png"},
        {"Feed Me, High Noon", "resource/flash/econ/music_kits/feedme_01.png"},
        {"Dren, Death's Head Demolition", "resource/flash/econ/music_kits/dren_01.png"},
        {"Austin Wintory, Desert Fire", "resource/flash/econ/music_kits/austinwintory_01.png"},
        {"Sasha, LNOE", "resource/flash/econ/music_kits/sasha_01.png"},
        {"Skog, Metal", "resource/flash/econ/music_kits/skog_01.png"},
        {"Midnight Riders, All I Want for Christmas", "resource/flash/econ/music_kits/midnightriders_01.png"},
        {"Matt Lange, IsoRhythm", "resource/flash/econ/music_kits/mattlange_01.png"},
        {"Mateo Messina, For No Mankind", "resource/flash/econ/music_kits/mateomessina_01.png"},
        {"Various Artists, Hotline Miami", "resource/flash/econ/music_kits/hotlinemiami_01.png"},
        {"Daniel Sadowski, Total Domination", "resource/flash/econ/music_kits/danielsadowski_02.png"},
        {"Damjan Mravunac, The Talos Principle", "resource/flash/econ/music_kits/damjanmravunac_01.png"},
        {"Proxy, Battlepack", "resource/flash/econ/music_kits/proxy_01.png"},
        {"Ki:Theory, MOLOTOV", "resource/flash/econ/music_kits/kitheory_01.png"},
        {"Troels Folmann, Uber Blasto Phone", "resource/flash/econ/music_kits/troelsfolmann_01.png"},
        {"Kelly Bailey, Hazardous Environments", "resource/flash/econ/music_kits/kellybailey_01.png"},
        {"Skog, II-Headshot", "resource/flash/econ/music_kits/skog_02.png"},
        {"Daniel Sadowski, The 8-Bit Kit", "resource/flash/econ/music_kits/danielsadowski_03.png"},
        {"AWOLNATION, I Am", "resource/flash/econ/music_kits/awolnation_01.png"},
        {"Mord Fustang, Diamonds", "resource/flash/econ/music_kits/mordfustang_01.png"},
        {"Michael Bross, Invasion!", "resource/flash/econ/music_kits/michaelbross_01.png"},
        {"Ian Hultquist, Lion's Mouth", "resource/flash/econ/music_kits/ianhultquist_01.png"},
        {"New Beat Fund, Sponge Fingerz", "resource/flash/econ/music_kits/newbeatfund_01.png"},
        {"Beartooth, Disgusting", "resource/flash/econ/music_kits/beartooth_01.png"},
        {"Lennie Moore, Java Havana Funkaloo", "resource/flash/econ/music_kits/lenniemoore_01.png"},
        {"Darude, Moments CS:GO", "resource/flash/econ/music_kits/darude_01.png"},
        {"The Verkkars, EZ4ENCE", "resource/flash/econ/music_kits/theverkkars_01.png"},
        {"Halo, The Master Chief Collection", "resource/flash/econ/music_kits/halo_01.png"},
        {"Scarlxrd: King, Scar", "resource/flash/econ/music_kits/scarlxrd_01.png"},
        {"Half-Life: Alyx, Anti-Citizen", "resource/flash/econ/music_kits/hlalyx_01.png"},
        {"Austin Wintory, Bachram", "resource/flash/econ/music_kits/austinwintory_02.png"},
        {"Dren, Gunman Taco Truck", "resource/flash/econ/music_kits/dren_02.png"},
        {"Daniel Sadowski, Eye of the Dragon", "resource/flash/econ/music_kits/danielsadowski_04.png"},
        {"Tree Adams and Ben Bromfield, M.U.D.D. FORCE", "resource/flash/econ/music_kits/treeadams_benbromfield_01.png"},
        {"Tim Huling, Neo Noir", "resource/flash/econ/music_kits/timhuling_01.png"},
        {"Sam Marshall, Bodacious", "resource/flash/econ/music_kits/sammarshall_01.png"},
        {"Matt Levine, Drifter", "resource/flash/econ/music_kits/mattlevine_01.png"},
        {"Amon Tobin, All for Dust", "resource/flash/econ/music_kits/amontobin_01.png"},
        {"Darren Korb, Hades Music Kit", "resource/flash/econ/music_kits/hades_01.png"},
        {"Neck Deep, The Lowlife Pack", "resource/flash/econ/music_kits/neckdeep_02.png"},
        {"Scarlxrd, CHAIN$AW.LXADXUT.", "resource/flash/econ/music_kits/scarlxrd_02.png"},
        {"Austin Wintory, Mocha Petal", "resource/flash/econ/music_kits/austinwintory_03.png"},
        {"Chipzel, ~Yellow Magic~", "resource/flash/econ/music_kits/chipzel_01.png"},
        {"Freaky DNA, Vici", "resource/flash/econ/music_kits/freakydna_01.png"},
        {"Jesse Harlin, Astro Bellum", "resource/flash/econ/music_kits/jesseharlin_01.png"},
        {"Laura Shigihara: Work Hard, Play Hard", "resource/flash/econ/music_kits/laurashigihara_01.png"},
        {"Sarah Schachner, KOLIBRI", "resource/flash/econ/music_kits/sarahschachner_01.png"},
        {"bbno$, u mad!", "resource/flash/econ/music_kits/bbnos_01.png"},
        {"The Verkkars & n0thing, Flashbang Dance", "resource/flash/econ/music_kits/theverkkars_02.png"},
        {"3kliksphilip, Heading for the Source", "resource/flash/econ/music_kits/3kliksphilip_01.png"},
        {"Humanity's Last Breath, Void", "resource/flash/econ/music_kits/hlb_01.png"},
        {"Juelz, Shooters", "resource/flash/econ/music_kits/juelz_01.png"},
        {"Knock2, dashstar*", "resource/flash/econ/music_kits/knock2_01.png"},
        {"Meechy Darko, Gothic Luxury", "resource/flash/econ/music_kits/meechydarko_01.png"},
        {"Sullivan King, Lock Me Up", "resource/flash/econ/music_kits/sullivanking_01.png"},
        {"Perfect World, 花脸 Hua Lian (Painted Face)", "resource/flash/econ/music_kits/perfectworld_01.png"},
        {"Denzel Curry, ULTIMATE", "resource/flash/econ/music_kits/denzelcurry_01.png"},
        {"Valve, CS:GO", "resource/flash/econ/music_kits/valve_01.png"},
        {"DRYDEN, Feel The Power", "resource/flash/econ/music_kits/dryden_01.png"},
        {"ISOxo, inhuman", "resource/flash/econ/music_kits/isoxo_01.png"},
        {"KILL SCRIPT, All Night", "resource/flash/econ/music_kits/killscript_01.png"},
        {"Knock2, Make U SWEAT!", "resource/flash/econ/music_kits/knock2_02.png"},
        {"Rad Cat, Reason", "resource/flash/econ/music_kits/radcat_01.png"},
        {"TWERL, Ekko & Sidetrack, Under Bright Lights", "resource/flash/econ/music_kits/twerl_01.png"},
        {"Austin Wintory, The Devil Went Clubbing In Georgia", "resource/flash/econ/music_kits/austinwintory_04.png"},
        {"Ben Bromfield, Rabbit Hole", "resource/flash/econ/music_kits/benbromfield_01.png"},
        {"Daniel Sadowski, Dead Shot", "resource/flash/econ/music_kits/danielsadowski_05.png"},
    };
    const std::vector<EquipmentItem> medal_items{
        {"Default", ""},
#include "../medal_catalog.hpp"
    };
    const std::vector<EquipmentItem> default_item{{"Default", ""}};
    std::vector<EquipmentItem> make_agents(bool ct) {
        std::vector<EquipmentItem> result;
        for (size_t i = 0; i < PreviewCatalog::AgentCount(ct); ++i) {
            const auto& a = PreviewCatalog::GetAgent(ct, i);
            result.push_back({a.name, a.icon});
        }
        return result;
    }
    const auto ct_agents = make_agents(true), t_agents = make_agents(false);
    const char* equipment_title(Equipment screen) {
        switch (screen) { case Equipment::Agent: return "AGENT"; case Equipment::Knife: return "KNIFE";
            case Equipment::Medal: return "MEDAL"; case Equipment::Gloves: return "GLOVES"; case Equipment::Music: return "MUSIC"; default: return ""; }
    }
    const std::vector<EquipmentItem>& equipment_items(Equipment screen, bool ct) {
        if (screen == Equipment::Agent) return ct ? ct_agents : t_agents;
        if (screen == Equipment::Knife) return knife_items;
        if (screen == Equipment::Gloves) return glove_items;
        if (screen == Equipment::Music) return music_items;
        if (screen == Equipment::Medal) return medal_items;
        return default_item;
    }
    const char* equipment_icon(Equipment screen, const std::string& equipped, bool ct) {
        if (!equipped.empty() && screen != Equipment::Music && screen != Equipment::Agent && screen != Equipment::Medal) return equipped.c_str();
        if (screen == Equipment::Knife) return ct ? "weapon_knife" : "weapon_knife_t";
        if (screen == Equipment::Gloves) return ct ? "ct_gloves" : "t_gloves";
        return nullptr;
    }
    void draw_symbol(std::shared_ptr<evo::ren::layer>& layer, Equipment screen, vec2 center) {
        if (screen == Equipment::Agent) {
            layer->add_circle(center + vec2{0.f, -7.f}, 6.f, colors.text_dark, 24, 1.f, 2.f);
            layer->add_line(center + vec2{-12.f, 14.f}, center + vec2{-8.f, 3.f}, colors.text_dark, 3.f);
            layer->add_line(center + vec2{-8.f, 3.f}, center + vec2{8.f, 3.f}, colors.text_dark, 3.f);
            layer->add_line(center + vec2{8.f, 3.f}, center + vec2{12.f, 14.f}, colors.text_dark, 3.f);
        } else if (screen == Equipment::Music) {
            layer->add_circle(center, 11.f, colors.text_dark, 24, 1.f, 2.f);
            layer->add_circle(center, 3.f, colors.text_dark, 24, 1.f, 2.f);
        } else if (screen == Equipment::Medal) {
            layer->add_circle(center, 11.f, colors.text_dark, 24, 1.f, 2.f);
            layer->add_circle(center, 4.f, colors.text_dark, 24, 1.f, 2.f);
        }
    }
    void draw_equipment_icon(std::shared_ptr<evo::ren::layer>& layer, Equipment screen, const std::string& selected,
                             bool ct, vec2 center, vec2 bounds, bool& budget, const std::string& paint = {}) {
        const auto name = equipment_icon(screen, selected, ct);
        if (name || ((screen == Equipment::Music || screen == Equipment::Agent || screen == Equipment::Medal) && !selected.empty())) {
            const auto path = screen == Equipment::Agent ? "resource/flash/econ/characters/" + selected + ".png" : selected;
            auto icon = (screen == Equipment::Music || screen == Equipment::Agent || screen == Equipment::Medal) ? get_asset_icon(path, budget) : get_weapon_icon(name, paint, budget);
            if (icon && icon->obj) {
                const auto native = icon->get_size();
                if (native.x > 0.f && native.y > 0.f) {
                    const auto scale = (std::min)(bounds.x / native.x, bounds.y / native.y);
                    const auto size = native * scale;
                    layer->g.texture = icon->obj;
                    layer->add_rect_filled(rect{center - size * .5f, center + size * .5f}, color::white());
                    layer->g.texture = {};
                }
            }
        } else if (screen != Equipment::Music && screen != Equipment::Medal) draw_symbol(layer, screen, center);
    }
    class equipment_tiles final : public control {
    public:
        equipment_tiles(control_id id, const vec2& size) : control(id, {}, size) {}
        void render() override {
            control::render();
            if (!is_visible) return;
            trim_icons(); // before the grid submits its draw commands
            auto& layer = draw.layers[ctx->content_layer];
            const auto r = area_abs();
            const bool ct = FatalityPreviewBridge::skins_team_ct.load(std::memory_order_relaxed);
            const int team = ct ? 0 : 1;
            const Equipment entries[] = {Equipment::Agent, Equipment::Gloves, Equipment::Knife, Equipment::Medal, Equipment::Music};
            const float width = (r.width() - 12.f) / 3.f;
            bool budget = true;
            for (int i = 0; i < 5; ++i) {
                const auto pos = r.tl() + vec2{static_cast<float>(i % 3) * (width + 6.f), static_cast<float>(i / 3) * 54.f};
                const rect card{pos, pos + vec2{width, 50.f}};
                layer->add_rect_filled_rounded(card, equipment_screen == entries[i] ? colors.bg_block_light : colors.bg_bottom, 2.f);
                const std::string empty;
                const auto& selected = entries[i] == Equipment::Knife ? equipped_knife[team] : entries[i] == Equipment::Gloves ? equipped_gloves[team] : entries[i] == Equipment::Music ? equipped_music[team] : entries[i] == Equipment::Medal ? equipped_medal[team] : empty;
                const auto& agent = PreviewCatalog::GetAgent(ct, static_cast<size_t>(FatalityPreviewBridge::agent[team].load(std::memory_order_relaxed)));
                if (entries[i] == Equipment::Knife && card.contains(input.cursor()) && equipment_screen == Equipment::None)
                    FatalityPreviewBridge::weapon.store(PreviewCatalog::WeaponIndex(selected.empty() ? (ct ? "weapon_knife" : "weapon_knife_t") : selected.c_str()), std::memory_order_relaxed);
                draw_equipment_icon(layer, entries[i], entries[i] == Equipment::Agent ? std::string(agent.icon) : selected, ct, pos + vec2{width * .5f, 18.f}, vec2{width - 6.f, 30.f}, budget, entries[i] == Equipment::Knife ? selected_equipment_paint(selected.c_str(), ct) : std::string{});
                layer->font = draw.fonts[GUI_HASH("gui_main")];
                layer->add_text(pos + vec2{width * .5f, 40.f}, equipment_title(entries[i]), colors.text, text_params::with_vh(align_center, align_center));
                layer->add_rect_filled(rect{pos + vec2{3.f, 47.f}, pos + vec2{width - 3.f, 49.f}}, colors.outline);
            }
        }
        void on_mouse_down(char key) override {
            if (key != m_left) return;
            const auto local = input.cursor() - area_abs().tl();
            const float width = (size.x - 12.f) / 3.f;
            if (local.y < 0.f || local.y >= 104.f || local.x < 0.f) return;
            const int col = static_cast<int>(local.x / (width + 6.f));
            const int row = static_cast<int>(local.y / 54.f);
            const int index = row * 3 + col;
            if (col < 0 || col >= 3 || index >= 5 || local.x - static_cast<float>(col) * (width + 6.f) >= width || local.y - static_cast<float>(row) * 54.f >= 50.f) return;
            const Equipment entries[] = {Equipment::Agent, Equipment::Gloves, Equipment::Knife, Equipment::Medal, Equipment::Music};
            equipment_screen = entries[index];
            FatalityPreviewBridge::skins_selecting.store(true, std::memory_order_relaxed);
        }
    };

    class weapon_grid final : public control {
    public:
        weapon_grid(control_id id, const vec2& size) : control(id, {}, size) {}
        void render() override {
            control::render();
            if (!is_visible) return;
            auto& layer = draw.layers[ctx->content_layer];
            const auto r = area_abs();
            if (equipment_screen != last_equipment_screen) {
                equipment_scroll = 0.f; options_open = false; filter_open = false; search_query.clear(); filter_index = 0;
                agent_editor = equipment_screen == Equipment::Agent;
                if (agent_editor) {
                    agent_choice = FatalityPreviewBridge::agent[team_ct ? 0 : 1].load(std::memory_order_relaxed);
                    pending.stickers = agent_patches[team_ct ? 0 : 1][agent_choice];
                }
                last_equipment_screen = equipment_screen;
            }
            if (decal_picker >= 0) { render_decal_picker(layer, r); return; }
            if (equipment_screen != Equipment::None) { selected_weapon = {}; render_equipment(layer, r); return; }
            if (selected_weapon.internal) { render_skins(layer, r); return; }
            const auto ct = rect(r.tl(), r.tl() + vec2{72.f, 25.f});
            const auto t = rect(r.tl() + vec2{78.f, 0.f}, r.tl() + vec2{150.f, 25.f});
            layer->add_rect_filled_rounded(ct, team_ct ? colors.bg_block_light : colors.bg_bottom, 2.f);
            layer->add_rect_filled_rounded(t, !team_ct ? colors.bg_block_light : colors.bg_bottom, 2.f);
            layer->font = draw.fonts[GUI_HASH("gui_main")];
            layer->add_text(ct.center(), "CT", team_ct ? colors.accent : colors.text_dark, text_params::with_vh(align_center, align_center));
            layer->add_text(t.center(), "T", !team_ct ? colors.accent : colors.text_dark, text_params::with_vh(align_center, align_center));
            if (ctx->res.icons.settings) {
                layer->g.texture = ctx->res.icons.settings->obj;
                layer->add_rect_filled(rect{vec2{r.maxs.x - 23.f, r.mins.y + 3.f}, vec2{r.maxs.x - 3.f, r.mins.y + 23.f}}, colors.text);
                layer->g.texture = {};
            }
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
                    if (viewport.contains(input.cursor()) && cell.contains(input.cursor()))
                        FatalityPreviewBridge::weapon.store(PreviewCatalog::WeaponIndex(weapon.internal), std::memory_order_relaxed);
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
            if (catalog_settings_open) {
                const rect popup{vec2{r.maxs.x - 210.f, r.mins.y + 28.f}, vec2{r.maxs.x, r.mins.y + 96.f}};
                layer->add_rect_filled_rounded(popup, colors.bg_block_light, 3.f);
                const rect check{popup.mins + vec2{10.f, 12.f}, popup.mins + vec2{22.f, 24.f}};
                layer->add_rect_filled(check, show_all_paints ? colors.accent : colors.outline);
                layer->add_text(popup.mins + vec2{32.f, 19.f}, "Show all skins", colors.text, text_params::with_v(align_center));
                const rect share{popup.mins + vec2{10.f, 42.f}, popup.mins + vec2{22.f, 54.f}};
                layer->add_rect_filled(share, LocalInventory::enabled.load() ? colors.accent : colors.outline);
                layer->add_text(popup.mins + vec2{32.f, 49.f}, "Share to inventory", colors.text, text_params::with_v(align_center));
            }
        }
        void on_mouse_down(char key) override {
            const auto local = input.cursor() - area_abs().tl();
            if (key != m_left) return;
            const float cell_width = (size.x - 12.f) / 3.f;
            if (decal_picker >= 0) { decal_picker_click(local); return; }
            if (equipment_screen != Equipment::None) {
                if (options_open && options_click(local)) return;
                if (local.y >= 0.f && local.y < 30.f && local.x >= size.x - 35.f) {
                    equipment_screen = Equipment::None;
                    FatalityPreviewBridge::skins_selecting.store(false, std::memory_order_relaxed);
                    return;
                }
                if (toolbar_click(local)) return;
                const float list_top = 72.f;
                if (local.y < list_top || local.x < 0.f) return;
                const int col = static_cast<int>(local.x / (cell_width + 6.f));
                const int row = static_cast<int>((local.y - list_top + equipment_scroll) / 78.f);
                if (col < 0 || col >= 3 || row < 0 || local.x - static_cast<float>(col) * (cell_width + 6.f) >= cell_width) return;
                const auto& items = equipment_items(equipment_screen, team_ct);
                const auto visible_items = filtered_equipment();
                const size_t visible_index = static_cast<size_t>(row * 3 + col);
                if (visible_index >= visible_items.size()) return;
                const size_t index = visible_items[visible_index];
                const int team = team_ct ? 0 : 1;
                if (equipment_screen == Equipment::Agent) {
                    agent_choice = static_cast<int>(index);
                    FatalityPreviewBridge::agent[team].store(agent_choice, std::memory_order_relaxed);
                    EquipmentState::Agent(team_ct,index==0 ? std::string{} : PreviewCatalog::GetAgent(team_ct,index).model);
                    pending.stickers = agent_patches[team][agent_choice];
                    return;
                }
                if ((equipment_screen == Equipment::Knife || equipment_screen == Equipment::Gloves) && index != 0) {
                    const auto item = items[index];
                    const auto kind = equipment_screen;
                    if (kind == Equipment::Knife) {
                        equipped_knife[team] = item.icon;
                        FatalityPreviewBridge::weapon.store(PreviewCatalog::WeaponIndex(item.icon),std::memory_order_relaxed);
                    } else equipped_gloves[team] = item.icon;
                    selected_weapon = {item.label, item.icon};
                    equipment_weapon = true;
                    paints.clear(); search_query.clear(); reverse_sort = false; filter_open = false; filter_index = 0;
                    load_catalog();
                    for (const auto& pair : EquipmentPaints::entries)
                        if (pair.item == item.icon && schema_metadata.paint_ids.count(std::string(pair.paint))) paints.emplace_back(pair.paint);
                    skin_scroll = 0.f; options_open = false; stat_menu_open = false; agent_editor = false;
                    const auto& saved = selection[team];
                    const auto found = saved.find(item.icon);
                    pending = found == saved.end() ? SkinChoice{} : found->second;
                    commit_weapon();
                    equipment_screen = Equipment::None;
                    return;
                }
                if (equipment_screen == Equipment::Knife) { equipped_knife[team] = items[index].icon; EquipmentState::Select(team_ct,false,items[index].icon); }
                if (equipment_screen == Equipment::Gloves) { equipped_gloves[team] = items[index].icon; EquipmentState::Select(team_ct,true,items[index].icon); }
                if (equipment_screen == Equipment::Music) equipped_music[team] = items[index].icon;
                if (equipment_screen == Equipment::Medal) equipped_medal[team] = items[index].icon;
                equipment_screen = Equipment::None;
                equipment_scroll = 0.f;
                FatalityPreviewBridge::skins_selecting.store(false, std::memory_order_relaxed);
                return;
            }
            if (selected_weapon.internal) { weapon_screen_click(local); return; }
            if (local.y >= 0.f && local.y < 25.f && local.x >= size.x - 28.f) {
                catalog_settings_open = !catalog_settings_open;
                return;
            }
            if (catalog_settings_open) {
                if (local.x >= size.x - 210.f && local.x <= size.x) {
                    if (local.y >= 28.f && local.y < 66.f) show_all_paints = !show_all_paints;
                    else if (local.y >= 66.f && local.y < 96.f)
                        LocalInventory::enabled.store(!LocalInventory::enabled.load());
                }
                catalog_settings_open = false;
                return;
            }
            if (local.y >= 0.f && local.y < 25.f) {
                if (local.x >= 0.f && local.x < 72.f) { team_ct = true; scroll = 0.f; }
                else if (local.x >= 78.f && local.x < 150.f) { team_ct = false; scroll = 0.f; }
                FatalityPreviewBridge::skins_team_ct.store(team_ct, std::memory_order_relaxed);
                return;
            }
            if (local.y < 55.f || local.x < 0.f) return;
            const int col = static_cast<int>(local.x / (cell_width + 6.f));
            const int row = static_cast<int>((local.y - 55.f + scroll) / 78.f);
            if (col < 0 || col >= 3 || row < 0 || local.x - static_cast<float>(col) * (cell_width + 6.f) >= cell_width) return;
            const auto& columns = team_ct ? ct_weapons : t_weapons;
            if (static_cast<size_t>(row) >= columns[col].size()) return;
            selected_weapon = columns[col][row]; equipment_weapon = false; agent_editor = false;
            paints.clear(); search_query.clear(); reverse_sort = false; filter_open = false; filter_index = 0;
            if (load_catalog()) {
                auto it = catalog.find(selected_weapon.internal);
                if (it != catalog.end()) paints = it->second;
            }
            skin_scroll = 0.f; options_open = false; stat_menu_open = false; agent_editor = false;
            const auto& saved = selection[team_ct ? 0 : 1];
            const auto saved_it = saved.find(selected_weapon.internal);
            pending = saved_it == saved.end() ? SkinChoice{} : saved_it->second;
            FatalityPreviewBridge::skins_selecting.store(true, std::memory_order_relaxed);
        }
        void on_mouse_wheel(float delta) override {
            input.consume_wheel();
            if (decal_picker >= 0) {
                const float rows = static_cast<float>((visible_decals().size() + 2) / 3);
                decal_scroll = std::clamp(decal_scroll - delta * 55.f, 0.f, (std::max)(0.f, rows * 78.f - (size.y - 42.f)));
                return;
            }
            if (options_open) {
                if (agent_editor) return;
                const auto local = input.cursor() - area_abs().tl() - vec2{size.x-324.f,0.f};
                if (local.x >= 120.f && local.x <= 300.f) {
                    if (local.y >= 90.f && local.y < 120.f) pending.seed = std::clamp(pending.seed + (delta > 0.f ? 1 : -1), 0, 1000);
                    else if (local.y >= 132.f && local.y < 162.f) pending.wear = std::clamp(pending.wear + (delta > 0.f ? .01f : -.01f), 0.f, 1.f);
                    else if (local.y >= 300.f && local.y < 330.f && pending.stattrak_mode == 1 && !pending.souvenir) pending.stattrak_count = std::clamp(pending.stattrak_count + (delta > 0.f ? 1 : -1), 0, 99999);
                    else return;
                    if (!agent_editor) commit_weapon();
                }
                return;
            }
            if (equipment_screen != Equipment::None) {
                const float rows = static_cast<float>((filtered_equipment().size() + 2) / 3);
                const float max_scroll = (std::max)(0.f, rows * 78.f - (size.y - 72.f));
                equipment_scroll = std::clamp(equipment_scroll - delta * 55.f, 0.f, max_scroll);
            } else if (selected_weapon.internal) {
                const float rows = static_cast<float>((filtered_paints().size() + 4) / 5);
                const float max_scroll = (std::max)(0.f, rows * 78.f - (size.y - 72.f));
                skin_scroll = std::clamp(skin_scroll - delta * 55.f, 0.f, max_scroll);
            } else {
                const float viewport_height = (std::max)(0.f, size.y - 55.f);
                const float max_scroll = (std::max)(0.f, 11.f * 78.f - viewport_height);
                scroll = std::clamp(scroll - delta * 55.f, 0.f, max_scroll);
            }
        }
        void on_mouse_move(const vec2& cursor, const vec2&) override {
            if (!selected_weapon.internal || !options_open || !drag_slider) return;
            if (!input.is_mouse_down(m_left)) { drag_slider=0; commit_weapon(); return; }
            const float fraction=std::clamp((cursor.x-area_abs().tl().x-(size.x-324.f)-120.f)/180.f,0.f,1.f);
            if (drag_slider==1) pending.seed=static_cast<int>(fraction*1000.f+.5f);
            else if (drag_slider==2) pending.wear=fraction;
            else pending.stattrak_count=static_cast<int>(fraction*99999.f+.5f);
        }
        void on_mouse_up(char key) override {
            if (key==m_left && drag_slider) { drag_slider=0; commit_weapon(); }
        }
        void on_text_input(const std::vector<uint32_t>& text) override {
            if (!editing_name && !editing_search) return;
            if (input.is_key_down(VK_CONTROL) || input.is_key_down(VK_MENU)) return;
            auto& target = editing_search ? search_query : pending.custom_name;
            const size_t limit = editing_search ? 64u : 31u;
            for (const auto codepoint : text) {
                if (codepoint < 0x20 || codepoint == 0x7f || codepoint > 0x10ffff ||
                    (codepoint >= 0xd800 && codepoint <= 0xdfff)) continue;
                const auto encoded = evo::ren::utf8_encode(codepoint);
                if (target.size() + encoded.size() <= limit) target += encoded;
            }
            if (editing_search) skin_scroll = equipment_scroll = 0.f;
            else if (selected_weapon.internal) commit_weapon();
        }
        void on_key_down(uint16_t key) override {
            if (!editing_name && !editing_search) return;
            if (key == VK_RETURN || key == VK_ESCAPE) { stop_name_input(); return; }
            auto& target = editing_search ? search_query : pending.custom_name;
            if (key == VK_BACK && !target.empty()) {
                size_t pos = target.size() - 1;
                while (pos > 0 && (static_cast<unsigned char>(target[pos]) & 0xc0) == 0x80) --pos;
                target.erase(pos);
                if (editing_search) skin_scroll = equipment_scroll = 0.f;
                else if (selected_weapon.internal) commit_weapon();
            }
        }
    private:
        void stop_name_input() { editing_name = false; editing_search = false; is_taking_text = false; is_taking_keys = false; }
        static std::string paint_name(const std::string& paint) {
            for (const auto& entry : SkinNames::entries) if (entry.id == paint) return std::string(entry.name);
            std::string result = paint;
            const auto prefix = result.find('_');
            if (prefix != std::string::npos && prefix <= 4) result.erase(0, prefix + 1);
            std::replace(result.begin(), result.end(), '_', ' ');
            bool word = true;
            for (auto& ch : result) { if (word) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch))); word = ch == ' '; }
            return result;
        }
        std::vector<std::string> filtered_paints() const {
            auto lower = [](std::string value) {
                std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                return value;
            };
            const auto query = lower(search_query);
            std::vector<std::string> result;
            if (filter_index < 3 && (query.empty() || std::string("default").find(query) != std::string::npos)) result.emplace_back();
            std::vector<std::string> matches;
            std::vector<std::string> available = paints;
            if (show_all_paints) {
                available.clear();
                for (const auto& entry : schema_metadata.paint_ids) available.push_back(entry.first);
            }
            for (const auto& paint : available) {
                const auto it = schema_metadata.rarity.find(paint);
                const bool rarity_match = filter_index < 3 || (filter_index == 10 ? it == schema_metadata.rarity.end() : it != schema_metadata.rarity.end() && it->second == filter_rarity[filter_index]);
                if (rarity_match && lower(paint_name(paint)).find(query) != std::string::npos) matches.push_back(paint);
            }
            std::sort(matches.begin(), matches.end(), [&](const auto& a, const auto& b) { return lower(paint_name(a)) < lower(paint_name(b)); });
            if (reverse_sort) std::reverse(matches.begin(), matches.end());
            result.insert(result.end(), matches.begin(), matches.end());
            return result;
        }
        int sticker_count() const {
            if (agent_editor) return 3;
            if (equipment_weapon) return 0;
            return selected_weapon.internal && (!std::strcmp(selected_weapon.internal, "weapon_revolver") || !std::strcmp(selected_weapon.internal, "weapon_g3sg1")) ? 5 : 4;
        }
        std::vector<const SkinSchema::Decal*> visible_decals() const {
            std::vector<const SkinSchema::Decal*> result;
            for (const auto& decal : schema_metadata.decals) if (decal.patch == agent_editor) result.push_back(&decal);
            return result;
        }
        void commit_weapon() {
            if (!selected_weapon.internal) return;
            selection[team_ct ? 0 : 1][selected_weapon.internal] = pending;
            const auto preview_id=schema_metadata.paint_ids.find(pending.paint);
            PreviewSkinState::Set(team_ct,selected_weapon.internal,preview_id==schema_metadata.paint_ids.end() ? 0 : preview_id->second,pending.seed,pending.wear);
            if (equipment_weapon) {
                SkinState::Choice finish{};finish.paint_kit=preview_id==schema_metadata.paint_ids.end()?0:preview_id->second;
                finish.seed=pending.seed;finish.wear=pending.wear;finish.souvenir=pending.souvenir;
                finish.stattrak=pending.souvenir || pending.stattrak_mode==2 ? -1 : pending.stattrak_mode==1 ? pending.stattrak_count : -2;
                std::memcpy(finish.name.data(),pending.custom_name.data(),(std::min)(pending.custom_name.size(),finish.name.size()-1));
                const auto* item=EquipmentState::Find(selected_weapon.internal);
                if(item)EquipmentState::Select(team_ct,!EquipmentState::Knife(item->definition),selected_weapon.internal,finish);
                return;
            }
            const int definition = SkinState::Definition(selected_weapon.internal);
            if (pending.paint.empty()) { SkinState::Remove(team_ct, definition); return; }
            const auto id = schema_metadata.paint_ids.find(pending.paint);
            if (id == schema_metadata.paint_ids.end() || !definition) {
                SkinState::Remove(team_ct, definition);
                LOG_WARN("skins: missing paint kit ID or weapon definition for %s / %s", selected_weapon.internal, pending.paint.c_str());
                return;
            }
            SkinState::Choice choice{};
            choice.paint_kit = id->second; choice.seed = pending.seed; choice.wear = pending.wear;
            choice.stattrak = pending.souvenir || pending.stattrak_mode == 2 ? -1 : pending.stattrak_mode == 1 ? pending.stattrak_count : -2;
            choice.souvenir = pending.souvenir;
            std::memcpy(choice.name.data(), pending.custom_name.data(), (std::min)(pending.custom_name.size(), choice.name.size() - 1));
            if (!SkinState::Set(team_ct, definition, choice)) LOG_WARN("skins: invalid choice for %s", selected_weapon.internal);
        }
        bool toolbar_click(const vec2& p) {
            const bool equipment = equipment_screen != Equipment::None;
            if (filter_open) {
                const int count = equipment ? 3 : 11;
                if (p.x >= 0.f && p.x < 100.f && p.y >= 64.f && p.y < 64.f + 18.f * static_cast<float>(count)) {
                    filter_index = static_cast<int>((p.y - 64.f) / 18.f); reverse_sort = filter_index == 2;
                    skin_scroll = equipment_scroll = 0.f;
                }
                filter_open = false; return true;
            }
            if (p.y < 34.f || p.y >= 64.f) return false;
            if (p.x >= size.x - 34.f && p.x < size.x) {
                options_open = true; stop_name_input(); return true;
            }
            if (p.x >= 0.f && p.x < 100.f) { filter_open = true; stop_name_input(); return true; }
            if (p.x >= 106.f && p.x < size.x - 40.f) {
                editing_search = true; editing_name = false; is_taking_text = true; is_taking_keys = true;
                return true;
            }
            return false;
        }
        bool options_click(const vec2& cursor) {
            const auto p = cursor - vec2{size.x - 324.f, 0.f};
            if (p.x < 0.f || p.x >= 324.f || p.y < 68.f || p.y >= 424.f) {
                options_open = false; stat_menu_open = false; drag_slider = 0; stop_name_input(); return true;
            }
            if (p.y < 94.f && p.x >= 290.f) { options_open = false; stop_name_input(); return true; }
            if (stat_menu_open) {
                if (pending.souvenir) { stat_menu_open=false; return true; }
                if (p.x >= 120.f && p.x < 300.f && p.y >= 286.f && p.y < 358.f) {
                    pending.stattrak_mode = static_cast<int>((p.y - 286.f) / 24.f);
                    pending.stattrak = pending.stattrak_mode == 1;
                    commit_weapon();
                }
                stat_menu_open = false; return true;
            }
            const float slots_y = agent_editor ? 120.f : 370.f;
            if (p.y >= slots_y && p.y < slots_y+32.f && p.x >= 120.f && p.x < 120.f+37.f*static_cast<float>(sticker_count())) {
                load_catalog(); decal_picker = static_cast<int>((p.x-120.f)/37.f); decal_scroll = 0.f; stop_name_input(); return true;
            }
            if (agent_editor) return true;
            if (p.y >= 220.f && p.y < 250.f && p.x >= 120.f && p.x < 300.f) {
                editing_name = true; editing_search = false; is_taking_text = true; is_taking_keys = true; return true;
            }
            stop_name_input();
            if (p.x >= 120.f && p.x <= 300.f) {
                if (p.y >= 94.f && p.y < 120.f) drag_slider = 1;
                else if (p.y >= 132.f && p.y < 162.f) drag_slider = 2;
                else if (p.y >= 260.f && p.y < 290.f && !pending.souvenir) { stat_menu_open = true; return true; }
                else if (p.y >= 300.f && p.y < 330.f && pending.stattrak_mode == 1 && !pending.souvenir) drag_slider = 3;
                else if (p.y >= 180.f && p.y < 208.f && p.x < 144.f) {
                    pending.souvenir = !pending.souvenir; stat_menu_open=false;
                    commit_weapon(); return true;
                }
                if (drag_slider) {
                    const float f = std::clamp((p.x-120.f)/180.f,0.f,1.f);
                    if (drag_slider == 1) pending.seed = static_cast<int>(f*1000.f+.5f);
                    else if (drag_slider == 2) pending.wear = f;
                    else pending.stattrak_count = static_cast<int>(f*99999.f+.5f);
                    commit_weapon();
                }
            }
            return true;
        }
        void weapon_screen_click(const vec2& p) {
            if (options_open && options_click(p)) return;
            if (p.y >= 0.f && p.y < 28.f && p.x >= size.x - 35.f) { close_skins(); return; }
            if (toolbar_click(p)) return;
            stop_name_input();
            if (p.y < 72.f || p.y >= size.y || p.x < 0.f) return;
            const float width = (size.x - 24.f) / 5.f;
            const int col = static_cast<int>(p.x / (width + 6.f));
            const int row = static_cast<int>((p.y - 72.f + skin_scroll) / 78.f);
            if (col < 0 || col >= 5 || row < 0 || p.x - static_cast<float>(col) * (width + 6.f) >= width) return;
            const auto visible = filtered_paints();
            const size_t index = static_cast<size_t>(row * 5 + col);
            if (index < visible.size()) { pending.paint = visible[index]; commit_weapon(); }
        }
        std::vector<size_t> filtered_equipment() const {
            const auto& items = equipment_items(equipment_screen, team_ct);
            auto lower = [](std::string text) { for (auto& c : text) c=static_cast<char>(std::tolower(static_cast<unsigned char>(c))); return text; };
            const auto query=lower(search_query);
            std::vector<size_t> result;
            for (size_t i=0;i<items.size();++i) if (lower(items[i].label).find(query)!=std::string::npos) result.push_back(i);
            if (filter_index == 1 || filter_index == 2) std::sort(result.begin(),result.end(),[&](size_t a,size_t b) {
                return filter_index == 2 ? lower(items[a].label)>lower(items[b].label) : lower(items[a].label)<lower(items[b].label);
            });
            return result;
        }
        void render_toolbar(std::shared_ptr<evo::ren::layer>& layer,const rect& r) {
            const rect sort{r.tl()+vec2{0.f,34.f},r.tl()+vec2{100.f,62.f}};
            const rect search{r.tl()+vec2{106.f,34.f},r.tl()+vec2{size.x-40.f,62.f}};
            layer->add_rect_filled_rounded(sort,colors.bg_bottom,3.f);
            layer->add_text(sort.center(),std::string(filter_labels[filter_index])+"  v",colors.text,text_params::with_vh(align_center,align_center));
            layer->add_rect_filled_rounded(search,colors.bg_bottom,3.f);
            layer->add_text(search.tl()+vec2{8.f,14.f},search_query.empty() ? (editing_search ? "|" : "Search...") : search_query+(editing_search ? "|" : ""),colors.text_dark,text_params::with_v(align_center));
            const auto center=r.tl()+vec2{size.x-17.f,48.f};
            layer->add_circle_filled(center,14.f,options_open ? colors.bg_block_light : colors.bg_bottom,24);
            layer->add_circle(center,14.f,colors.outline,24,1.f,1.f);
            layer->add_text(center,"...",colors.text,text_params::with_vh(align_center,align_center));
        }
        void render_filter(std::shared_ptr<evo::ren::layer>& layer,const rect& r) {
            if (!filter_open) return;
            const int count=equipment_screen != Equipment::None ? 3 : 11;
            for (int i=0;i<count;++i) {
                const rect option{r.tl()+vec2{0.f,64.f+18.f*static_cast<float>(i)},r.tl()+vec2{100.f,82.f+18.f*static_cast<float>(i)}};
                layer->add_rect_filled(option,filter_index == i ? colors.bg_block_light : colors.bg_bottom);
                layer->add_text(option.tl()+vec2{6.f,9.f},filter_labels[i],colors.text,text_params::with_v(align_center));
            }
        }
        void decal_picker_click(const vec2& p) {
            if (p.y < 32.f) {
                if (p.x >= size.x - 35.f) { decal_picker = -1; return; }
                if (p.x < 130.f) { pending.stickers[decal_picker] = {}; decal_picker = -1; if (!agent_editor) commit_weapon(); else agent_patches[team_ct ? 0 : 1][agent_choice] = pending.stickers; return; }
            }
            if (p.y < 42.f || p.x < 0.f) return;
            const float width = (size.x - 12.f) / 3.f;
            const int col = static_cast<int>(p.x / (width + 6.f));
            const int row = static_cast<int>((p.y - 42.f + decal_scroll) / 78.f);
            if (col < 0 || col >= 3 || row < 0 || p.x - static_cast<float>(col) * (width + 6.f) >= width) return;
            const auto visible = visible_decals();
            const size_t index = static_cast<size_t>(row * 3 + col);
            if (index < visible.size()) { pending.stickers[decal_picker] = *visible[index]; decal_picker = -1; if (!agent_editor) commit_weapon(); else agent_patches[team_ct ? 0 : 1][agent_choice] = pending.stickers; }
        }
        void draw_decal(std::shared_ptr<evo::ren::layer>& layer, const SkinSchema::Decal& decal, const rect& box, bool& budget) {
            if (decal.name.empty()) return;
            const std::string path = std::string("resource/flash/econ/") + (decal.patch ? "patches/" : "stickers/") + decal.path + ".png";
            if (auto icon = get_asset_icon(path, budget); icon && icon->obj) {
                const auto native = icon->get_size();
                if (native.x > 0.f && native.y > 0.f) {
                    const float scale = (std::min)(box.width() / native.x, box.height() / native.y);
                    const vec2 sz = native * scale;
                    layer->g.texture = icon->obj;
                    layer->add_rect_filled(rect{box.center() - sz * .5f, box.center() + sz * .5f}, color::white());
                    layer->g.texture = {};
                }
            }
        }
        void render_decal_picker(std::shared_ptr<evo::ren::layer>& layer, const rect& r) {
            layer->font = draw.fonts[GUI_HASH("gui_main")];
            layer->add_text(r.tl() + vec2{4.f, 14.f}, "REMOVE", colors.accent, text_params::with_v(align_center));
            layer->add_text(r.tl() + vec2{150.f, 14.f}, agent_editor ? "PATCHES" : "STICKERS", colors.text, text_params::with_v(align_center));
            const rect close{r.tr() - vec2{30.f, 0.f}, r.tr() + vec2{0.f, 27.f}};
            layer->add_rect_filled_rounded(close, colors.bg_bottom, 2.f);
            layer->add_text(close.center(), "X", color{230, 60, 90}, text_params::with_vh(align_center, align_center));
            const auto visible = visible_decals();
            const float width = (r.width() - 12.f) / 3.f;
            const rect viewport{r.tl() + vec2{0.f, 42.f}, r.br()};
            const auto clip = layer->g.clip_rect; layer->override_clip_rect(viewport);
            bool budget = true;
            for (size_t i = 0; i < visible.size(); ++i) {
                const float x = r.mins.x + static_cast<float>(i % 3) * (width + 6.f);
                const float y = r.mins.y + 42.f + static_cast<float>(i / 3) * 78.f - decal_scroll;
                const rect cell{vec2{x,y},vec2{x+width,y+72.f}};
                if (cell.maxs.y <= viewport.mins.y || cell.mins.y >= viewport.maxs.y) continue;
                layer->add_rect_filled_rounded(cell, colors.bg_bottom, 2.f);
                draw_decal(layer, *visible[i], rect{vec2{x+width*.5f-24.f,y+2.f},vec2{x+width*.5f+24.f,y+50.f}}, budget);
                layer->font = draw.fonts[GUI_HASH("gui_main")];
                layer->add_text(vec2{x+3.f,y+58.f}, visible[i]->name, colors.text, text_params::with_v(align_center));
            }
            layer->g.clip_rect = clip;
        }
        const std::string& selected_paint(const char* weapon) const {
            return selected_equipment_paint(weapon, team_ct);
        }
        void close_skins() {
            stop_name_input();
            options_open = false; stat_menu_open = false; decal_picker = -1;
            selected_weapon = {}; equipment_weapon = false;
            FatalityPreviewBridge::skins_selecting.store(false, std::memory_order_relaxed);
        }
        void render_equipment(std::shared_ptr<evo::ren::layer>& layer, const rect& r) {
            layer->font = draw.fonts[GUI_HASH("gui_main")];
            layer->add_text(r.tl() + vec2{2.f, 12.f}, equipment_title(equipment_screen), colors.text, text_params::with_v(align_center));
            const rect close{r.tr() - vec2{30.f, 0.f}, r.tr() + vec2{0.f, 27.f}};
            layer->add_rect_filled_rounded(close, colors.bg_bottom, 2.f);
            layer->add_text(close.center(), "X", color{230, 60, 90}, text_params::with_vh(align_center, align_center));
            const auto& items = equipment_items(equipment_screen, team_ct);
            const float cell_width = (r.width() - 12.f) / 3.f;
            const float list_top = 72.f;
            render_toolbar(layer,r);
            const rect viewport{r.tl() + vec2{0.f, list_top}, r.br()};
            const auto old_clip = layer->g.clip_rect;
            layer->override_clip_rect(viewport);
            bool budget = true;
            const auto visible_items = filtered_equipment();
            for (size_t slot = 0; slot < visible_items.size(); ++slot) {
                const size_t i = visible_items[slot];
                const float x = r.mins.x + static_cast<float>(slot % 3) * (cell_width + 6.f);
                const float y = r.mins.y + list_top + static_cast<float>(slot / 3) * 78.f - equipment_scroll;
                const rect cell{vec2{x, y}, vec2{x + cell_width, y + 72.f}};
                if (cell.maxs.y <= viewport.mins.y || cell.mins.y >= viewport.maxs.y) continue;
                layer->add_rect_filled_rounded(cell, colors.bg_bottom, 2.f);
                if (equipment_screen == Equipment::Knife && viewport.contains(input.cursor()) && cell.contains(input.cursor()))
                    FatalityPreviewBridge::weapon.store(PreviewCatalog::WeaponIndex(items[i].icon[0] ? items[i].icon : (team_ct ? "weapon_knife" : "weapon_knife_t")), std::memory_order_relaxed);
                draw_equipment_icon(layer, equipment_screen, items[i].icon, team_ct, vec2{x + cell_width * .5f, y + 27.f}, vec2{cell_width - 10.f, 48.f}, budget, equipment_screen == Equipment::Knife ? selected_paint(items[i].icon) : std::string{});
                layer->font = draw.fonts[GUI_HASH("gui_main")];
                std::string label = items[i].label;
                if (layer->font->get_text_size(label).x > cell_width - 10.f) {
                    while (!label.empty() && layer->font->get_text_size(label + "...").x > cell_width - 10.f) {
                        auto pos = label.size() - 1;
                        while (pos && (static_cast<unsigned char>(label[pos]) & 0xc0) == 0x80) --pos;
                        label.erase(pos);
                    }
                    label += "...";
                }
                layer->add_text(vec2{x + cell_width * .5f, y + 57.f}, label, colors.text, text_params::with_vh(align_center, align_center));
            }
            layer->g.clip_rect = old_clip;
            render_filter(layer,r);
            if (options_open) render_weapon_options(layer,r);
        }
        void render_skins(std::shared_ptr<evo::ren::layer>& layer, const rect& r) {
            layer->font = draw.fonts[GUI_HASH("gui_main")];
            const auto title = std::string(selected_weapon.label) + (pending.paint.empty() ? " | Default" : " | " + paint_name(pending.paint));
            layer->add_text(r.tl() + vec2{size.x * .5f, 14.f}, title, colors.text, text_params::with_vh(align_center, align_center));
            const rect close{r.tr() - vec2{30.f, 0.f}, r.tr() + vec2{0.f, 27.f}};
            layer->add_rect_filled_rounded(close, colors.bg_bottom, 3.f);
            layer->add_text(close.center(), "X", colors.text_dark, text_params::with_vh(align_center, align_center));
            FatalityPreviewBridge::weapon_editor_visible.store(false,std::memory_order_relaxed);
            bool budget = true;
            render_toolbar(layer,r);
            const float width = (r.width() - 24.f) / 5.f;
            const rect viewport{r.tl()+vec2{0.f,72.f},r.br()};
            const auto clip = layer->g.clip_rect; layer->override_clip_rect(viewport);
            const auto visible = filtered_paints();
            for (size_t i = 0; i < visible.size(); ++i) {
                const float x = r.mins.x + static_cast<float>(i % 5) * (width + 6.f);
                const float y = r.mins.y + 72.f + static_cast<float>(i / 5) * 78.f - skin_scroll;
                const rect cell{vec2{x,y},vec2{x+width,y+72.f}};
                if (cell.maxs.y <= viewport.mins.y || cell.mins.y >= viewport.maxs.y) continue;
                layer->add_rect_filled_rounded(cell, colors.bg_bottom, 4.f);
                if (visible[i] == pending.paint) layer->add_rect_rounded(cell, colors.accent, 4.f);
                if (auto icon = get_weapon_icon(selected_weapon.internal, visible[i], budget); icon && icon->obj) {
                    const auto native = icon->get_size();
                    if (native.x > 0.f && native.y > 0.f) {
                        const auto scale = (std::min)((width - 8.f) / native.x, 43.f / native.y);
                        const auto dims = native * scale; const vec2 center{x + width*.5f,y+25.f};
                        layer->g.texture = icon->obj; layer->add_rect_filled(rect{center-dims*.5f,center+dims*.5f},color::white()); layer->g.texture = {};
                    }
                }
                const auto item_clip = layer->g.clip_rect;
                layer->override_clip_rect(rect{vec2{x+2.f,(std::max)(y+48.f,viewport.mins.y)},vec2{x+width-2.f,(std::min)(y+72.f,viewport.maxs.y)}});
                layer->add_text(vec2{x+4.f,y+59.f},visible[i].empty() ? std::string("Default") : paint_name(visible[i]),colors.text,text_params::with_v(align_center));
                layer->g.clip_rect = item_clip;
            }
            layer->g.clip_rect = clip;
            render_filter(layer,r);
            if (options_open) render_weapon_options(layer, r);
        }
        void render_weapon_options(std::shared_ptr<evo::ren::layer>& layer, const rect& r) {
            const auto origin=r.tl()+vec2{size.x-324.f,0.f};
            const rect panel{origin+vec2{0.f,68.f},origin+vec2{324.f,agent_editor ? 180.f : 424.f}};
            layer->add_rect_filled_rounded(panel,colors.bg_block,6.f);
            layer->add_rect_rounded(panel,colors.outline,6.f);
            layer->add_text(origin+vec2{12.f,82.f},"Options",colors.text,text_params::with_v(align_center));
            layer->add_text(origin+vec2{304.f,82.f},"X",colors.text_dark,text_params::with_vh(align_center,align_center));
            auto label=[&](float y,const char* text) { layer->add_text(origin+vec2{12.f,y},text,colors.text,text_params::with_v(align_center)); };
            auto slider=[&](float y,const char* text,float value,const char* display) {
                label(y,text);
                layer->add_rect_filled_rounded(rect{origin+vec2{120.f,y+3.f},origin+vec2{300.f,y+8.f}},colors.bg_bottom,2.f);
                layer->add_rect_filled_rounded(rect{origin+vec2{120.f,y+3.f},origin+vec2{120.f+180.f*value,y+8.f}},colors.accent,2.f);
                layer->add_text(origin+vec2{300.f,y-9.f},display,colors.text_dark,text_params::with_h(align_right));
            };
            if (!agent_editor) {
                char number[32];
                std::snprintf(number,sizeof(number),"%d",pending.seed); slider(108.f,"Seed",static_cast<float>(pending.seed)/1000.f,number);
                std::snprintf(number,sizeof(number),"%.3f",pending.wear); slider(150.f,"Wear",pending.wear,number);
                label(194.f,"Souvenir");
                layer->add_rect_filled_rounded(rect{origin+vec2{120.f,184.f},origin+vec2{140.f,204.f}},pending.souvenir ? colors.accent : colors.bg_bottom,3.f);
                label(235.f,"Custom name");
                const rect name{origin+vec2{120.f,220.f},origin+vec2{300.f,249.f}};
                layer->add_rect_filled_rounded(name,colors.bg_bottom,3.f);
                const auto old_clip=layer->g.clip_rect; layer->override_clip_rect(name);
                layer->add_text(origin+vec2{128.f,235.f},pending.custom_name.empty() ? (editing_name ? "|" : "Name tag") : pending.custom_name+(editing_name ? "|" : ""),colors.text,text_params::with_v(align_center));
                layer->g.clip_rect=old_clip;
                label(275.f,"StatTrak");
                const char* modes[]={"Default","Static","None"};
                layer->add_rect_filled_rounded(rect{origin+vec2{120.f,260.f},origin+vec2{300.f,289.f}},colors.bg_bottom,3.f);
                layer->add_text(origin+vec2{128.f,275.f},std::string(modes[pending.stattrak_mode])+"  v",colors.text,text_params::with_v(align_center));
                std::snprintf(number,sizeof(number),"%d",pending.stattrak_count); slider(317.f,"Value",static_cast<float>(pending.stattrak_count)/99999.f,number);
            }
            const float slots_y=agent_editor ? 120.f : 370.f;
            if (sticker_count()) label(slots_y+16.f,agent_editor ? "Patches" : "Stickers");
            else label(slots_y+16.f,"No sticker slots for this item");
            bool budget=true;
            for (int i=0;i<sticker_count();++i) {
                const auto center=origin+vec2{136.f+37.f*static_cast<float>(i),slots_y+16.f};
                layer->add_circle_filled(center,14.f,colors.bg_bottom,24);
                layer->add_circle(center,14.f,colors.outline,24,1.f,1.f);
                if (!pending.stickers[i].name.empty())
                    draw_decal(layer,pending.stickers[i],rect{center-vec2{11.f,11.f},center+vec2{11.f,11.f}},budget);
            }
            if (!agent_editor && pending.souvenir)
                layer->add_rect_filled_rounded(rect{origin+vec2{8.f,256.f},origin+vec2{310.f,334.f}},color{0,0,0,155},3.f);
            if (stat_menu_open && !pending.souvenir) for (int i=0;i<3;++i) {
                const rect option{origin+vec2{120.f,286.f+24.f*static_cast<float>(i)},origin+vec2{300.f,310.f+24.f*static_cast<float>(i)}};
                layer->add_rect_filled(option,colors.bg_bottom);
                const char* modes[]={"Default","Static","None"};
                layer->add_text(option.tl()+vec2{8.f,12.f},modes[i],colors.text,text_params::with_v(align_center));
            }
        }
        bool catalog_settings_open{};
        bool team_ct{true};
        float scroll{}, skin_scroll{}, equipment_scroll{};
        Equipment last_equipment_screen{Equipment::None};
        WeaponIcon selected_weapon{};
        SkinChoice pending{};
        bool editing_name{}, editing_search{}, reverse_sort{}, filter_open{}, options_open{}, stat_menu_open{};
        int filter_index{};
        std::string search_query;
        int drag_slider{}, decal_picker{-1}, agent_choice{};
        float decal_scroll{};
        bool agent_editor{}, equipment_weapon{};
        std::vector<std::string> paints;
    };
    }

    void skins_equipment_tiles(std::shared_ptr<layout>& e) {
        e->add(MAKE("skins>equipment>tiles", equipment_tiles, vec2{220.f, 108.f}));
    }
    void skins_weapon_grid(std::shared_ptr<layout>& e) {
        e->add(MAKE("skins>weapon_grid", weapon_grid, vec2{480.f, 440.f}));
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
