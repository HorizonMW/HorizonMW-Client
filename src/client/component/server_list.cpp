#include <std_include.hpp>
#include "loader/component_loader.hpp"

#include "command.hpp"
#include "console.hpp"
#include "fastfiles.hpp"
#include "localized_strings.hpp"
#include "network.hpp"
#include "party.hpp"
#include "scheduler.hpp"
#include "server_list.hpp"

#include "game/game.hpp"
#include "game/dvars.hpp"
#include "game/ui_scripting/execution.hpp"

#include <utils/cryptography.hpp>
#include <utils/string.hpp>
#include <utils/hook.hpp>
#include <utils/io.hpp>
#include <utils/toast.hpp>
#include <tcp/hmw_tcp_utils.hpp>

#include <thread>
#include <limits.h>
#include <future>

namespace server_list
{
	template<typename T>
	inline T ceil(T x, T y)
	{
		return x / y + (x % y != 0);
	}

	namespace tcp 
	{
		// Paging
		const int server_limit_per_page = 100;
		int current_page = 0;
		bool is_loading_page = false;

		// Threading
		std::mutex server_list_mutex;

		// Used for when we're refreshing the server / favourites list
		bool getting_server_list = false;
		bool getting_favourites = false;

		// Used for stopping the threads abruptly
		std::mutex interrupt_mutex;
		bool interrupt_server_list = false;
		bool interrupt_favourites = false;

		struct PageData {
			std::vector<server_info> listed_servers;
			int page_index = -1;

			void add_server(server_info& info) 
			{
				if (listed_servers.size() >= server_limit_per_page) 
				{
					return;
				}

				listed_servers.emplace_back(info);
			}

			void clear_servers() 
			{
				listed_servers.clear();
			}

			int get_server_count() 
			{
				return static_cast<int>(listed_servers.size());
			}

			int get_player_count() {
				int count = 0;
				for (server_info& info : listed_servers) {
					count += info.clients - info.bots;
				}
				return count;
			}
		};

		std::vector<PageData> pages;
		
		std::string notification_message = "";

		bool error_is_displayed = false;
		std::string error_header = "Error!";
		std::string error_message = "";
		std::chrono::seconds error_display_length = 3s;

		std::string failed_to_join_header = "Failed to join server!";
		std::string failed_to_join_reason = "";
	}

	namespace
	{
		enum sort_types : uint32_t {
			sort_type_unknown = 0, // Patoke @todo: reverse, Captain Barbossa: It will never trigger because it is not used in the server_list.lua
			sort_type_hostname = 1,
			sort_type_map = 2,
			sort_type_mode = 3,
			sort_type_players = 4,
			sort_type_ping = 5,
			sort_type_outdated = 6
		};

		struct
		{
			game::netadr_s address{};
			volatile bool requesting = false;
		} master_state;

		std::mutex mutex;
		std::vector<server_info> servers;

		size_t server_list_page = 0;
		int list_sort_type = sort_type_players;
		std::chrono::high_resolution_clock::time_point last_scroll{};

		bool get_favourites_file(nlohmann::json& out)
		{
			std::string data = utils::io::read_file("players2/favourites.json");

			nlohmann::json obj;
			try
			{
				obj = nlohmann::json::parse(data.data());
			}
			catch (const nlohmann::json::parse_error& ex)
			{
				(void)ex;
				return false;
			}

			if (!obj.is_array())
			{
				console::error("Favourites storage file is invalid!");
				return false;
			}

			out = obj;

			return true;
		}

		void refresh_server_list()
		{
			if (tcp::getting_server_list || tcp::getting_favourites || tcp::is_loading_page) {
				return;
			}

			{
				std::lock_guard<std::mutex> _(mutex);
				servers.clear();

				tcp::pages.clear();
				tcp::current_page = 0;

				server_list_page = 0;

				tcp::interrupt_favourites = false;
				tcp::interrupt_server_list = false;
			}

			ui_scripting::notify("updateGameList", {});
			ui_scripting::notify("updatePageCounter", {});

			auto* sort_type = game::Dvar_FindVar("ui_netSource");
			// Internet
			if (sort_type && sort_type->current.integer == 1)
			{
				server_list::tcp::populate_server_list_threaded();
			}
			// Favourites
			else if (sort_type && sort_type->current.integer == 2)
			{
				server_list::tcp::parse_favourites_tcp_threaded();
			}
		}

