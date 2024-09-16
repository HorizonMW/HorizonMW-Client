#pragma once
#include "game/game.hpp"

namespace party
{
	struct connection_state
	{
		game::netadr_s host;
		std::string challenge;
		bool hostDefined;
		std::string motd;
		int max_clients;
		std::string base_url;
	};

	struct discord_information
	{
		std::string image;
		std::string image_text;
	};

	void user_download_response(bool response);

	void menu_error(const std::string& error);

	void reset_server_connection_state();
	game::netadr_s& get_target();

	void connect(const game::netadr_s& target);
	void start_map(const std::string& mapname, bool dev = false);

	void clear_sv_motd();
	connection_state get_server_connection_state();
	std::optional<discord_information> get_server_discord_info();

	int get_client_num_by_name(const std::string& name);

	int get_client_count();
	int get_bot_count();

	// Aphrodite's stuff

	namespace {
		struct usermap_file
		{
			std::string extension;
			std::string name;
			bool optional;
		};

		// snake case these names before release
		std::vector<usermap_file> usermap_files =
		{
			{".ff", "usermap_hash", false},
			{"_load.ff", "usermap_load_hash", true},
			{".arena", "usermap_arena_hash", true},
			{".pak", "usermap_pak_hash", true},
		};

		std::vector<usermap_file> mod_files =
		{
			{".ff", "mod_hash", false},
			{"_pre_gfx.ff", "mod_pre_gfx_hash", true},
			{".pak", "mod_pak_hash", true},
		};

		bool needs_vid_restart = false;
		bool connecting_to_server = false;
	}

	void connect_tcp(const game::netadr_s& target);

	int getinfo_callback(size_t total, size_t progress);

	std::string get_file_hash(const std::string& file);

	std::string get_usermap_file_path(const std::string& mapname, const std::string& extension);

	std::string get_dvar_string(const std::string& dvar);

	int get_dvar_int(const std::string& dvar);

	bool get_dvar_bool(const std::string& dvar);

}
