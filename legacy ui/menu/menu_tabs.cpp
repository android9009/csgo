#include "../../globals.hpp"
#include "../config_system.h"
#include "../config_vars.h"

#include "../legacy_str.h"
#include "menu.h"
#include "../../skins.hpp"
#include "inventory_preview.h"

#include <ShlObj.h>
#include <algorithm>
#include <shellapi.h>
#include <Windows.h>
#include <map>

std::vector< std::string > player_tabs = {
	XOR("Enemies"),
	XOR("Teammates"),
	XOR("Local"),
};

std::vector< std::string > world_tabs = {
	XOR("World ESP"),
	XOR("Modulation"),
	XOR("Effects"),
	XOR("Camera"),
	XOR("HUD"),
};

std::vector< std::string > inventory_tabs = {
	XOR("Loadout"),
	XOR("Skin editor"),
	XOR("Agents & gloves"),
};

std::vector< std::string > misc_tabs = {
  XOR("Movement"),
  XOR("Events"),
  XOR("Other"),
};

const char* target_selections[] = {
	"Crosshair",
	"Distance",
	"Health",
	"Hybrid",
};

#define begin_child( text ) begin_group_box( ( text ) );
#define end_child end_group_box( ); ImGui::ItemSize( ImVec2( 0, 9 ) );

std::vector< std::string > config_list{};
std::vector< std::string > empty_list = { XOR("Config folder is empty!") };

bool update_configs = false;

typedef void (*LPSEARCHFUNC)(LPCTSTR lpszFileName);