		void join_server(int, int, const int index)
		{
			tcp::join_server_new(index);
			return; // Do nothing now

			/*
			std::lock_guard<std::mutex> _(mutex);

			const auto i = static_cast<size_t>(index);
			if (i < servers.size())
			{
				static auto last_index = ~0ull;
				if (last_index != i)
				{
					last_index = i;
				}
				else
				{
					console::info("Connecting to (%d - %zu): %s\n", index, i, servers[i].host_name.data());
					//servers[i].address.port = htons(servers[i].address.port);
					
					bool canJoin = server_list::tcp::check_can_join(servers[i].connect_address);
					if (canJoin) {
						//command::execute("connect " + servers[i].connect_address);
						tcp::interrupt_favourites = true;
						tcp::interrupt_server_list = true;
						party::connect(servers[i].address);
					}
					else {
						server_list::tcp::display_error(server_list::tcp::failed_to_join_header, server_list::tcp::failed_to_join_reason);
					}
				}
			}*/
		}

		int ui_feeder_count()
		{
			std::lock_guard<std::mutex> _(mutex);
			return static_cast<int>(servers.size());
		}

		const char* ui_feeder_item_text(const int index, const int column)
		{
			std::lock_guard<std::mutex> _(mutex);

			const auto i = static_cast<size_t>(index);

			if (i >= servers.size())
			{
				return "";
			}

			switch (column)
			{
			case 0:
			{
				if (servers[i].host_name.empty()) {
					return "";
				}

				auto name = servers[i].host_name.data();
				return name;
			}
			case 1:
			{
				const auto& map_name = servers[i].map_name;
				if (map_name.empty())
				{
					return "Unknown";
				}

				auto map_display_name = game::UI_GetMapDisplayName(map_name.data());
				if (!fastfiles::usermap_exists(map_name) && !fastfiles::exists(map_name, false))
				{
					map_display_name = utils::string::va("^1%s", map_display_name);
				}
				return map_display_name;
			}
			case 2:
			{
				const auto client_count = servers[i].clients - servers[i].bots;
				return utils::string::va("%d/%d [%d]", client_count, servers[i].max_clients,
					servers[i].clients);
			}
			case 3:
				return servers[i].game_type.empty() ? "" : servers[i].game_type.data();
			case 4:
			{
				const auto ping = servers[i].ping ? servers[i].ping : 999;
				if (ping < 75)
				{
					return utils::string::va("^2%d", ping);
				}
				else if (ping < 150)
				{
					return utils::string::va("^3%d", ping);
				}
				return utils::string::va("^1%d", ping);
			}
			case 5:
				return servers[i].is_private ? "1" : "0";
			case 6:
				return servers[i].mod_name.empty() ? "" : servers[i].mod_name.data();
			case 8:
			{
				auto version = servers[i].game_version.data();

				std::string versionStr(version);

				if (versionStr.starts_with('v')) {
					versionStr.erase(0, 1);
				}

				return servers[i].outdated ? utils::string::va("^1%s", versionStr.data()) : utils::string::va("%s", versionStr.data());
			}
			default:
				return "";
			}
		}

		void insert_server(server_info&& server)
		{
			// Do not exceed the server limit
			if (servers.size() >= tcp::server_limit_per_page) {
				return;
			}

			std::lock_guard<std::mutex> _(mutex);

			// Duplicate handling
			auto it = std::find_if(servers.begin(), servers.end(),
				[&server](const server_info& existing_server) {
					return existing_server.connect_address == server.connect_address;
				});

			if (it == servers.end()) {
				servers.emplace_back(std::move(server));
			}
		}

		bool is_server_list_open()
		{
			return game::Menu_IsMenuOpenAndVisible(0, "menu_systemlink_join");
		}

		utils::hook::detour lui_open_menu_hook;

