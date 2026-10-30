#pragma once
#include <map>
#include <unordered_map>
#include <string>
#include <vector>

namespace skin_changer
{
	inline std::vector< std::pair< std::string, int > > paint_kits{};
	inline std::unordered_map<int, std::string> paint_kit_names_by_id{};

	extern void init_parser();
	extern std::string get_model_name(int team);
	extern int glove_skins_id();
	extern int correct_sequence(const short& index, const int seq);
	extern void on_postdataupdate_start(int stage);
	extern void on_game_events(c_game_event* event);
	extern void on_frame_render_end(int stage);
}