BOOL search_files(LPCTSTR lpszFileName, LPSEARCHFUNC lpSearchFunc, BOOL bInnerFolders)
{
	LPTSTR part;
	char tmp[MAX_PATH];
	char name[MAX_PATH];

	HANDLE hSearch = NULL;
	WIN32_FIND_DATA wfd;
	memset(&wfd, 0, sizeof(WIN32_FIND_DATA));

	if (bInnerFolders)
	{
		if (GetFullPathNameA(lpszFileName, MAX_PATH, tmp, &part) == 0)
			return FALSE;
		strcpy(name, part);
		strcpy(part, CXOR("*.*"));
		wfd.dwFileAttributes = FILE_ATTRIBUTE_DIRECTORY;
		if (!((hSearch = FindFirstFileA(tmp, &wfd)) == INVALID_HANDLE_VALUE))
			do
			{
				if (!strncmp(wfd.cFileName, CXOR("."), 1) || !strncmp(wfd.cFileName, CXOR(".."), 2))
					continue;

				if (wfd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
				{
					char next[MAX_PATH];
					if (GetFullPathNameA(lpszFileName, MAX_PATH, next, &part) == 0)
						return FALSE;
					strcpy(part, wfd.cFileName);
					strcat(next, CXOR("\\"));
					strcat(next, name);

					search_files(next, lpSearchFunc, TRUE);
				}
			} while (FindNextFileA(hSearch, &wfd));
			FindClose(hSearch);
	}

	if ((hSearch = FindFirstFileA(lpszFileName, &wfd)) == INVALID_HANDLE_VALUE)
		return TRUE;
	do
		if (!(wfd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
		{
			char file[MAX_PATH];
			if (GetFullPathNameA(lpszFileName, MAX_PATH, file, &part) == 0)
				return FALSE;
			strcpy(part, wfd.cFileName);

			lpSearchFunc(wfd.cFileName);
		}
	while (FindNextFileA(hSearch, &wfd));
	FindClose(hSearch);
	return TRUE;
}

void read_configs(LPCTSTR lpszFileName)
{
	config_list.push_back(lpszFileName);
}

void refresh_configs()
{
	std::string folder, file;

	config_list.clear();
	std::string config_dir = CXOR("airflow\\*");
	search_files(config_dir.c_str(), read_configs, FALSE);
}

int selector = 0;
int selector_visuals = 0;

const char* rage_hitboxes[] = {
	  xor_strs::hitbox_head.c_str(),
	  xor_strs::hitbox_body.c_str(),
	  xor_strs::hitbox_limbs.c_str(),
};

const char* weapon_configs[] = {
	  xor_strs::weapon_default.c_str(),
	  xor_strs::weapon_auto.c_str(),
	  xor_strs::weapon_heavy_pistols.c_str(),
	  xor_strs::weapon_pistols.c_str(),
	  xor_strs::weapon_ssg08.c_str(),
	  xor_strs::weapon_awp.c_str(),
};

#ifdef LEGACY
const char* knife_models[]{
	xor_strs::knife_default.c_str(),
	xor_strs::knife_bayonet.c_str(),
	xor_strs::knife_flip.c_str(),
	xor_strs::knife_gut.c_str(),
	xor_strs::knife_karambit.c_str(),
	xor_strs::knife_m9.c_str(),
	xor_strs::knife_huntsman.c_str(),
	xor_strs::knife_falchion.c_str(),
	xor_strs::knife_bowie.c_str(),
	xor_strs::knife_butterfly.c_str(),
};

#else
const char* knife_models[]{
	xor_strs::knife_default.c_str(),
	xor_strs::knife_bayonet.c_str(),
	xor_strs::knife_css.c_str(),
	xor_strs::knife_skeleton.c_str(),
	xor_strs::knife_nomad.c_str(),
	xor_strs::knife_paracord.c_str(),
	xor_strs::knife_survival.c_str(),
	xor_strs::knife_flip.c_str(),
	xor_strs::knife_gut.c_str(),
	xor_strs::knife_karambit.c_str(),
	xor_strs::knife_m9.c_str(),
	xor_strs::knife_huntsman.c_str(),
	xor_strs::knife_falchion.c_str(),
	xor_strs::knife_bowie.c_str(),
	xor_strs::knife_butterfly.c_str(),
	xor_strs::knife_shadow.c_str(),
	xor_strs::knife_ursus.c_str(),
	xor_strs::knife_navaga.c_str(),
	xor_strs::knife_stiletto.c_str(),
	xor_strs::knife_talon.c_str(),
};
#endif

const char* defensive_aa_mode[]
{
	xor_strs::defensive_trigger.c_str(),
	xor_strs::defensive_always.c_str(),
};

const char* legit_weapon_configs[] = {
	xor_strs::weapon_cfg_deagle.c_str(),
	xor_strs::weapon_cfg_duals.c_str(),
	xor_strs::weapon_cfg_fiveseven.c_str(),
	xor_strs::weapon_cfg_glock.c_str(),
	xor_strs::weapon_cfg_ak.c_str(),
	xor_strs::weapon_cfg_aug.c_str(),
	xor_strs::weapon_cfg_awp.c_str(),
	xor_strs::weapon_cfg_famas.c_str(),
	xor_strs::weapon_cfg_g3sg1.c_str(),
	xor_strs::weapon_cfg_galil.c_str(),
	xor_strs::weapon_cfg_m249.c_str(),
	xor_strs::weapon_cfg_m4a1.c_str(),
	xor_strs::weapon_cfg_m4a1s.c_str(),
	xor_strs::weapon_cfg_mac10.c_str(),
	xor_strs::weapon_cfg_p90.c_str(),
	xor_strs::weapon_cfg_mp5.c_str(),
	xor_strs::weapon_cfg_ump45.c_str(),
	xor_strs::weapon_cfg_xm1014.c_str(),
	xor_strs::weapon_cfg_bizon.c_str(),
	xor_strs::weapon_cfg_mag7.c_str(),
	xor_strs::weapon_cfg_negev.c_str(),
	xor_strs::weapon_cfg_sawed_off.c_str(),
	xor_strs::weapon_cfg_tec9.c_str(),
	xor_strs::weapon_cfg_p2000.c_str(),
	xor_strs::weapon_cfg_mp7.c_str(),
	xor_strs::weapon_cfg_mp9.c_str(),
	xor_strs::weapon_cfg_nova.c_str(),
	xor_strs::weapon_cfg_p250.c_str(),
	xor_strs::weapon_cfg_scar20.c_str(),
	xor_strs::weapon_cfg_sg553.c_str(),
	xor_strs::weapon_cfg_scout.c_str(),
	xor_strs::weapon_cfg_usps.c_str(),
	xor_strs::weapon_cfg_cz75.c_str(),
	xor_strs::weapon_cfg_revolver.c_str(),
};

const char* skin_weapon_configs[] = {
	xor_strs::weapon_cfg_deagle.c_str(),
	xor_strs::weapon_cfg_duals.c_str(),
	xor_strs::weapon_cfg_fiveseven.c_str(),
	xor_strs::weapon_cfg_glock.c_str(),
	xor_strs::weapon_cfg_ak.c_str(),
	xor_strs::weapon_cfg_aug.c_str(),
	xor_strs::weapon_cfg_awp.c_str(),
	xor_strs::weapon_cfg_famas.c_str(),
	xor_strs::weapon_cfg_g3sg1.c_str(),
	xor_strs::weapon_cfg_galil.c_str(),
	xor_strs::weapon_cfg_m249.c_str(),
	xor_strs::weapon_cfg_m4a1.c_str(),
	xor_strs::weapon_cfg_m4a1s.c_str(),
	xor_strs::weapon_cfg_mac10.c_str(),
	xor_strs::weapon_cfg_p90.c_str(),
	xor_strs::weapon_cfg_mp5.c_str(),
	xor_strs::weapon_cfg_ump45.c_str(),
	xor_strs::weapon_cfg_xm1014.c_str(),
	xor_strs::weapon_cfg_bizon.c_str(),
	xor_strs::weapon_cfg_mag7.c_str(),
	xor_strs::weapon_cfg_negev.c_str(),
	xor_strs::weapon_cfg_sawed_off.c_str(),
	xor_strs::weapon_cfg_tec9.c_str(),
	xor_strs::weapon_cfg_p2000.c_str(),
	xor_strs::weapon_cfg_mp7.c_str(),
	xor_strs::weapon_cfg_mp9.c_str(),
	xor_strs::weapon_cfg_nova.c_str(),
	xor_strs::weapon_cfg_p250.c_str(),
	xor_strs::weapon_cfg_scar20.c_str(),
	xor_strs::weapon_cfg_sg553.c_str(),
	xor_strs::weapon_cfg_scout.c_str(),
	xor_strs::weapon_cfg_usps.c_str(),
	xor_strs::weapon_cfg_cz75.c_str(),
	xor_strs::weapon_cfg_revolver.c_str(),
	xor_strs::weapon_cfg_knife.c_str(),
};

const char* aa_pitch[] =
{
  xor_strs::aa_disabled.c_str(),
  xor_strs::aa_pitch_down.c_str(),
  xor_strs::aa_pitch_up.c_str(),
};

const char* aa_yaw[] =
{
  xor_strs::aa_disabled.c_str(),
  xor_strs::aa_yaw_back.c_str(),
  xor_strs::aa_yaw_spin.c_str(),
};

const char* aa_jitter_yaw[] =
{
	  xor_strs::aa_disabled.c_str(),
	  xor_strs::aa_jitter_center.c_str(),
	  xor_strs::aa_jitter_offset.c_str(),
	  xor_strs::aa_jitter_random.c_str(),
	  xor_strs::aa_jitter_3way.c_str(),
};

const char* aa_desync_type[] =
{
	  xor_strs::aa_default.c_str(),
	  xor_strs::aa_desync_jitter.c_str(),
};

const char* vis_chams_type[] =
{
	xor_strs::vis_chams_textured.c_str(),
	xor_strs::vis_chams_metallic.c_str(),
	xor_strs::vis_chams_flat.c_str(),
	xor_strs::vis_chams_glass.c_str(),
	xor_strs::vis_chams_glow.c_str(),
	xor_strs::vis_chams_bubble.c_str(),
	xor_strs::vis_chams_money.c_str(),
	xor_strs::vis_chams_fadeup.c_str(),
};

const char* buybot_main[]
{
	xor_strs::buybot_none.c_str(),
	xor_strs::weapon_auto.c_str(),
	xor_strs::weapon_ssg08.c_str(),
	xor_strs::weapon_awp.c_str(),
	xor_strs::weapon_negev.c_str(),
	xor_strs::weapon_m249.c_str(),
	xor_strs::weapon_ak47.c_str(),
	xor_strs::weapon_aug.c_str(),
};

const char* buybot_second[]
{
	xor_strs::buybot_none.c_str(),
	xor_strs::weapon_duals.c_str(),
	xor_strs::weapon_p250.c_str(),
	xor_strs::weapon_cz.c_str(),
	xor_strs::weapon_heavy_pistols.c_str(),
};

const char* enemy_models[]
{
	xor_strs::chams_visible.c_str(),
	xor_strs::chams_xqz.c_str(),
	xor_strs::chams_history.c_str(),
	xor_strs::chams_onshot.c_str(),
	xor_strs::chams_ragdolls.c_str(),
};

const char* local_models[]
{
	xor_strs::chams_visible.c_str(),
	xor_strs::chams_viewmodel.c_str(),
	xor_strs::chams_wpn.c_str(),
	xor_strs::chams_attachments.c_str(),
	//xor_strs::chams_fake.c_str(),
};

const char* hitsound[]
{
	xor_strs::aa_disabled.c_str(),
	xor_strs::sound_metallic.c_str(),
	xor_strs::sound_tap.c_str(),
};

const char* boxes[]
{
	xor_strs::box_default.c_str(),
	xor_strs::box_thin.c_str(),
};

const char* ragdoll_gravities[]
{
	xor_strs::buybot_none.c_str(),
	xor_strs::ragdoll_away.c_str(),
	xor_strs::ragdoll_fly.c_str(),
};

const char* target_selection[]
{
	xor_strs::target_distance.c_str(),
	xor_strs::target_damage.c_str(),
};

const char* agents[]
{
	xor_strs::agent_default.c_str(),
	xor_strs::agent_danger_a.c_str(),
	xor_strs::agent_danger_b.c_str(),
	xor_strs::agent_danger_c.c_str(),
	xor_strs::agent_cmdr_davida.c_str(),
	xor_strs::agent_cmdr_frank.c_str(),
	xor_strs::agent_cmdr_lieutenant.c_str(),
	xor_strs::agent_cmdr_michael.c_str(),
	xor_strs::agent_cmdr_operator.c_str(),
	xor_strs::agent_cmdr_spec_agent_ava.c_str(),
	xor_strs::agent_cmdr_markus.c_str(),
	xor_strs::agent_cmdr_sous.c_str(),
	xor_strs::agent_cmdr_chem_haz.c_str(),
	xor_strs::agent_cmdr_chef_d.c_str(),
	xor_strs::agent_cmdr_aspirant.c_str(),
	xor_strs::agent_cmdr_officer.c_str(),
	xor_strs::agent_cmdr_d_sq.c_str(),
	xor_strs::agent_cmdr_b_sq.c_str(),
	xor_strs::agent_cmdr_seal_team6.c_str(),
	xor_strs::agent_cmdr_bunkshot.c_str(),
	xor_strs::agent_cmdr_lt_commander.c_str(),
	xor_strs::agent_cmdr_bunkshot2.c_str(),
	xor_strs::agent_cmdr_3rd_commando.c_str(),
	xor_strs::agent_cmdr_two_times_.c_str(),
	xor_strs::agent_cmdr_two_times_2.c_str(),
	xor_strs::agent_cmdr_premeiro.c_str(),
	xor_strs::agent_cmdr_cmdr.c_str(),
	xor_strs::agent_cmdr_1st_le.c_str(),
	xor_strs::agent_cmdr_john_van.c_str(),
	xor_strs::agent_cmdr_bio_haz.c_str(),
	xor_strs::agent_cmdr_sergeant.c_str(),
	xor_strs::agent_cmdr_chem_haz__.c_str(),
	xor_strs::agent_cmdr_farwlo.c_str(),
	xor_strs::agent_cmdr_getaway_sally.c_str(),
	xor_strs::agent_cmdr_getaway_number_k.c_str(),
	xor_strs::agent_cmdr_getaway_little_kev.c_str(),
	xor_strs::agent_cmdr_safecracker.c_str(),
	xor_strs::agent_cmdr_bloody_darryl.c_str(),
	xor_strs::agent_cmdr_bloody_loud.c_str(),
	xor_strs::agent_cmdr_bloody_royale.c_str(),
	xor_strs::agent_cmdr_bloody_skullhead.c_str(),
	xor_strs::agent_cmdr_bloody_silent.c_str(),
	xor_strs::agent_cmdr_bloody_miami.c_str(),
	xor_strs::agent_street_solider.c_str(),
	xor_strs::agent_solider.c_str(),
	xor_strs::agent_slingshot.c_str(),
	xor_strs::agent_enforcer.c_str(),
	xor_strs::agent_mr_muhlik.c_str(),
	xor_strs::agent_prof_shahmat.c_str(),
	xor_strs::agent_prof_osiris.c_str(),
	xor_strs::agent_prof_ground_rebek.c_str(),
	xor_strs::agent_prof_elite_muhlik.c_str(),
	xor_strs::agent_prof_trapper.c_str(),
	xor_strs::agent_prof_trapper_aggressor.c_str(),
	xor_strs::agent_prof_vypa_sista.c_str(),
	xor_strs::agent_prof_col_magnos.c_str(),
	xor_strs::agent_prof_crasswater.c_str(),
	xor_strs::agent_prof_crasswater_forgotten.c_str(),
	xor_strs::agent_prof_solman.c_str(),
	xor_strs::agent_prof_romanov.c_str(),
	xor_strs::agent_prof_blackwolf.c_str(),
	xor_strs::agent_prof_maximus.c_str(),
	xor_strs::agent_prof_dragomir.c_str(),
	xor_strs::agent_prof_rezan.c_str(),
	xor_strs::agent_prof_rezan_red.c_str(),
	xor_strs::agent_prof_dragomir2.c_str(),
	xor_strs::agent_gign.c_str(),
	xor_strs::agent_gign_a.c_str(),
	xor_strs::agent_gign_b.c_str(),
	xor_strs::agent_gign_c.c_str(),
	xor_strs::agent_gign_d.c_str(),
	xor_strs::agent_pirate.c_str(),
	xor_strs::agent_pirate_a.c_str(),
	xor_strs::agent_pirate_b.c_str(),
	xor_strs::agent_pirate_c.c_str(),
	xor_strs::agent_pirate_d.c_str(),
	xor_strs::sky_custom.c_str(),
};

const char* masks[]
{
	xor_strs::mask_none.c_str(),
	xor_strs::mask_battle.c_str(),
	xor_strs::mask_hoxton.c_str(),
	xor_strs::mask_doll.c_str(),
	xor_strs::mask_skull.c_str(),
	xor_strs::mask_samurai.c_str(),
	xor_strs::mask_evil_clown.c_str(),
	xor_strs::mask_wolf.c_str(),
	xor_strs::mask_sheep.c_str(),
	xor_strs::mask_bunny_gold.c_str(),
	xor_strs::mask_anaglyph.c_str(),
	xor_strs::mask_kabuki_doll.c_str(),
	xor_strs::mask_dallas.c_str(),
	xor_strs::mask_pumpkin.c_str(),
	xor_strs::mask_sheep_bloody.c_str(),
	xor_strs::mask_devil_plastic.c_str(),
	xor_strs::mask_boar.c_str(),
	xor_strs::mask_chains.c_str(),
	xor_strs::mask_tiki.c_str(),
	xor_strs::mask_bunny.c_str(),
	xor_strs::mask_sheep_gold.c_str(),
	xor_strs::mask_zombie_plastic.c_str(),
	xor_strs::mask_chicken.c_str(),
	xor_strs::mask_skull_gold.c_str(),
	xor_strs::mask_demon_man.c_str(),
	xor_strs::mask_engineer.c_str(),
	xor_strs::mask_heavy.c_str(),
	xor_strs::mask_medic.c_str(),
	xor_strs::mask_pyro.c_str(),
	xor_strs::mask_scout.c_str(),
	xor_strs::mask_sniper.c_str(),
	xor_strs::mask_solider.c_str(),
	xor_strs::mask_spy.c_str(),
	xor_strs::mask_holiday_light.c_str(),
};

const char* skyboxes[]
{
	xor_strs::aa_default.c_str(),
	xor_strs::sky_tibet.c_str(),
	xor_strs::sky_bagage.c_str(),
	xor_strs::sky_italy.c_str(),
	xor_strs::sky_jungle.c_str(),
	xor_strs::sky_office.c_str(),
	xor_strs::sky_daylight.c_str(),
	xor_strs::sky_daylight2.c_str(),
	xor_strs::sky_vertigo_blue.c_str(),
	xor_strs::sky_vertigo.c_str(),
	xor_strs::sky_day.c_str(),
	xor_strs::sky_nuke_bank.c_str(),
	xor_strs::sky_venice.c_str(),
	xor_strs::sky_daylight3.c_str(),
	xor_strs::sky_daylight4.c_str(),
	xor_strs::sky_cloudy.c_str(),
	xor_strs::sky_night.c_str(),
	xor_strs::sky_nightb.c_str(),
	xor_strs::sky_night_flat.c_str(),
	xor_strs::sky_dust.c_str(),
	xor_strs::sky_vietnam.c_str(),
	xor_strs::sky_lunacy.c_str(),
	xor_strs::sky_embassy.c_str(),
	xor_strs::sky_custom.c_str(),
};

const char* glove_models[]
{
	xor_strs::glove_default.c_str(),
	xor_strs::glove_bloodhound.c_str(),
	xor_strs::glove_sporty.c_str(),
	xor_strs::glove_slick.c_str(),
	xor_strs::glove_leather_wrap.c_str(),
	xor_strs::glove_motorcycle.c_str(),
	xor_strs::glove_specialist.c_str(),
	xor_strs::glove_hydra.c_str(),
};

const char* glove_skins[]
{
	xor_strs::glove_skin_charred.c_str(),
	xor_strs::glove_skin_snakebite.c_str(),
	xor_strs::glove_skin_bronzed.c_str(),
	xor_strs::glove_skin_leather.c_str(),
	xor_strs::glove_skin_spruce.c_str(),
	xor_strs::glove_skin_lunar.c_str(),
	xor_strs::glove_skin_convoy.c_str(),
	xor_strs::glove_skin_crimson.c_str(),
	xor_strs::glove_skin_superconductor.c_str(),
	xor_strs::glove_skin_arid.c_str(),
	xor_strs::glove_skin_slaugher.c_str(),
	xor_strs::glove_skin_eclipse.c_str(),
	xor_strs::glove_skin_spearmint.c_str(),
	xor_strs::glove_skin_boom.c_str(),
	xor_strs::glove_skin_coolmint.c_str(),
	xor_strs::glove_skin_forest.c_str(),
	xor_strs::glove_skin_crimson_kimono.c_str(),
	xor_strs::glove_skin_emerald_web.c_str(),
	xor_strs::glove_skin_foundation.c_str(),
	xor_strs::glove_skin_badlands.c_str(),
	xor_strs::glove_skin_pandora.c_str(),
	xor_strs::glove_skin_hedge.c_str(),
	xor_strs::glove_skin_guerilla.c_str(),
	xor_strs::glove_skin_diamondback.c_str(),
	xor_strs::glove_skin_king.c_str(),
	xor_strs::glove_skin_imperial.c_str(),
	xor_strs::glove_skin_overtake.c_str(),
	xor_strs::glove_skin_racing.c_str(),
	xor_strs::glove_skin_amphibious.c_str(),
	xor_strs::glove_skin_bronze.c_str(),
	xor_strs::glove_skin_omega.c_str(),
	xor_strs::glove_skin_vice.c_str(),
	xor_strs::glove_skin_pow.c_str(),
	xor_strs::glove_skin_turtle.c_str(),
	xor_strs::glove_skin_transport.c_str(),
	xor_strs::glove_skin_polygon.c_str(),
	xor_strs::glove_skin_cobalt.c_str(),
	xor_strs::glove_skin_overprint.c_str(),
	xor_strs::glove_skin_duct.c_str(),
	xor_strs::glove_skin_arboreal.c_str(),
	xor_strs::glove_skin_emerald.c_str(),
	xor_strs::glove_skin_mangrove.c_str(),
	xor_strs::glove_skin_rattler.c_str(),
	xor_strs::glove_skin_case.c_str(),
	xor_strs::glove_skin_crimson_web.c_str(),
	xor_strs::glove_skin_buñkshot.c_str(),
	xor_strs::glove_skin_fade.c_str(),
	xor_strs::glove_skin_mogul.c_str(),
};

const char* tracers[]
{
	xor_strs::tracer_laser.c_str(),
	xor_strs::tracer_beam.c_str(),
	xor_strs::tracer_white.c_str(),
	xor_strs::tracer_line.c_str(),
	xor_strs::tracer_glow.c_str(),
};

void c_menu::draw_ui_items()
{
	auto window_pos = get_window_pos();
	auto prev_pos = ImGui::GetCursorPos();

	auto new_pos = ImVec2(185, 76);
	ImGui::SetCursorPos(new_pos);

	auto list = get_draw_list();

	ImRect window_bb = ImRect(window_pos + ImVec2(170, 60), window_pos + ImVec2(800, 600));
	ImGui::PushClipRect(window_bb.Min, window_bb.Max, false);

	float old_alpha = get_alpha();

	static int prev_sel = 0;
	if (tab_selector != prev_sel)
	{
		for (auto& a : item_animations)
		{
			a.second.reset();
			tab_alpha = 0.f;
		}

		prev_sel = tab_selector;
	}

	// change alpha everytime user clicks on new tab
	create_animation(tab_alpha, g_cfg.misc.menu && tab_selector == prev_sel, 0.2f, skip_disable | lerp_animation);

	alpha = tab_alpha;

	auto render_warning_message = [&](const std::string& type, const std::string& type2)
	{
		ImGui::PushFont(RENDER->fonts.dmg.get());

		auto old_pos = ImGui::GetCursorPos();

#ifdef LEGACY
		auto text = /*XOR("Please turn off ") + type +*/ type2 + XOR(" is not available on legacy!");
#else
		auto text = /*XOR("Please turn off ") + type +*/ XOR("Currently ") + type2 + XOR(" is under construction!");
#endif
		auto text_size = ImGui::CalcTextSize(text.c_str());

		auto half_text_size = ImVec2(text_size.x / 2.f, text_size.y / 2.f);
		auto image_size = ImVec2(32, 32);

		auto half_window = ((window_bb.Min + window_bb.Max) - ImVec2(0.f, image_size.y * 2.f)) / 2.f;
		auto half_image = image_size / 2.f;

		list->AddImage((void*)warning_texture, half_window - half_image, half_window + half_image, ImVec2(0, 0), ImVec2(1, 1), c_color(255, 255, 255, 150 * alpha).as_imcolor());

		ImGui::SetCursorPos(ImVec2(new_pos.x + 110.f / 2.f - half_text_size.x, new_pos.y + 290.f / 2.f - half_text_size.y));

		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.6f, 0.6f * alpha));
		ImGui::Text(text.c_str());
		ImGui::PopStyleColor();

		ImGui::PopFont();
		ImGui::SetCursorPos(old_pos);
	};

	if (tab_selector == 7)
	{
		if (!update_configs)
		{
			refresh_configs();
			update_configs = true;
		}
	}
	else
		update_configs = false;

	ImGui::BeginChild(CXOR("##tab_child"), ImVec2(600, 509), false);
	{
		switch (tab_selector)
		{
		case 0:
		{
			// please turn off legit before configuring rage!
			//if (g_cfg.legit.enable)
			//	render_warning_message(XOR("legit"), XOR("rage"));
		//	else
			{
				ImGui::Columns(2, 0, false);
				ImGui::SetColumnOffset(1, 300);
				{
					begin_child(CXOR("Weapon & targeting"))
					{
						combo(CXOR("Setting up..."), &g_cfg.rage.group_type, weapon_configs, IM_ARRAYSIZE(weapon_configs));
						combo(CXOR("Target selection##rage"), &g_cfg.rage.target_selection, target_selections, IM_ARRAYSIZE(target_selections));
						slider_int(CXOR("Maximum FOV##rage"), &g_cfg.rage.max_fov, 1, 180, CXOR("%d deg"));
						if (g_cfg.rage.group_type > 0)
						{
							auto name = XOR("Enable config##group_") + std::to_string(g_cfg.rage.group_type);
							checkbox(name.c_str(), &g_cfg.rage.weapon[g_cfg.rage.group_type].enable);
						}
					}
					end_child;

					begin_child(CXOR("General"))
					{
						checkbox(CXOR("Enable"), &g_cfg.rage.enable);
						checkbox(CXOR("Auto fire"), &g_cfg.rage.auto_fire);

						checkbox(CXOR("Anti aim correction"), &g_cfg.rage.resolver);

						slider_int(CXOR("Enemy desync"), &g_cfg.rage.desync_degree, -58, 58, g_cfg.rage.desync_degree == 0 ? CXOR("Auto") : CXOR("%d"));
					}
					end_child;

					begin_child(CXOR("Exploits"))
					{
						key_bind(CXOR("Double tap"), g_cfg.binds[dt_b]);

#ifndef LEGACY
						key_bind(CXOR("Hide shots"), g_cfg.binds[hs_b]);
#endif

						checkbox(CXOR("Defensive in air"), &g_cfg.rage.air_defensive);
					}
					end_child;

					begin_child(CXOR("Bindings"))
					{
						key_bind(CXOR("Override damage"), g_cfg.binds[override_dmg_b]);
						key_bind(CXOR("Force body"), g_cfg.binds[force_body_b]);

#ifndef LEGACY
						key_bind(CXOR("Force safe point"), g_cfg.binds[force_sp_b]);
#endif

					}
					end_child;

					begin_child(CXOR("Ping spike"))
					{
						int max_spike_amount = (int)(HACKS->max_unlag * 1000.f);

						key_bind(CXOR("Ping spike"), g_cfg.binds[spike_b]);
						slider_int(CXOR("Amount"), &g_cfg.rage.spike_amt, 0, max_spike_amount, CXOR("%d%ms"));
					}
					end_child;
				}
				ImGui::NextColumn();
				{
					auto& weapon_settings = g_cfg.rage.weapon[g_cfg.rage.group_type];

					begin_child(CXOR("Accuracy"))
					{
						if (g_cfg.rage.group_type == global || g_cfg.rage.group_type == auto_snipers || g_cfg.rage.group_type == scout || g_cfg.rage.group_type == awp)
							checkbox(CXOR("Auto scope"), &weapon_settings.auto_scope);

						auto dmg_str = weapon_settings.mindamage == 100 ? CXOR("HP") : weapon_settings.mindamage > 100 ? CXOR("HP + ") + std::to_string(weapon_settings.mindamage - 100) : CXOR("%dHP");
						auto override_str = weapon_settings.damage_override == 100 ? CXOR("HP") : weapon_settings.damage_override > 100 ? CXOR("HP + ") + std::to_string(weapon_settings.damage_override - 100) : CXOR("%dHP");

				//		multi_combo(CXOR("Skip hitchance"), weapon_settings.hitchance_skips, { XOR("Low spread"), XOR("DT / Still") });

						slider_int(CXOR("Hitchance"), &weapon_settings.hitchance, 0, 100, CXOR("%d%%"));

						slider_int(CXOR("Min damage"), &weapon_settings.mindamage, 1, 120, dmg_str.c_str());
						slider_int(CXOR("Min damage (override)"), &weapon_settings.damage_override, 1, 120, override_str.c_str());

						checkbox(CXOR("Quick stop"), &weapon_settings.quick_stop);
						multi_combo(CXOR("Options##quick_stop"), weapon_settings.quick_stop_options, { CXOR("Early"), CXOR("Between shots"), CXOR("Force accuracy"), CXOR("In air")/*, CXOR("Force duck")*/ });
					}
					end_child;

					begin_child(CXOR("Hitscan"))
					{
						multi_combo(CXOR("Hitboxes"), weapon_settings.hitboxes, { XOR("Head"), XOR("Chest"), XOR("Stomach"), XOR("Pelvis"), XOR("Arms"), XOR("Legs") });

						static auto percent_xored = XOR("%d");

						auto scale_head_str = weapon_settings.scale_head == -1 ? CXOR("Auto") : percent_xored;
						auto scale_body_str = weapon_settings.scale_body == -1 ? CXOR("Auto") : percent_xored;

						slider_int(CXOR("Head scale"), &weapon_settings.scale_head, -1, 100, scale_head_str.c_str());
						slider_int(CXOR("Body scale"), &weapon_settings.scale_body, -1, 100, scale_body_str.c_str());
						checkbox( CXOR("Prefer body"), &weapon_settings.prefer_body );
						checkbox( CXOR("Prefer safe point"), &weapon_settings.prefer_safe );

				/*		checkbox(CXOR("Prefer safe point"), &weapon_settings.prefer_safe);
						checkbox(CXOR("Prefer body"), &weapon_settings.prefer_body);*/
					}
					end_child;

					//begin_child(CXOR("Roll resolver"))
					//{
					//	key_bind(CXOR("Roll override"), g_cfg.binds[force_roll_b]);
					//	slider_int(CXOR("Angle"), &g_cfg.rage.roll_amt, -90, 90, CXOR("%d degree"));
					//	slider_int(CXOR("Pitch angle"), &g_cfg.rage.roll_amt_pitch, 0, 20, CXOR("%d degree"));
					//}
					//end_child;
				}
			}
		}
		break;
		case 1:
		{
			ImGui::Columns(2, 0, false);
			ImGui::SetColumnOffset(1, 300);
			{
				begin_child(CXOR("Choose your weapon##legit"))
				{
					combo(CXOR("Setting up...##legit"), &g_cfg.legit.group_type, legit_weapon_configs, IM_ARRAYSIZE(legit_weapon_configs));
				}
				end_child;

				begin_child(CXOR("General##legit"))
				{
					checkbox(CXOR("Enable##legit_cfg"), &g_cfg.legit.enable);
					key_bind(CXOR("Aim##legit_cfg"), g_cfg.binds[toggle_legit_b]);
					combo(CXOR("Target selection##legit"), &g_cfg.legit.target_selection, target_selections, IM_ARRAYSIZE(target_selections));
					checkbox(CXOR("Target lock##legit_cfg"), &g_cfg.legit.target_lock);
					checkbox(CXOR("Autowall##legit_cfg"), &g_cfg.legit.autowall);

					auto dmg_str = g_cfg.legit.min_damage == 100 ? CXOR("HP") : g_cfg.legit.min_damage > 100 ? CXOR("HP + ") + std::to_string(g_cfg.legit.min_damage - 100) : CXOR("%dHP");
					slider_int(CXOR("Min damage##legit"), &g_cfg.legit.min_damage, 1, 120, dmg_str.c_str());

					checkbox(CXOR("Disable on flash"), &g_cfg.legit.flash_check);
					checkbox(CXOR("Disable on smoke"), &g_cfg.legit.smoke_check);
				}
				end_child;

				begin_child(CXOR("RCS settings"))
				{
					auto& rcs_cfg = g_cfg.legit.rcs;
					checkbox(CXOR("Enable##legit_aimbot_rcs"), &rcs_cfg.enable);
					slider_int(CXOR("Amount##legit_aimbot_rcs"), &rcs_cfg.amount, 0, 100, CXOR("%d%%"));
				}
				end_child;
			}
			ImGui::NextColumn();
			{
				begin_child(CXOR("Weapon settings"))
				{
					auto& legit_cfg = g_cfg.legit.legit_weapon[g_cfg.legit.group_type];
					checkbox(CXOR("Enable##legit_aimbot"), &legit_cfg.enable);
					checkbox(CXOR("Draw FOV##legit_aimbot"), &legit_cfg.fov_cicle);
					color_picker(CXOR("FOV color##legit_aimbot"), legit_cfg.circle_color);
					checkbox(CXOR("Backtrack##legit_aimbot"), &legit_cfg.backtrack);
					checkbox(CXOR("Quick stop##legit_aimbot"), &legit_cfg.quick_stop);

					multi_combo(CXOR("Hitboxes##legit"), legit_cfg.hitboxes, { XOR("Head"), XOR("Chest"), XOR("Stomach"), XOR("Pelvis") });
					slider_int(CXOR("FOV##legit"), &legit_cfg.fov, 1, 30, CXOR("%d deg"));
					slider_int(CXOR("Smooth##legit"), &legit_cfg.smooth, 1, 100);
					slider_int(CXOR("Shot delay##legit"), &legit_cfg.aim_delay, 0, 1000, CXOR("%d ms"));
				}
				end_child;

				begin_child(CXOR("Triggerbot"))
				{
					checkbox(CXOR("Enable##legit_trigger"), &g_cfg.legit.trigger.enable);
					key_bind(CXOR("Trigger##legit_cfg"), g_cfg.binds[toggle_trigger_b]);
					slider_int(CXOR("Delay##legit_trigger"), &g_cfg.legit.trigger.delay, 0, 500, CXOR("%d ms"));
				}
				end_child;
			}
		}
		break;
		case 2:
		{
			ImGui::Columns(2, 0, false);
			ImGui::SetColumnOffset(1, 300);
			{
				begin_child(CXOR("Main"))
				{
					checkbox(CXOR("Enable##antiaim"), &g_cfg.antihit.enable);
#ifndef LEGACY
					//checkbox(CXOR("Silent on-shot##antiaim"), &g_cfg.antihit.silent_onshot);
#endif
					checkbox(CXOR("At targets##antiaim"), &g_cfg.antihit.at_targets);

					combo(CXOR("Pitch"), &g_cfg.antihit.pitch, aa_pitch, IM_ARRAYSIZE(aa_pitch));
					combo(CXOR("Yaw"), &g_cfg.antihit.yaw, aa_yaw, IM_ARRAYSIZE(aa_yaw));
					slider_int(CXOR("Yaw add"), &g_cfg.antihit.yaw_add, -180, 180);

					combo(CXOR("Yaw jitter"), &g_cfg.antihit.jitter_mode, aa_jitter_yaw, IM_ARRAYSIZE(aa_jitter_yaw));
					if (g_cfg.antihit.jitter_mode > 0)
						slider_int(CXOR("Range##jitter"), &g_cfg.antihit.jitter_range, -180, 180);

					key_bind(CXOR("Edge yaw"), g_cfg.binds[edge_b]);
					key_bind(CXOR("Freestanding"), g_cfg.binds[freestand_b]);

					key_bind(CXOR("Manual left"), g_cfg.binds[left_b]);
					key_bind(CXOR("Manual right"), g_cfg.binds[right_b]);
					key_bind(CXOR("Manual back"), g_cfg.binds[back_b]);
				}
				end_child;

#ifndef LEGACY
				begin_child(CXOR("Defensive Anti-Aims"))
				{
					checkbox(CXOR("Change pitch##antiaim"), &g_cfg.antihit.def_pitch);
					checkbox(CXOR("Change yaw##antiaim"), &g_cfg.antihit.def_yaw);

					combo(CXOR("Defensive type##antiaim"), &g_cfg.antihit.def_aa_mode, defensive_aa_mode, IM_ARRAYSIZE(defensive_aa_mode));
				}
				end_child;
#endif
			}
			ImGui::NextColumn();
			{
				begin_child(CXOR("Fake lag"))
				{
					checkbox(CXOR("Enable##fakelag"), &g_cfg.antihit.fakelag);

					multi_combo(CXOR("Conditions##fakelag"), g_cfg.antihit.fakelag_conditions,
						{
						  XOR("Standing"),
						  XOR("Moving"),
						  XOR("Air"),
						});

					slider_int(CXOR("Limit"), &g_cfg.antihit.fakelag_limit, 1, HACKS->max_choke ? HACKS->max_choke + 1 : 15, CXOR("%d ticks"));

					checkbox(CXOR("Fluctuate in air##fakelag"), &g_cfg.antihit.fluctiate_in_air);
				}
				end_child;

#ifndef LEGACY
				begin_child(CXOR("Enhancement"))
				{
				//	checkbox(CXOR("Jitter move"), &g_cfg.antihit.jitter_move);
					checkbox(CXOR("Randomize yaw jitter"), &g_cfg.antihit.random_jitter);
					checkbox(CXOR("Randomize fake jitter"), &g_cfg.antihit.random_dsy);
					checkbox(CXOR("Randomize fake amount"), &g_cfg.antihit.random_amount);
				}
				end_child;
#endif

				begin_child(CXOR("Movement"))
				{
#ifdef LEGACY
					key_bind(CXOR("Fake walk"), g_cfg.binds[sw_b]);
#else
					key_bind(CXOR("Fake duck"), g_cfg.binds[fd_b]);
					key_bind(CXOR("Slow walk"), g_cfg.binds[sw_b]);
#endif
				}
				end_child;

				begin_child(CXOR("Fake angle"))
				{
					checkbox(CXOR("Enable##fake_angle"), &g_cfg.antihit.desync);
					key_bind(CXOR("Inverter"), g_cfg.binds[inv_b]);
#ifndef LEGACY
					combo(CXOR("Fake type"), &g_cfg.antihit.desync_mode, aa_desync_type, IM_ARRAYSIZE(aa_desync_type));
					slider_int(CXOR("Left amount##desync"), &g_cfg.antihit.desync_left, 0, 58, CXOR("%d"));
					slider_int(CXOR("Right amount##desync"), &g_cfg.antihit.desync_right, 0, 58, CXOR("%d"));
#else
				//	combo(CXOR("Breaker type"), &g_cfg.antihit.desync_mode, aa_desync_type, IM_ARRAYSIZE(aa_desync_type));
					slider_int(CXOR("LBY Delta##desync"), &g_cfg.antihit.desync_left, 0, 180, CXOR("%d"));
#endif
				}
				end_child;

#ifndef LEGACY
				begin_child(CXOR("Extended fake"))
				{
					checkbox(CXOR("Enable##distortion"), &g_cfg.antihit.distortion);
					key_bind(CXOR("Force extend"), g_cfg.binds[ens_lean_b]);

					slider_int(CXOR("Amount##body_lean"), &g_cfg.antihit.distortion_range, 0, 100, CXOR("%d%%"));
					slider_int(CXOR("Height##body_lean"), &g_cfg.antihit.distortion_pitch, 0, 50);
				}
				end_child;
#endif
			}
		}
		break;
		case 3:
		{
			static int selector = 0;
			draw_sub_tabs(selector, player_tabs);

			static int prev_sub_sel = 0;
			if (selector != prev_sub_sel)
			{
				for (auto& a : item_animations)
				{
					a.second.reset();
					subtab_alpha = 0.f;
				}

				prev_sub_sel = selector;
			}

			create_animation(subtab_alpha, g_cfg.misc.menu && selector == prev_sub_sel, 0.2f, skip_disable | lerp_animation);
			alpha = subtab_alpha * tab_alpha;

			ImGui::SetCursorPosX(0);
			ImGui::BeginChild(CXOR("subtab_players"), {}, false);
			switch (selector)
			{
			case 0:
			{
				ImGui::Columns(2, 0, false);
				ImGui::SetColumnOffset(1, 300);
				{
					begin_child("General ");
					{
						auto& enemy_esp = g_cfg.visuals.esp[esp_enemy];
						checkbox(CXOR("Enable##esp_enemy"), &enemy_esp.enable);
						multi_combo(CXOR("Elements##esp_enemy"), enemy_esp.elements,
							{
							  XOR("Box"),
							  XOR("Name"),
							  XOR("Health"),
							  XOR("Icon"),
							  XOR("Weapon"),
							  XOR("Ammo"),
							  XOR("Flags"),
							  XOR("OOF Arrow"),
							  XOR("Glow"),
#if _DEBUG || ALPHA || BETA
							  XOR("Resolver mode"),
#endif
							//  XOR("Skeleton"),
							});

						if (enemy_esp.elements & 64)
							multi_combo(CXOR("Flags##esp_enemy_flags"), enemy_esp.flags,
								{ XOR("Armor"), XOR("Unarmored"), XOR("Defuser"), XOR("Blind"), XOR("Scoped"), XOR("Reload"), XOR("Immunity"), XOR("Slowed"), XOR("Defuse"), XOR("Bomb"), XOR("Planting"), XOR("Pin pulled"), XOR("Money") });
						if (enemy_esp.elements & 1)
							color_picker(CXOR("Box color##esp_enemy"), enemy_esp.colors.box);

						if (enemy_esp.elements & 2)
							color_picker(CXOR("Name color##esp_enemy"), enemy_esp.colors.name);

						if (enemy_esp.elements & 4)
							color_picker(CXOR("Health color##esp_enemy"), enemy_esp.colors.health);

						if (enemy_esp.elements & 8 || enemy_esp.elements & 16)
							color_picker(CXOR("Weapon color##esp_enemy"), enemy_esp.colors.weapon);

						if (enemy_esp.elements & 32)
							color_picker(CXOR("Ammo color##esp_enemy"), enemy_esp.colors.ammo_bar);

						if (enemy_esp.elements & 256)
							color_picker(CXOR("Glow color##esp_enemy"), enemy_esp.colors.glow);

						/*if (enemy_esp.elements & 512)
							color_picker(CXOR("Skeleton color##esp_enemy"), enemy_esp.colors.skeleton);*/
					}
					end_child;
				}
				ImGui::NextColumn();
				{
					auto& enemy_esp = g_cfg.visuals.esp[esp_enemy];

					begin_child(CXOR("Chams  "));
					{
						static int chams_model = 0;

						combo(CXOR("Type"), &chams_model, enemy_models, IM_ARRAYSIZE(enemy_models));

						auto& enemy_config = g_cfg.visuals.chams[chams_model];

						checkbox(CXOR("Enable"), &enemy_config.enable);
						if (chams_model == 2)
							checkbox(CXOR("Full history"), &g_cfg.visuals.show_all_history);

						combo(CXOR("Material"), &enemy_config.material, vis_chams_type, IM_ARRAYSIZE(vis_chams_type));
						color_picker(CXOR("Material color"), enemy_config.main_color);
						if (enemy_config.material == 4)
						{
							slider_int(CXOR("Fill"), &enemy_config.glow_fill, 0, 100, CXOR("%d%%"));
							color_picker(CXOR("Glow color"), enemy_config.glow_color);
						}

						if (chams_model == 3)
							slider_int(CXOR("Shot duration"), &enemy_config.shot_duration, 1, 10, CXOR("%d s"));

						if (enemy_config.material == 6)
							input_text(CXOR("Sprite path"), enemy_config.chams_sprite, 128);
					}
					end_child;

					begin_child(CXOR("Player OOF "));
					{
						slider_int(CXOR("Distance  "), &enemy_esp.offscreen_dist, 1, 450, CXOR("%d ft"));
						slider_int(CXOR("Size  "), &enemy_esp.offscreen_size, 5, 25, CXOR("%d px"));
						color_picker(CXOR("Color##esp_enemy_arrow"), enemy_esp.colors.offscreen_arrow);
						color_picker(CXOR("Outline color##esp_enemy_arrow"), enemy_esp.colors.offscreen_arrow_outline);
					}
					end_child;

					begin_child(CXOR("Sound ESP"))
					{
						checkbox(CXOR("Enable##sound_esp"), &g_cfg.visuals.sound_esp.enable);
						if (g_cfg.visuals.sound_esp.enable)
						{
							slider_int(CXOR("Duration##sound_esp"), &g_cfg.visuals.sound_esp.duration, 300, 3000, CXOR("%d ms"));
							slider_int(CXOR("Radius##sound_esp"), &g_cfg.visuals.sound_esp.radius, 10, 80, CXOR("%d px"));
							color_picker(CXOR("Color##sound_esp"), g_cfg.visuals.sound_esp.color);
						}
					}
					end_child;
				}
			}
			break;
			case 1:
			{
				ImGui::Columns(2, 0, false);
				ImGui::SetColumnOffset(1, 300);
				{
					begin_child(CXOR("Teammates"))
					{
						auto& team_esp = g_cfg.visuals.esp[esp_teammate];
						checkbox(CXOR("Enabled##esp_teammate"), &team_esp.enable);
						multi_combo(CXOR("ESP items##esp_teammate"), team_esp.elements,
							{ XOR("Bounding box"), XOR("Name"), XOR("Health bar"), XOR("Weapon icon"), XOR("Weapon text"), XOR("Ammo bar"), XOR("Flags"), XOR("Offscreen arrow"), XOR("Glow") });
						if (team_esp.elements & 64)
							multi_combo(CXOR("Flags##esp_teammate_flags"), team_esp.flags,
								{ XOR("Armor"), XOR("Unarmored"), XOR("Defuser"), XOR("Blind"), XOR("Scoped"), XOR("Reload"), XOR("Immunity"), XOR("Slowed"), XOR("Defuse"), XOR("Bomb"), XOR("Planting"), XOR("Pin pulled"), XOR("Money") });
						checkbox(CXOR("Disable Model Rendering##teammates"), &team_esp.disable_model);
						if (team_esp.elements & 1)
							color_picker(CXOR("Box color##esp_teammate"), team_esp.colors.box);
						if (team_esp.elements & 2)
							color_picker(CXOR("Name color##esp_teammate"), team_esp.colors.name);
						if (team_esp.elements & 4)
							color_picker(CXOR("Health color##esp_teammate"), team_esp.colors.health);
						if (team_esp.elements & 8 || team_esp.elements & 16)
							color_picker(CXOR("Weapon color##esp_teammate"), team_esp.colors.weapon);
						if (team_esp.elements & 32)
							color_picker(CXOR("Ammo color##esp_teammate"), team_esp.colors.ammo_bar);
						if (team_esp.elements & 256)
							color_picker(CXOR("Glow color##esp_teammate"), team_esp.colors.glow);
					}
					end_child;
				}
				ImGui::NextColumn();
				{
					auto& team_esp = g_cfg.visuals.esp[esp_teammate];
					begin_child(CXOR("Chams##teammates"))
					{
						static int team_chams_model = 0;
						const char* team_models[] = { CXOR("Player"), CXOR("Behind walls") };
						combo(CXOR("Type##team_chams"), &team_chams_model, team_models, IM_ARRAYSIZE(team_models));
						auto& team_chams = g_cfg.visuals.chams[c_team_vis + team_chams_model];
						checkbox(CXOR("Enable##team_chams"), &team_chams.enable);
						combo(CXOR("Material##team_chams"), &team_chams.material, vis_chams_type, IM_ARRAYSIZE(vis_chams_type));
						color_picker(CXOR("Material color##team_chams"), team_chams.main_color);
						if (team_chams.material == 4)
						{
							slider_int(CXOR("Fill##team_chams"), &team_chams.glow_fill, 0, 100, CXOR("%d%%"));
							color_picker(CXOR("Glow color##team_chams"), team_chams.glow_color);
						}
						if (team_chams.material == 6)
							input_text(CXOR("Sprite path##team_chams"), team_chams.chams_sprite, 128);
					}
					end_child;

					begin_child(CXOR("Offscreen arrow##team"))
					{
						slider_int(CXOR("Distance##team_oof"), &team_esp.offscreen_dist, 1, 450, CXOR("%d ft"));
						slider_int(CXOR("Size##team_oof"), &team_esp.offscreen_size, 5, 25, CXOR("%d px"));
						color_picker(CXOR("Color##esp_teammate_arrow"), team_esp.colors.offscreen_arrow);
						color_picker(CXOR("Outline##esp_teammate_arrow"), team_esp.colors.offscreen_arrow_outline);
					}
					end_child;
				}
			}
			break;
			case 2:
			{
				ImGui::Columns(2, 0, false);
				ImGui::SetColumnOffset(1, 300);
				{
					begin_child(CXOR("General    "))
					{
						checkbox(CXOR("Glow##local_player"), &g_cfg.visuals.local_glow);

						if (g_cfg.visuals.local_glow)
							color_picker(CXOR("Glow color##local_esp_glow"), g_cfg.visuals.local_glow_color);

						slider_int(CXOR("Local Model transparency"), &g_cfg.misc.local_model_transparency, 0, 100, CXOR("%d%%"));
						slider_int(CXOR("Local Weapon transparency"), &g_cfg.misc.attachments_amt, 0, 100, CXOR("%d%%"));
					}
					end_child;

					begin_child(CXOR("Crosshair    "))
					{
						checkbox(CXOR("Penetration crosshair"), &g_cfg.misc.pen_xhair);
						checkbox(CXOR("Force crosshair on sniper"), &g_cfg.misc.snip_crosshair);
						checkbox(CXOR("Fix zoom sensitivity"), &g_cfg.misc.fix_sensitivity);
					}
					end_child;
				}
				ImGui::NextColumn();
				{
					begin_child(CXOR("Chams    "))
					{
						static int chams_local_model = 0;

						combo(CXOR("Type##local"), &chams_local_model, local_models, IM_ARRAYSIZE(local_models));

						auto& local_config = g_cfg.visuals.chams[chams_local_model + 5];

						checkbox(CXOR("Enable##local"), &local_config.enable);

						combo(CXOR("Material##local"), &local_config.material, vis_chams_type, IM_ARRAYSIZE(vis_chams_type));
						color_picker(CXOR("Material color##local"), local_config.main_color);
						if (local_config.material == 4)
						{
							slider_int(CXOR("Fill##local"), &local_config.glow_fill, 0, 100, CXOR("%d%%"));
							color_picker(CXOR("Glow color##local"), local_config.glow_color);
						}

						if (local_config.material == 6)
							input_text(CXOR("Sprite path"), local_config.chams_sprite, 128);
					}
					end_child;
				}
			}
			break;
			}
			ImGui::EndChild(false);

			ImGui::PopClipRect();
			draw_list->PopClipRect();
		}
		break;
		case 4:
		{
			static int selector = 0;
			static int world_profile_scope = 0;
			const auto map_name = get_world_map_name();
			const char* profile_names[] = { CXOR("Global"), map_name.empty() ? CXOR("Current map") : map_name.c_str() };
			auto* map_profile = world_profile_scope == 1 ? get_map_world_settings(true) : nullptr;
			auto& world_cfg = map_profile ? *map_profile : static_cast<world_settings_t&>(g_cfg.misc);
			draw_sub_tabs(selector, world_tabs);

			static int prev_sub_sel = 0;
			if (selector != prev_sub_sel)
			{
				for (auto& a : item_animations)
				{
					a.second.reset();
					subtab_alpha = 0.f;
				}

				prev_sub_sel = selector;
			}

			create_animation(subtab_alpha, g_cfg.misc.menu && selector == prev_sub_sel, 0.2f, skip_disable | lerp_animation);
			alpha = subtab_alpha * tab_alpha;

			ImGui::SetCursorPosX(0);
			ImGui::BeginChild(CXOR("subtab_world"), {}, false);
			switch (selector)
			{
			case 0:
			{
				ImGui::Columns(2, 0, false);
				ImGui::SetColumnOffset(1, 300);
				{
					begin_child(CXOR("General     "))
					{
						auto& weapon_esp = g_cfg.visuals.esp[esp_weapon];
						checkbox(CXOR("Enable##esp_weapon"), &weapon_esp.enable);
						multi_combo(CXOR("Elements##esp_weapon"), weapon_esp.elements,
							{
							  XOR("Box"),
							  XOR("Icon"),
							  XOR("Name"),
							  XOR("Ammo"),
							  XOR("Inferno"),
							  XOR("Smoke"),
							  XOR("Glow"),
							});

						if (weapon_esp.elements & 1)
							color_picker(CXOR("Box color##esp_weapon"), weapon_esp.colors.box);

						if (weapon_esp.elements & 2 || weapon_esp.elements & 4)
							color_picker(CXOR("Name color##esp_weapon"), weapon_esp.colors.name);

						if (weapon_esp.elements & 8)
							color_picker(CXOR("Ammo color##esp_weapon"), weapon_esp.colors.ammo_bar);

						if (weapon_esp.elements & 16)
							color_picker(CXOR("Inferno color##esp_weapon"), weapon_esp.colors.molotov_range);

						if (weapon_esp.elements & 32)
							color_picker(CXOR("Smoke color##esp_weapon"), weapon_esp.colors.smoke_range);

						if (weapon_esp.elements & 64)
							color_picker(CXOR("Glow color##esp_weapon"), weapon_esp.colors.glow);
					}
					end_child;
				}
				ImGui::NextColumn();
				{
					begin_child(CXOR("Predictions"))
					{
						checkbox(CXOR("Projectile prediction"), &g_cfg.visuals.grenade_predict);
						if (g_cfg.visuals.grenade_predict)
							color_picker(CXOR("Prediction color"), g_cfg.visuals.predict_clr);

						checkbox(CXOR("Projectile warning"), &g_cfg.visuals.grenade_warning);
						checkbox(CXOR("Warning line"), &g_cfg.visuals.grenade_warning_line);
						color_picker(CXOR("Warning color"), g_cfg.visuals.warning_clr);
					}
					end_child;

					auto& weapon_esp = g_cfg.visuals.esp[esp_weapon];
					begin_child(CXOR("Prediction OOF "));
					{
						checkbox(CXOR("Enable##esp_pred_arrow"), &weapon_esp.nade_offscreen);
						slider_int(CXOR("Distance  "), &weapon_esp.offscreen_dist, 1, 450, CXOR("%d ft"));
						slider_int(CXOR("Size  "), &weapon_esp.offscreen_size, 5, 25, CXOR("%d px"));
						color_picker(CXOR("Color##esp_pred_arrow"), weapon_esp.colors.offscreen_arrow);
						color_picker(CXOR("Outline color##esp_pred_arrow"), weapon_esp.colors.offscreen_arrow_outline);
					}
					end_child;
				}
			}
			break;
			case 1:
			{
				ImGui::Columns(2, 0, false);
				ImGui::SetColumnOffset(1, 300);
				{
					begin_child(CXOR("Map selection"))
					{
						combo(CXOR("Profile##world_modulation"), &world_profile_scope, profile_names, 2);
						checkbox(CXOR("Use per-map profiles"), &g_cfg.misc.per_map_world);
					}
					end_child;

					begin_child(CXOR("Sky & props"))
					{
						combo(CXOR("Skybox"), &world_cfg.skybox, skyboxes, IM_ARRAYSIZE(skyboxes));
						if (world_cfg.skybox == 23)
							input_text(CXOR("Sky name"), world_cfg.skybox_name, 128);

						slider_int(CXOR("Props alpha"), &world_cfg.prop_alpha, 0, 100, CXOR("%d%%"));
					}
					end_child;
				}
				ImGui::NextColumn();
				{
					begin_child(CXOR("World modulation"))
					{
						multi_combo(CXOR("Modifiers"), world_cfg.world_modulation,
							{
								XOR("Night mode"),
								XOR("Sunset mode"),
								XOR("Fullbright")
							});

						if (world_cfg.world_modulation & 1)
						{
							color_picker(CXOR("World color"), world_cfg.world_clr[world], no_alpha_picker);
							color_picker(CXOR("Props color"), world_cfg.world_clr[props], no_alpha_picker);
							color_picker(CXOR("Skybox color"), world_cfg.world_clr[sky], no_alpha_picker);
						}

						if (world_cfg.world_modulation & 2)
						{
							slider_int(CXOR("Sun pitch"), &world_cfg.sunset_angle.x, -180, 180, CXOR("%d degrees"));
							slider_int(CXOR("Sun yaw"), &world_cfg.sunset_angle.y, -180, 180, CXOR("%d degrees"));
							slider_int(CXOR("Sun roll"), &world_cfg.sunset_angle.z, -180, 180, CXOR("%d degrees"));
						}
					}
					end_child;
				}
			}
			break;
			case 2:
			{
				ImGui::Columns(2, 0, false);
				ImGui::SetColumnOffset(1, 300);
				{
					begin_child(CXOR("Map selection##effects"))
					{
						combo(CXOR("Profile##world_effects"), &world_profile_scope, profile_names, 2);
					}
					end_child;

					begin_child(CXOR("Screen effects"))
					{
						multi_combo(CXOR("Removals"), g_cfg.misc.removals,
							{
								XOR("Scope"),
								XOR("Visual recoil"),
								XOR("Post processing"),
								XOR("Smoke"),
								XOR("Flash"),
								XOR("Fog"),
								XOR("Shadows"),
								XOR("Landing bob"),
							});
					}
					end_child;

					begin_child(CXOR("World bloom"))
					{
						checkbox(CXOR("Customize bloom"), &world_cfg.custom_bloom);
						if (world_cfg.custom_bloom)
						{
							slider_int(CXOR("Bloom scale"), &world_cfg.bloom_scale, 0, 5);
							slider_int(CXOR("Exposure min"), &world_cfg.exposure_min, 1, 10);
							slider_int(CXOR("Exposure max"), &world_cfg.exposure_max, 1, 10);
						}
					}
					end_child;
				}
				ImGui::NextColumn();
				{
					begin_child(CXOR("World fog"))
					{
						checkbox(CXOR("Customize fog"), &world_cfg.custom_fog);
						if (world_cfg.custom_fog)
						{
							slider_int(CXOR("Fog start"), &world_cfg.fog_start, 0, 10000);
							slider_int(CXOR("Fog end"), &world_cfg.fog_end, 0, 10000);
							slider_int(CXOR("Density"), &world_cfg.fog_density, 0, 100, CXOR("%d%%"));
							color_picker(CXOR("Fog color"), world_cfg.world_clr[fog], no_alpha_picker);
						}
					}
					end_child;
				}
			}
			break;
			case 3:
			{
				ImGui::Columns(2, 0, false);
				ImGui::SetColumnOffset(1, 300);
				{
					begin_child(CXOR("World camera"))
					{
						key_bind(CXOR("Thirdperson"), g_cfg.binds[tp_b]);
						slider_int(CXOR("Distance"), &g_cfg.misc.thirdperson_dist, 10, 300);
						checkbox(CXOR("While dead"), &g_cfg.misc.thirdperson_dead);
						checkbox(CXOR("Disable close to wall"), &g_cfg.misc.thirdperson_disable_on_collision);
					}
					end_child;

					begin_child(CXOR("Viewmodel  "))
					{
						slider_int(CXOR("Viewmodel FOV"), &g_cfg.misc.fovs[arms], 0, 30);
						checkbox(CXOR("Force Viewmodel FOV"), &g_cfg.misc.force_viewmodel_fov);
						checkbox(CXOR("Show viewmodel in scope"), &g_cfg.misc.viewmodel_scope);
						slider_int(CXOR("Offset X"), &g_cfg.misc.viewmodel_pos[0], -20, 20);
						slider_int(CXOR("Offset Y"), &g_cfg.misc.viewmodel_pos[1], -20, 20);
						slider_int(CXOR("Offset Z"), &g_cfg.misc.viewmodel_pos[2], -20, 20);
						slider_int(CXOR("Angle pitch##viewmodel"), &g_cfg.misc.viewmodel_angles[0], -90, 90);
						slider_int(CXOR("Angle yaw##viewmodel"), &g_cfg.misc.viewmodel_angles[1], -180, 180);
						slider_int(CXOR("Angle roll##viewmodel"), &g_cfg.misc.viewmodel_angles[2], -90, 90);
						checkbox(CXOR("Override modifiers##viewmodel"), &g_cfg.misc.viewmodel_modifiers);
						if (g_cfg.misc.viewmodel_modifiers)
						{
							slider_int(CXOR("Bobbing##viewmodel"), &g_cfg.misc.viewmodel_bob, 0, 200, CXOR("%d%%"));
							slider_int(CXOR("Rotation sway##viewmodel"), &g_cfg.misc.viewmodel_sway, 0, 200, CXOR("%d%%"));
							slider_int(CXOR("Lower weapon##viewmodel"), &g_cfg.misc.viewmodel_lower, 0, 200, CXOR("%d%%"));
						}
					}
					end_child;
				}
				ImGui::NextColumn();
				{
					begin_child(CXOR("World view"))
					{
						slider_int(CXOR("Aspect ratio"), &g_cfg.misc.aspect_ratio, 0, 200);
						slider_int(CXOR("FOV"), &g_cfg.misc.fovs[world], 0, 60);
						slider_int(CXOR("Scale (x1)##scope"), &g_cfg.misc.fovs[zoom], 0, 100, CXOR("%d%%"));
						slider_int(CXOR("Scale (x2)##scope"), &g_cfg.misc.zoom_fov_second, 0, 100, CXOR("%d%%"));
						checkbox(CXOR("Instant Scope"), &g_cfg.misc.instant_scope);
						checkbox(CXOR("Skip second zoom"), &g_cfg.misc.skip_second_zoom);
					}
					end_child;
				}
			}
			break;
			case 4:
			{
				ImGui::Columns(2, 0, false);
				ImGui::SetColumnOffset(1, 300);
				{
					begin_child(CXOR("HUD"))
					{
						checkbox(CXOR("Radar reveal"), &g_cfg.misc.force_radar);
						checkbox(CXOR("Inaccuracy Overlay"), &g_cfg.misc.inaccuracy_overlay);
						color_picker(CXOR("Overlay color##inaccuracy"), g_cfg.misc.inaccuracy_color);
						checkbox(CXOR("Death Notices"), &g_cfg.misc.preverse_killfeed);
						if (g_cfg.misc.preverse_killfeed)
							slider_int(CXOR("Notice duration"), &g_cfg.misc.killfeed_time, 1, 60, CXOR("%d s"));
						multi_combo(CXOR("Windows##hud"), g_cfg.misc.menu_indicators, { XOR("Keybind list"), XOR("Bomb"), XOR("Watermark"), XOR("Spectators") });
					}
					end_child;
				}
				ImGui::NextColumn();
				{
					begin_child(CXOR("Crosshairs"))
					{
						checkbox(CXOR("Penetration crosshair"), &g_cfg.misc.pen_xhair);
						checkbox(CXOR("Sniper crosshair"), &g_cfg.misc.snip_crosshair);
						checkbox(CXOR("Fix zoom sensitivity"), &g_cfg.misc.fix_sensitivity);
					}
					end_child;

					begin_child(CXOR("Other HUD"))
					{
						checkbox(CXOR("Unlock inventory"), &g_cfg.misc.unlock_inventory);
						checkbox(CXOR("Remove server ads"), &g_cfg.misc.remove_ads);
					}
					end_child;
				}
			}
			break;
			}
			ImGui::EndChild(false);

			ImGui::PopClipRect();
			draw_list->PopClipRect();
		}
		break;
		case 6:
		{
			static int selector2 = 0;
			draw_sub_tabs(selector2, misc_tabs);

			static int prev_sub_sel2 = 0;
			if (selector2 != prev_sub_sel2)
			{
				for (auto& a : item_animations)
				{
					a.second.reset();
					subtab_alpha2 = 0.f;
				}

				prev_sub_sel2 = selector2;
			}

			create_animation(subtab_alpha2, g_cfg.misc.menu && selector2 == prev_sub_sel2, 0.2f, skip_disable | lerp_animation);
			alpha = subtab_alpha2 * tab_alpha;

			ImGui::SetCursorPosX(0);
			ImGui::BeginChild(CXOR("subtab_misc"), {}, false);
			switch (selector2)
			{
			case 0:
			{
				ImGui::Columns(2, 0, false);
				ImGui::SetColumnOffset(1, 300);
				{
					begin_child(CXOR("Main"))
					{
						checkbox(CXOR("Bunny Hop"), &g_cfg.misc.auto_jump);

						checkbox(CXOR("Air Strafe"), &g_cfg.misc.auto_strafe);
						const char* strafe_modes[] = { CXOR("View angles"), CXOR("Movement keys") };
						combo(CXOR("Strafe direction"), &g_cfg.misc.strafe_mode, strafe_modes, 2);
						checkbox(CXOR("Only while holding jump"), &g_cfg.misc.strafe_only_jump);
						checkbox(CXOR("Stop on Shift key"), &g_cfg.misc.strafe_stop_on_shift);
						slider_int(CXOR("Turn Speed"), &g_cfg.misc.strafe_smooth, 0, 100, CXOR("%d%%"));

						key_bind(CXOR("Edge Jump"), g_cfg.binds[ej_b]);

						checkbox(CXOR("Standalone Quick Stop"), &g_cfg.misc.fast_stop);
						checkbox(CXOR("Slide Walk"), &g_cfg.misc.slide_walk);
						key_bind(CXOR("Slow Walk"), g_cfg.binds[sw_b]);
					}
					end_child;
				}
				ImGui::NextColumn();
				{
					begin_child(CXOR("Helpers"))
					{
						key_bind(CXOR("Auto peek"), g_cfg.binds[ap_b]);
						color_picker(CXOR("Start color##peek"), g_cfg.misc.autopeek_clr);
						color_picker(CXOR("Move color##peek"), g_cfg.misc.autopeek_clr_back);
						checkbox(CXOR("Return on key release"), &g_cfg.misc.retrack_peek);
						checkbox(CXOR("Knife bot"), &g_cfg.misc.knife_bot);
						checkbox(CXOR("Quick Switch"), &g_cfg.misc.quick_switch);
						checkbox(CXOR("Automatic Grenade Release"), &g_cfg.misc.automatic_grenade_release);
						if (g_cfg.misc.automatic_grenade_release)
							slider_int(CXOR("Release delay"), &g_cfg.misc.grenade_release_delay, 0, 3000, CXOR("%d ms"));
						checkbox(CXOR("Prevent AFK"), &g_cfg.misc.prevent_afk);
					}
					end_child;
				}
			}
			break;
			case 1:
			{
				ImGui::Columns(2, 0, false);
				ImGui::SetColumnOffset(1, 300);
				{
					begin_child(CXOR("Tracers"))
					{
						checkbox(CXOR("Bullet impacts"), &g_cfg.misc.impacts);
						color_picker(CXOR("Server color"), g_cfg.misc.server_clr);
						color_picker(CXOR("Client color"), g_cfg.misc.client_clr);
						slider_int(CXOR("Size"), &g_cfg.misc.impact_size, 5, 15);
						slider_int(CXOR("Impact duration"), &g_cfg.misc.impact_duration, 1, 15, CXOR("%d s"));

						multi_combo(CXOR("Bullet tracers"), g_cfg.misc.tracers, { XOR("Enemy"), XOR("Team"), XOR("Local") });

						combo(CXOR("Tracer type"), &g_cfg.misc.tracer_type, tracers, IM_ARRAYSIZE(tracers));
						slider_int(CXOR("Bullet size"), &g_cfg.misc.bullet_size, 0, 10);
						slider_int(CXOR("Tracer duration"), &g_cfg.misc.tracer_duration, 1, 10, CXOR("%d s"));

						if (g_cfg.misc.tracers & 1)
							color_picker(CXOR("Enemy tracer"), g_cfg.misc.trace_clr[0]);

						if (g_cfg.misc.tracers & 2)
							color_picker(CXOR("Team tracer"), g_cfg.misc.trace_clr[1]);

						if (g_cfg.misc.tracers & 4)
							color_picker(CXOR("Local tracer"), g_cfg.misc.trace_clr[2]);
					}
					end_child;

					begin_child(CXOR("Event log"))
					{
						checkbox(CXOR("Enable##eventlog"), &g_cfg.visuals.eventlog.enable);
						multi_combo(CXOR("Type##eventlog"), g_cfg.visuals.eventlog.logs, { XOR("Hits"), XOR("Misses"), XOR("Debug"), XOR("Purchases"), XOR("Bomb plant") });
						checkbox(CXOR("Filter console##eventlog"), &g_cfg.visuals.eventlog.filter_console);
					}
					end_child;
				}
				ImGui::NextColumn();
				{
					begin_child(CXOR("Hit indicators"))
					{
						multi_combo(CXOR("Hitmarker"), g_cfg.misc.hitmarker, { XOR("On enemy"), XOR("On screen") });
						color_picker(CXOR("Hitmarker color"), g_cfg.misc.hitmarker_clr);

						combo(CXOR("Hitsound"), &g_cfg.misc.sound, hitsound, IM_ARRAYSIZE(hitsound));
						slider_int(CXOR("Volume"), &g_cfg.misc.sound_volume, 0, 100, CXOR("%d%%"));

						if (g_cfg.misc.sound == 2)
							input_text(CXOR("Sound name"), g_cfg.misc.sound_name, 128);

						checkbox(CXOR("Damage indicator"), &g_cfg.misc.damage);
						color_picker(CXOR("Indicator color"), g_cfg.misc.damage_clr);
					}
					end_child;

					begin_child(CXOR("Buy bot"))
					{
						checkbox(CXOR("Enable##buybot"), &g_cfg.misc.buybot.enable);
						combo(CXOR("Primary weapon##buybot"), &g_cfg.misc.buybot.main_weapon, buybot_main, IM_ARRAYSIZE(buybot_main));
						combo(CXOR("Secondary weapon##buybot"), &g_cfg.misc.buybot.second_weapon, buybot_second, IM_ARRAYSIZE(buybot_second));

						multi_combo(CXOR("Items##buybot"), g_cfg.misc.buybot.other_items,
							{ XOR("Head armor"), XOR("Armor"), XOR("HE grenade"), XOR("Molotov"), XOR("Smoke"), XOR("Taser"), XOR("Defuse kit") });
					}
					end_child;
				}
			}
			break;
			case 2:
			{
				ImGui::Columns(2, 0, false);
				ImGui::SetColumnOffset(1, 300);
				{
					begin_child(CXOR("Hud"))
					{
						checkbox(CXOR("Force engine radar"), &g_cfg.misc.force_radar);
						checkbox(CXOR("Preserve killfeed"), &g_cfg.misc.preverse_killfeed);
						if (g_cfg.misc.preverse_killfeed)
							slider_int(CXOR("Killfeed duration"), &g_cfg.misc.killfeed_time, 1, 60, CXOR("%d s"));
						checkbox(CXOR("Unlock inventory"), &g_cfg.misc.unlock_inventory);
						checkbox(CXOR("Remove server ads"), &g_cfg.misc.remove_ads);
					}
					end_child;

					begin_child(CXOR("Menu"))
					{
						color_picker(CXOR("Accent color"), g_cfg.misc.ui_color);
						multi_combo(CXOR("Indicators##menu"), g_cfg.misc.menu_indicators, { XOR("Keybind list"), XOR("Bomb"), XOR("Watermark"), XOR("Spectators") });
					}
					end_child;
				}
				ImGui::NextColumn();
				{
					begin_child(CXOR("Changers"))
					{
						multi_combo(CXOR("Breaker animation"), g_cfg.misc.local_breaker, { XOR("static legs air"), XOR("reverse legs"), XOR("allah legs") });
						combo(CXOR("Ragdoll gravity"), &g_cfg.misc.ragdoll_gravity, ragdoll_gravities, IM_ARRAYSIZE(ragdoll_gravities));
						checkbox(CXOR("Clantag changer"), &g_cfg.misc.clantag);
					}
					end_child;

					begin_child(CXOR("Bypass"))
					{
						checkbox(CXOR("Unlock hidden cvars"), &g_cfg.misc.unlock_hidden_cvars);
						checkbox(CXOR("Bypass sv_pure"), &g_cfg.misc.bypass_sv_pure);

//#if ALPHA || _DEBUG
//						checkbox(CXOR("Force crash"), &g_cfg.misc.force_crash);
//#endif
					}
					end_child;
				}
			}
			break;
			}
			ImGui::EndChild(false);

			ImGui::PopClipRect();
			draw_list->PopClipRect();
		}
		break;
		case 5:
		{
			static int selector = 0;
			static int inventory_team = 0;
			const char* inventory_teams[] = { CXOR("CT Loadout"), CXOR("T Loadout") };
			draw_sub_tabs(selector, inventory_tabs);

			static int prev_sub_sel = 0;
			if (selector != prev_sub_sel)
			{
				for (auto& a : item_animations)
					a.second.reset();
				subtab_alpha = 0.f;
				prev_sub_sel = selector;
			}
			create_animation(subtab_alpha, g_cfg.misc.menu && selector == prev_sub_sel, 0.2f, skip_disable | lerp_animation);
			alpha = subtab_alpha * tab_alpha;

			ImGui::SetCursorPosX(0);
			ImGui::BeginChild(CXOR("subtab_inventory"), {}, false);
			switch (selector)
			{
			case 0:
			{
				combo(CXOR("Team##inventory_loadout"), &inventory_team, inventory_teams, IM_ARRAYSIZE(inventory_teams));
				begin_child(CXOR("Current loadout"))
				{
					inventory_preview::draw_agent(inventory_team == 0 ? 3 : 2, ImVec2(120, 140), alpha);
					ImGui::SameLine();
					inventory_preview::draw_weapon(weapon_cfg_knife, ImVec2(150, 140), alpha, false, inventory_team == 0 ? 3 : 2);
					ImGui::SameLine();
					inventory_preview::draw_gloves(ImVec2(150, 140), alpha);
				}
				end_child;

				const std::vector<std::vector<int>> categories = {
					{ weapon_cfg_deagle, weapon_cfg_duals, weapon_cfg_fiveseven, weapon_cfg_glock, weapon_cfg_tec9, weapon_cfg_p2000, weapon_cfg_p250, weapon_cfg_usps, weapon_cfg_cz75, weapon_cfg_revolver },
					{ weapon_cfg_famas, weapon_cfg_galil, weapon_cfg_mac10, weapon_cfg_mp5sd, weapon_cfg_ump45, weapon_cfg_xm1014, weapon_cfg_bizon, weapon_cfg_mag7, weapon_cfg_sawedoff, weapon_cfg_mp7, weapon_cfg_mp9, weapon_cfg_nova },
					{ weapon_cfg_ak47, weapon_cfg_aug, weapon_cfg_awp, weapon_cfg_g3sg1, weapon_cfg_m249, weapon_cfg_m4a1, weapon_cfg_m4a1s, weapon_cfg_p90, weapon_cfg_negev, weapon_cfg_scar20, weapon_cfg_sg556, weapon_cfg_ssg08, weapon_cfg_knife }
				};
				const char* category_names[] = { CXOR("Pistols"), CXOR("Mid-Tier"), CXOR("Rifles & Knife") };
				ImGui::Columns(3, 0, false);
				ImGui::SetColumnOffset(1, 200);
				ImGui::SetColumnOffset(2, 400);
				for (int column = 0; column < 3; ++column)
				{
					if (column > 0)
						ImGui::NextColumn();
					begin_child(category_names[column])
					{
						for (const int index : categories[column])
						{
							if (inventory_preview::weapon_card(index, skin_weapon_configs[index], g_cfg.skins.group_type == index, alpha, inventory_team == 0 ? 3 : 2))
							{
								g_cfg.skins.group_type = index;
								selector = 1;
							}
							ImGui::ItemSize(ImVec2(0, 3));
						}
					}
					end_child;
				}
			}
			break;
			case 1:
			{
				ImGui::Columns(2, 0, false);
				ImGui::SetColumnOffset(1, 300);
				{
					begin_child(CXOR("Weapon"))
					{
						combo(CXOR("Team##inventory_editor"), &inventory_team, inventory_teams, IM_ARRAYSIZE(inventory_teams));
						combo(CXOR("Loadout slot##skins"), &g_cfg.skins.group_type, skin_weapon_configs, IM_ARRAYSIZE(skin_weapon_configs));
						auto& config = skin_changer::get_weapon_config(g_cfg.skins.group_type, inventory_team == 0 ? 3 : 2);
						checkbox(CXOR("Enable skin##inventory"), &config.enable);
						slider_int(CXOR("Wear##inventory"), &config.wear, 0, 100, CXOR("%d%%"));
						slider_int(CXOR("Pattern seed##inventory"), &config.seed, 0, 1000);
						checkbox(CXOR("StatTrak##inventory"), &config.stattrak);
						if (config.stattrak)
							slider_int(CXOR("StatTrak kills##inventory"), &config.stattrak_kills, 0, 1000000);
						if (g_cfg.skins.group_type == weapon_cfg_knife)
							combo(CXOR("Knife model"), &config.knife_model, knife_models, IM_ARRAYSIZE(knife_models));
					}
					end_child;

					begin_child(CXOR("Selected skin preview"))
					{
						inventory_preview::draw_weapon(g_cfg.skins.group_type, ImVec2(260, 125), alpha, true, inventory_team == 0 ? 3 : 2);
					}
					end_child;
				}
				ImGui::NextColumn();
				{
					if (skin_names.size() < skin_changer::paint_kits.size())
						for (auto& skin : skin_changer::paint_kits)
							skin_names.emplace_back(skin.first);

					auto& config = skin_changer::get_weapon_config(g_cfg.skins.group_type, inventory_team == 0 ? 3 : 2);
					auto skin_list = skin_changer::paint_kits.empty() ? empty_list : skin_names;
					const std::string title = XOR("Paint kits | ") + std::to_string(skin_list.size());
					begin_child(title.c_str())
					{
						input_text(CXOR("Search##skins"), config.skins_search, 256);
						listbox(CXOR("skinlist_____"), &config.skin, skin_list, 15, config.skins_search);
					}
					end_child;
				}
			}
			break;
			case 2:
			{
				ImGui::Columns(2, 0, false);
				ImGui::SetColumnOffset(1, 300);
				{
					begin_child(CXOR("Gloves"))
					{
						combo(CXOR("Glove model"), &g_cfg.skins.model_glove, glove_models, IM_ARRAYSIZE(glove_models));
						combo(CXOR("Glove skin"), &g_cfg.skins.glove_skin, glove_skins, IM_ARRAYSIZE(glove_skins));
						inventory_preview::draw_gloves(ImVec2(260, 105), alpha);
					}
					end_child;
				}
				ImGui::NextColumn();
				{
#ifndef LEGACY
					begin_child(CXOR("Agents"))
					{
						combo(CXOR("Mask"), &g_cfg.skins.masks, masks, IM_ARRAYSIZE(masks));
						combo(CXOR("Agent CT"), &g_cfg.skins.model_ct, agents, IM_ARRAYSIZE(agents));
						if (g_cfg.skins.model_ct == 76)
							input_text(CXOR("Model path##agent_ct"), g_cfg.skins.custom_model_ct, 128);
						combo(CXOR("Agent T"), &g_cfg.skins.model_t, agents, IM_ARRAYSIZE(agents));
						if (g_cfg.skins.model_t == 76)
							input_text(CXOR("Model path##agent_t"), g_cfg.skins.custom_model_t, 128);
						inventory_preview::draw_agent(3, ImVec2(120, 150), alpha);
						ImGui::SameLine();
						inventory_preview::draw_agent(2, ImVec2(120, 150), alpha);
					}
					end_child;
#endif
				}
			}
			break;
			}
			ImGui::EndChild(false);
			ImGui::PopClipRect();
			draw_list->PopClipRect();
		}
		break;
		case 7:
		{
			static int config_select = -1;
			static char config_name[256]{};

			bool wrong_config = config_select == -1 || config_list.size() <= 0 || config_select > config_list.size() - 1;

			int cfg_flags = 0;
			if (wrong_config)
				cfg_flags |= button_wait;

			ImGui::Columns(2, 0, false);
			ImGui::SetColumnOffset(1, 300);
			{
				begin_child(CXOR("General   "))
				{
					input_text(CXOR("Config name"), config_name, 256);

					int len = std::strlen(config_name);

					button(
						CXOR("Create"),
						[]()
						{
							config::create(config_name);
							refresh_configs();
						},
						len <= 0 ? button_wait : 0);

					button(CXOR("Refresh"), []() { refresh_configs(); });

					button(CXOR("Open configs folder"),
						[]()
						{
							std::string folder = CXOR("airflow\\");
							ShellExecuteA(NULL, NULL, folder.c_str(), NULL, NULL, SW_SHOWNORMAL);
						});
				}
				end_child;

				std::string config_child_name = XOR("Config: ") + (wrong_config ? XOR("None") : config_list[config_select]);

				begin_child(config_child_name.c_str())
				{
					button(
						CXOR("Load"),
						[]()
						{
							config::load(config_list[config_select]);
							if (!HACKS->loading_config)
								HACKS->loading_config = true;
						},
						cfg_flags);

					button(
						CXOR("Save"), []() { config::save(config_list[config_select]); }, cfg_flags);

					button(
						CXOR("Reset"),
						[]()
						{
							g_cfg = configs_t();
							config::save(config_list[config_select]);
						},
						cfg_flags);

					int delete_flags = button_danger;
					if (wrong_config)
						delete_flags |= button_wait;

					button(
						CXOR("Delete"),
						[]()
						{
							config::erase(config_list[config_select]);
							refresh_configs();
						},
						delete_flags);
				}
				end_child;
			}
			ImGui::NextColumn();
			{
				static char config_search[256];

				auto list = config_list.size() <= 0 ? empty_list : config_list;
				std::string config_list_child_name = XOR("Configs | ") + std::to_string(list.size()) + XOR(" total");

				begin_child(config_list_child_name.c_str())
				{
					input_text(CXOR("Config search"), config_search, 256);
					listbox(CXOR("configlist___"), &config_select, list, 19, config_search);
				}
				end_child;
			}
		}
		break;
		}
	}
	ImGui::EndChild(false);

	alpha = old_alpha;
	ImGui::PopClipRect();
	ImGui::SetCursorPos(prev_pos);
}