		void lui_open_menu_stub(int controllerIndex, const char* menuName, int isPopup, int isModal, unsigned int isExclusive)
		{
#ifdef DEBUG
			console::info("[LUI] %s\n", menuName);
#endif
			if (!strcmp(menuName, "menu_systemlink_join"))
			{
				refresh_server_list();
			}

			lui_open_menu_hook.invoke<void>(controllerIndex, menuName, isPopup, isModal, isExclusive);
		}
	}

	void tcp::sort_current_page(int sort_type, bool bypassListCheck) {
		// bypassListCheck is used for auto sorting on refresh / parsing favourites
		if (!bypassListCheck) {
			if (getting_server_list || getting_favourites || is_loading_page) {
				return;
			}
		}

		auto servers_cache = servers;

		{
			std::lock_guard<std::mutex> _(mutex);
			servers.clear();
		}
		ui_scripting::notify("updateGameList", {});

		std::stable_sort(servers_cache.begin(), servers_cache.end(), [sort_type](const server_info& a, const server_info& b)
		{
			switch (sort_type)
			{
				case sort_type_unknown:
					// Patoke @todo: what is this doing and why does it exist?
					break;
				case sort_type_hostname:
					return a.host_name.compare(b.host_name) < 0;
				case sort_type_map:
					return a.map_name.compare(b.map_name) < 0;
				case sort_type_mode:
					return a.game_type.compare(b.game_type) < 0;
				case sort_type_players: // sort by most players
					return (a.clients - a.bots) > (b.clients - b.bots);
				case sort_type_ping: // sort by smallest ping
					return a.ping < b.ping;
				case sort_type_outdated:
					// Sort by outdated status, with outdated servers coming last
					return a.outdated == b.outdated ? false : a.outdated > b.outdated;
			}
				return true;
			});

		scheduler::once([=]()
		{
			for (server_info server : servers_cache)
			{
				insert_server(std::move(server));
			}
			ui_scripting::notify("updateGameList", {});
		}, scheduler::pipeline::main, 125ms);
	}

	bool tcp::is_getting_server_list()
	{
		return getting_server_list;
	}

	bool tcp::is_getting_favourites()
	{
		return getting_favourites;
	}

	bool tcp::is_loading_a_page()
	{
		return is_loading_page;
	}

	void tcp::fetch_game_server_info(const std::string& connect_address, std::shared_ptr<std::atomic<int>> server_index) {
		{
			std::lock_guard<std::mutex> lock(interrupt_mutex);
			if (interrupt_server_list || interrupt_favourites) {
				return;
			}
		}

		// @CB: This try catches isn't really needed. But since this is multithreaded, it's better to be safe then sorry

		try {
			std::string game_server_info = connect_address + "/getInfo";
			std::string game_server_response = hmw_tcp_utils::GET_url(game_server_info.c_str(), {}, true, 1500L, true, 3);

			if (!game_server_response.empty()) {
				{
					std::lock_guard<std::mutex> lock(server_list_mutex);
					tcp::add_server_to_list(game_server_response, connect_address, server_index->fetch_add(1));
					ui_scripting::notify("updateGameList", {});
				}
			}
		}
		catch (const std::exception& e) {
			console::error("Failed to fetch server info: %s", std::string(e.what()).data());
		}
	}

	void tcp::set_sort_type(int type)
	{
		list_sort_type = type;
	}

	void tcp::join_server_new(int index)
	{
		scheduler::once([=]()
		{
			console::info("Joining server: %d", index);
			std::lock_guard<std::mutex> _(mutex);

			if (index < servers.size())
			{
				std::string server_address = "";
				console::info("Connecting to server:[%d] {%s} %s\n", index, servers[index].connect_address.data(), servers[index].host_name.data());
				//tcp::interrupt_favourites = true;
				//tcp::interrupt_server_list = true;
				//party::connect(servers[index].address);

				//@CB TODO. Fix this with new system. because its now called with lui, connect_address gets corruipted
				bool canJoin = server_list::tcp::check_can_join(servers[index].connect_address.data());
				if (canJoin) {
					//command::execute("connect " + servers[i].connect_address);
					tcp::interrupt_favourites = true;
					tcp::interrupt_server_list = true;
					party::connect(servers[index].address);
				}
				else {
					server_list::tcp::display_error(server_list::tcp::failed_to_join_header, server_list::tcp::failed_to_join_reason);
				}
			}
		}, scheduler::pipeline::main);
	}

