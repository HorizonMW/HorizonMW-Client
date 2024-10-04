#pragma once

#include "game/game.hpp"
#include <utils/info_string.hpp>

enum sort_types
{
	hostname,
	map,
	mode,
	players,
	ping,
	verified
};

namespace server_list
{
	struct server_info
	{
		// gotta add more to this
		int clients;
		int max_clients;
		int bots;
		int ping;
		std::string host_name;
		std::string map_name;
		std::string game_type;
		std::string mod_name;
		game::CodPlayMode play_mode;
		char in_game;
		game::netadr_s address;
		bool is_private;
		std::string connect_address;
		std::string game_version;
		bool outdated;
		bool is_official;
	};

	int get_player_count();
	int get_server_count();
	void add_favourite(int server_index);
	void delete_favourite(int server_index);

	namespace tcp {
		// TCP functions
		void populate_server_list();

		void populate_server_list_threaded();

		void add_server_to_list(const std::string &infoJson, const std::string &connect_address, int server_index);

		void parse_favourites_tcp();

		void parse_favourites_tcp_threaded();

		int get_server_limit_per_page();

		int get_current_page();

		int get_total_pages();

		int get_page_number(int server_index);

		void load_page(int page_number, bool add_servers = true);

		void next_page();

		void previous_page();

		void add_server_to_page(int page_index, server_info &server_info);

		std::string get_notification_message();

		std::string get_error_message();

		std::string get_error_header();

		void display_error(std::string header, std::string message);

		bool check_can_join(const char* connect_address);

		void sort_current_page(int sort_type, bool bypassListCheck = false);

		// Functions to pass these to lua
		bool is_getting_server_list();
		bool is_getting_favourites();
		bool is_loading_a_page();

		void fetch_game_server_info(const std::string& connect_address, std::shared_ptr<std::atomic<int>> server_index);

		void set_sort_type(int type);

		void join_server_new(int index);

	}
}