	int get_player_count()
	{
		std::lock_guard<std::mutex> _(mutex);
		int count = 0;
		for (server_list::tcp::PageData page : server_list::tcp::pages) 
		{
			count += page.get_player_count();
		}
		return count;
	}

	int get_server_count()
	{
		std::lock_guard<std::mutex> _(mutex);
		int count = 0;
		for (server_list::tcp::PageData page : server_list::tcp::pages) 
		{
			count += page.get_server_count();
		}
		return count;
	}

	void add_favourite(int index)
	{
		// Read existing favorites from the file
		nlohmann::json obj;
		std::ifstream favourites_file("players2/favourites.json");

		if (favourites_file.is_open())
		{
			favourites_file >> obj;
			favourites_file.close();
		}

		if (tcp::current_page < 0 || tcp::current_page >= tcp::pages.size())
		{
			return;
		}

		tcp::PageData& page = tcp::pages[tcp::current_page];

		if (index < 0 || index >= page.listed_servers.size())
		{
			return;
		}

		server_info& info = page.listed_servers[index];

		// Check if the server is already in favorites
		if (obj.find(info.connect_address) != obj.end())
		{
			utils::toast::show("Error", "Server already marked as favourite.");
			return;
		}

		// Add the new favorite server
		obj.push_back(info.connect_address);

		// Write updated favorites to the file
		utils::io::write_file("players2/favourites.json", obj.dump());
		utils::toast::show("Success", "Server added to favourites.");

		console::debug("added %s to favourites", info.connect_address);
	}

	void delete_favourite(int index)
	{
		nlohmann::json obj;
		if (!get_favourites_file(obj))
		{
			return;
		}

		if (tcp::current_page < 0 || tcp::current_page >= tcp::pages.size()) 
		{
			return;
		}

		tcp::PageData& page = tcp::pages[tcp::current_page];

		if (index < 0 || index >= page.listed_servers.size())
		{
			return;
		}

		server_info& info = page.listed_servers[index];

		for (auto it = obj.begin(); it != obj.end(); ++it)
		{
			if (!it->is_string()) 
			{
				continue;
			}

			if (it->get<std::string>() == info.connect_address) // Check if the element matches the connect address
			{
				console::debug("removed %s from favourites", info.connect_address);
				obj.erase(it);
				break;
			}
		}

		if (!utils::io::write_file("players2/favourites.json", obj.dump())) 
		{
			return;
		}

		page.listed_servers.erase(
			std::remove_if(
				page.listed_servers.begin(),
				page.listed_servers.end(),
				[&info](const server_info& s) {
					return s.connect_address == info.connect_address;
				}
			),
			page.listed_servers.end()
		);

		refresh_server_list();
	}

	void tcp::populate_server_list() {
		// @CB: These try catches aren't really needed. But since this is multithreaded, it's better to be safe then sorry

		notification_message = "Refreshing server list...";
		ui_scripting::notify("showRefreshingNotification", {});

		std::string master_server_list = hmw_tcp_utils::GET_url(hmw_tcp_utils::MasterServer::get_master_server(), {}, false, 10000L, true, 3);

		// Clear error message if any
		if (error_is_displayed) {
			ui_scripting::notify("hideErrorMessage", {});
			error_is_displayed = false;
		}

		auto server_index = std::make_shared<std::atomic<int>>(0);

		console::info("Checking if localhost server is running on default port (27017)");
		std::string port = "27017";
		bool localhost = hmw_tcp_utils::GameServer::is_localhost(port);

		if (localhost) {
			std::string local_res = hmw_tcp_utils::GET_url("localhost:27017/getInfo", {}, true, 1500L, false, 1);
			if (!local_res.empty()) {
				add_server_to_list(local_res, "localhost:27017", server_index->fetch_add(1));
				ui_scripting::notify("updateGameList", {});
			}
		}

		if (master_server_list.empty()) {
			console::info("Failed to get response from master server!");
			getting_server_list = false;
			ui_scripting::notify("updateGameList", {});
			ui_scripting::notify("hideRefreshingNotification", {});
			ui_scripting::notify("updateRefreshTimer", {});
			display_error("MASTER SERVER ERROR!", "No response!");
			return;
		}

		std::vector<std::thread> threads;

		try {
			nlohmann::json master_server_response_json = nlohmann::json::parse(master_server_list);

			for (const auto& element : master_server_response_json) {
				std::string connect_address = element.get<std::string>();

				{
					std::lock_guard<std::mutex> lock(interrupt_mutex);
					if (interrupt_server_list) {
						break; // If interrupted, break out of the loop
					}
				}

				// Launch threads for fetching server info
				threads.emplace_back([connect_address, server_index]() {
					try {
						fetch_game_server_info(connect_address, server_index);
					}
					catch (std::exception e) {
						console::error("Error fetching server info: %s", std::string(e.what()).data());
					}
				});
			}
		}

		catch (const std::exception& e) {
			getting_server_list = false;
			ui_scripting::notify("updateGameList", {});
			ui_scripting::notify("hideRefreshingNotification", {});
			ui_scripting::notify("updateRefreshTimer", {});

			console::error("Error parsing master server JSON response: %s", std::string(e.what()));
			display_error("MASTER SERVER ERROR!", "Failed to parse response!");
			return;
		}

		// Join all the threads to ensure they complete
		for (auto& t : threads) {
			if (t.joinable()) {
				t.join();
			}
		}

		load_page(0, false);

		{
			std::lock_guard<std::mutex> interrupt_lock(interrupt_mutex);
			interrupt_server_list = false;
			getting_server_list = false;
		}

		ui_scripting::notify("updateGameList", {});
		ui_scripting::notify("hideRefreshingNotification", {});
		ui_scripting::notify("updateRefreshTimer", {});

		// Auto sort after server populate
		scheduler::once([=]()
		{
			sort_current_page(list_sort_type, true); // Sort after populating
		}, scheduler::pipeline::main);
	}

	void tcp::populate_server_list_threaded()
	{
		// if we were updating the favourites list, interrupt it
		server_list::tcp::interrupt_favourites = server_list::tcp::getting_favourites;
		
		if (getting_server_list) 
		{
			return;
		}

		scheduler::once([=]()
		{
			getting_server_list = true;
			populate_server_list();
		}, scheduler::pipeline::network);
	}

	void tcp::parse_favourites_tcp_threaded()
	{
		// if we were updating the server list, interrupt it
		server_list::tcp::interrupt_server_list = server_list::tcp::getting_server_list;

		if (getting_favourites)
		{
			return;
		}

		scheduler::once([=]()
		{
			getting_favourites = true;
			parse_favourites_tcp();
		}, scheduler::pipeline::network);
	}

	int tcp::get_server_limit_per_page()
	{
		return server_limit_per_page;
	}

	int tcp::get_current_page()
	{
		return current_page;
	}

	int tcp::get_total_pages()
	{
		int total_pages = ceil(get_server_count(), server_limit_per_page);
		return total_pages;
	}

	int tcp::get_page_number(int server_index)
	{
		// add one so we start from 1 rather than 0
		int page_number = server_index / server_limit_per_page + 1;
		return page_number;
	}

	void tcp::load_page(int page_number, bool add_servers)
	{
		if (page_number < 0 || page_number >= pages.size()) {
			return;
		}

		if (add_servers)
		{
			is_loading_page = true;
			notification_message = "Loading page " + (page_number + 1);
			ui_scripting::notify("showRefreshingNotification", {});
			{
				std::lock_guard<std::mutex> _(mutex);
				servers.clear();
			}
			ui_scripting::notify("updateGameList", {});

			scheduler::once([=]()
			{
				PageData page = pages[page_number];
				for (server_info server : page.listed_servers)
				{
					insert_server(std::move(server));
				}

				ui_scripting::notify("updateGameList", {});
				ui_scripting::notify("hideRefreshingNotification", {});
				scheduler::once([=]()
				{
					is_loading_page = false;
					sort_current_page(list_sort_type); // Sort after populating
				}, scheduler::pipeline::main);
			}, scheduler::pipeline::main, 125ms);
		}

		//sort_serverlist(list_sort_type);

		current_page = page_number;

		ui_scripting::notify("updateGameList", {});
		ui_scripting::notify("updatePageCounter", {});
	}

	void tcp::next_page()
	{
		if (getting_server_list || getting_favourites || is_loading_page) {
			return;
		}

		current_page++;
		if (current_page >= get_total_pages()) 
		{
			load_page(0); // Load first page
			return;
		}
		load_page(current_page);
	}

	void tcp::previous_page()
	{
		if (getting_server_list || getting_favourites || is_loading_page) {
			return;
		}

		current_page--;
		if (current_page < 0) 
		{
			load_page(get_total_pages() - 1); // Load to last page
			return;
		}
		load_page(current_page);
	}

	void tcp::add_server_to_page(int page_index, server_info &server_info)
	{
		// Page index can't be negative
		if (page_index < 0) 
		{
			return;
		}

		if (page_index >= pages.size()) 
		{
			pages.resize(page_index + 1);
			pages[page_index].page_index = page_index;
		}

		pages[page_index].add_server(server_info);
	}

	std::string tcp::get_notification_message()
	{
		return notification_message;
	}

	std::string tcp::get_error_message()
	{
		return error_message;
	}

	std::string tcp::get_error_header()
	{
		return error_header;
	}

	void tcp::display_error(std::string header, std::string message)
	{
		if (error_is_displayed)
		{
			return;
		}

		error_header = header;
		error_message = message;
		error_is_displayed = true;
		ui_scripting::notify("showErrorMessage", {});
		scheduler::once([=]()
		{
			error_is_displayed = false;
			ui_scripting::notify("hideErrorMessage", {});
		}, scheduler::pipeline::main, error_display_length);
	}

	bool tcp::check_can_join(const char* connect_address)
	{
		// Ensure connect_address is valid before using it
		if (connect_address == nullptr) {
			console::info("Failed to join server. Invalid connect address.");
			error_header = "Failed to join server!";
			error_message = "Invalid connect address.";
			display_error(error_header, error_message);
			return false;
		}

		std::string game_server_info = std::string(connect_address) + "/getInfo";

		const char* server_address = game_server_info.data();

		std::string game_server_response = hmw_tcp_utils::GET_url(server_address, {}, true, 1500L, true, 3);

		if (game_server_response.empty())
		{
			failed_to_join_reason = "Server did not respond.";
			return false;
		}

		nlohmann::json game_server_response_json = nlohmann::json::parse(game_server_response);
		std::string ping = game_server_response_json["ping"];

		// Don't show servers that aren't using the same protocol!
		std::string server_protocol = game_server_response_json["protocol"];
		const auto protocol = std::atoi(server_protocol.c_str());
		if (protocol != PROTOCOL)
		{
			failed_to_join_reason = "Invalid protocol";
			return false;
		}

		// Don't show servers that aren't running!
		std::string server_running = game_server_response_json["sv_running"];
		const auto sv_running = std::atoi(server_running.c_str());
		if (!sv_running)
		{
			failed_to_join_reason = "Server not running.";
			return false;
		}

		// Only handle servers of the same playmode!
		std::string response_playmode = game_server_response_json["playmode"];
		const auto playmode = game::CodPlayMode(std::atoi(response_playmode.c_str()));
		if (game::Com_GetCurrentCoDPlayMode() != playmode)
		{
			failed_to_join_reason = "Invalid playmode.";
			return false;
		}

		// Only show H2M games
		std::string server_gamename = game_server_response_json["gamename"];
		if (server_gamename != "H2M")
		{
			failed_to_join_reason = "Invalid gamename.";
			return false;
		}

		std::string mapname = game_server_response_json["mapname"];
		if (mapname.empty()) {
			failed_to_join_reason = "Invalid map.";
			return false;
		}

		std::string gametype = game_server_response_json["gametype"];
		if (gametype.empty()) {
			failed_to_join_reason = "Invalid gametype.";
			return false;
		}

		std::string maxclients = game_server_response_json["sv_maxclients"];
		std::string clients = game_server_response_json["clients"];
		std::string bots = game_server_response_json["bots"];
		std::string privateclients = game_server_response_json.value("sv_privateClients", "0");

		int max_clients = std::stoi(maxclients);
		int current_clients = std::stoi(clients);
		int private_clients = std::stoi(privateclients);
		int current_bots = std::stoi(bots);
		int player_count = current_clients - current_bots;

		int actual_max_clients = max_clients - private_clients;

		if (player_count == max_clients) {
			error_display_length = 3s;
			failed_to_join_header = "Failed to join server!";
			failed_to_join_reason = "Game is full.";
			return false;
		}

		if (player_count == actual_max_clients) {
			error_display_length = 10s;
			failed_to_join_header = "Reserved Game Full!";
			failed_to_join_reason = "Reserved? Use commandline\n/connect " + std::string(connect_address);
			return false;
		}

		failed_to_join_reason = "";
		return true;
	}

	void tcp::add_server_to_list(const std::string& infoJson, const std::string& connect_address, int server_index)
	{
		nlohmann::json game_server_response_json = nlohmann::json::parse(infoJson);
		std::string ping = game_server_response_json["ping"];

		// Don't show servers that aren't using the same protocol!
		std::string server_protocol = game_server_response_json["protocol"];
		const auto protocol = std::atoi(server_protocol.c_str());
		if (protocol != PROTOCOL)
		{
			return;
		}

		// Don't show servers that aren't dedicated!
		std::string server_dedicated = game_server_response_json["dedicated"];
		const auto dedicated = std::atoi(server_dedicated.c_str());
		if (!dedicated)
		{
			return;
		}

		// Don't show servers that aren't running!
		std::string server_running = game_server_response_json["sv_running"];
		const auto sv_running = std::atoi(server_running.c_str());
		if (!sv_running)
		{
			return;
		}

		// Only handle servers of the same playmode!
		std::string response_playmode = game_server_response_json["playmode"];
		const auto playmode = game::CodPlayMode(std::atoi(response_playmode.c_str()));
		if (game::Com_GetCurrentCoDPlayMode() != playmode)
		{
			return;
		}

		// Only show H2M games
		std::string server_gamename = game_server_response_json["gamename"];
		if (server_gamename != "H2M")
		{
			return;
		}

		std::string gameversion = game_server_response_json.value("gameversion", "Unknown");
		bool outdated = gameversion != hmw_tcp_utils::get_version();

		game::netadr_s address{};
		if (game::NET_StringToAdr(connect_address.c_str(), &address))
		{
			server_info server{};
			server.address = address;
			server.host_name = game_server_response_json["hostname"];
			server.map_name = game_server_response_json["mapname"];
			server.game_version = gameversion;
			server.outdated = outdated;

			std::string game_type = game_server_response_json["gametype"];
			server.game_type = game::UI_GetGameTypeDisplayName(game_type.c_str());
			server.mod_name = game_server_response_json["fs_game"];

			server.play_mode = playmode;

			std::string clients = game_server_response_json["clients"];
			server.clients = atoi(clients.c_str());

			std::string max_clients = game_server_response_json["sv_maxclients"];
			server.max_clients = atoi(max_clients.c_str());

			std::string bots = game_server_response_json["bots"];
			server.bots = atoi(bots.c_str());

			int latency = std::atoi(ping.c_str());
			server.ping = latency >= 999 ? 999 : latency; // Cap latency display to 999

			std::string isPrivate = game_server_response_json["isPrivate"];
			server.is_private = atoi(isPrivate.c_str()) == 1;

			server.connect_address = connect_address;

			int page_number = get_page_number(server_index) - 1;
			add_server_to_page(page_number, server);

			if (page_number == 0)
			{
				insert_server(std::move(server)); // Populate the first page in real time
			}
		}
	}

	void tcp::parse_favourites_tcp() {
		notification_message = "Loading favourites...";
		ui_scripting::notify("showRefreshingNotification", {});

		nlohmann::json obj;
		if (!get_favourites_file(obj)) {
			{
				std::lock_guard<std::mutex> lock(interrupt_mutex);
				interrupt_favourites = false;
				getting_favourites = false;
			}

			console::info("Finished getting favourites!");

			ui_scripting::notify("updateGameList", {});
			ui_scripting::notify("hideRefreshingNotification", {});
			ui_scripting::notify("updateRefreshTimer", {});
			return;
		}

		// @CB: These try catches aren't really needed. But since this is multithreaded, it's better to be safe then sorry

		auto server_index = std::make_shared<std::atomic<int>>(0);  // Use shared_ptr for thread-safe atomic
		std::vector<std::thread> threads;

		for (auto& element : obj) {
			if (!element.is_string()) {
				continue;
			}

			std::string connect_address = element.get<std::string>();

			{
				std::lock_guard<std::mutex> lock(interrupt_mutex);
				if (interrupt_favourites) {
					break;
				}
			}

			// Launch threads for fetching server info
			threads.emplace_back([connect_address, server_index]() {
				try {
					fetch_game_server_info(connect_address, server_index);
				}
				catch (const std::exception& e) {
					console::error("Error fetching favourite server info: %s", std::string(e.what()));
				}
				});
		}

		// Join all threads to ensure they complete
		for (auto& t : threads) {
			if (t.joinable()) {
				t.join();
			}
		}
		
		load_page(0, false);

		{
			std::lock_guard<std::mutex> interrupt_lock(interrupt_mutex);
			interrupt_favourites = false;
			getting_favourites = false;
		}

		console::info("Finished getting favourites!");

		ui_scripting::notify("updateGameList", {});
		ui_scripting::notify("hideRefreshingNotification", {});
		ui_scripting::notify("updateRefreshTimer", {});

		// Auto sort after parsing favourites
		scheduler::once([=]()
		{
			sort_current_page(list_sort_type, true); // Sort after populating
		}, scheduler::pipeline::main);
	}

	class component final : public component_interface
	{
	public:
		void post_unpack() override
		{
			// hook LUI_OpenMenu to refresh server list for system link menu
			lui_open_menu_hook.create(game::LUI_OpenMenu, lui_open_menu_stub);

			// replace UI_RunMenuScript call in LUI_CoD_LuaCall_RefreshServerList to our refresh_servers
			utils::hook::jump(0x28E049_b, utils::hook::assemble([](utils::hook::assembler& a)
			{
				a.pushad64();
				a.call_aligned(refresh_server_list);
				a.popad64();

				a.xor_(eax, eax);
				a.mov(rbx, qword_ptr(rsp, 0x38));
				a.add(rsp, 0x20);
				a.pop(rdi);
				a.ret();
			}), true);

			utils::hook::jump(0x28E557_b, utils::hook::assemble([](utils::hook::assembler& a)
			{
				a.mov(r8d, edi);
				a.mov(ecx, eax);
				a.mov(ebx, eax);

				a.pushad64();
				a.call_aligned(join_server);
				a.popad64();

				a.jmp(0x28E563_b);
			}), true);

			utils::hook::nop(0x28E57D_b, 5);

			// do feeder stuff
			utils::hook::jump(0x28E117_b, utils::hook::assemble([](utils::hook::assembler& a)
			{
				a.mov(ecx, eax);

				a.pushad64();
				a.call_aligned(ui_feeder_count);
				a.movd(xmm0, eax);
				a.popad64();

				a.mov(rax, qword_ptr(rbx, 0x48));
				a.cvtdq2ps(xmm0, xmm0);
				a.jmp(0x28E12B_b);
			}), true);

			utils::hook::jump(0x28E331_b, utils::hook::assemble([](utils::hook::assembler& a)
			{
				a.push(rax);
				a.pushad64();
				a.mov(rcx, r9); // index
				a.mov(rdx, qword_ptr(rsp, 0x88 + 0x20)); // column
				a.call_aligned(ui_feeder_item_text);
				a.mov(qword_ptr(rsp, 0x80), rax);
				a.popad64();
				a.pop(rax);

				a.mov(rsi, qword_ptr(rsp, 0x90));
				a.mov(rdi, rax);
				a.jmp(0x28E341_b);
			}), true);
		}
	};
}

REGISTER_COMPONENT(server_list::component)
