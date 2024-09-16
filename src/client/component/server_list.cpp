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

namespace server_list
{
	namespace tcp {
		// Paging
		const int server_limit_per_page = 100;
		int current_page = 0;
		bool first_refresh = true;

		// Used for when we're refreshing the server / favourites list
		bool getting_server_list = false;
		bool getting_favourites = false;

		// Used for stopping the threads abruptly
		bool interrupt_server_list = false;
		bool interrupt_favourites = false;

		struct PageData {
			int pageIndex = -1;
			std::vector<server_info> listed_servers;

			void add_server(server_info& info) {
				if (listed_servers.size() >= server_limit_per_page) {
					return;
				}

				listed_servers.emplace_back(info);
			}

			void clear_servers() {
				listed_servers.clear();
			}

			int get_server_count() {
				return static_cast<int>(listed_servers.size());
			}

			int get_player_count() {
				int count = 0;
				for (server_info& server : listed_servers)
				{
					count += server.clients - server.bots;
				}
				return count;
			}
		};

		std::vector<PageData> pages;
		
		std::string notification_message = "";

		bool error_is_displayed = false;
		std::string error_header = "Error";
		std::string error_message = "";

		std::string failed_to_join_reason = "";
	}

	namespace
	{
		const int server_limit = INT_MAX; // Handled in serverlist.lua now

		enum sort_types : uint32_t {
			sort_type_unknown = 0, // Patoke @todo: reverse
			sort_type_hostname = 1,
			sort_type_map = 2,
			sort_type_mode = 3,
			sort_type_players = 4,
			sort_type_ping = 5,
		};

		struct
		{
			game::netadr_s address{};
			volatile bool requesting = false;
			std::unordered_map<game::netadr_s, int> queued_servers{};
		} master_state;

		std::mutex mutex;
		std::vector<server_info> servers;

		size_t server_list_page = 0;
		volatile bool update_server_list = false;
		int list_sort_type = sort_type_hostname;
		std::chrono::high_resolution_clock::time_point last_scroll{};

		size_t get_page_count()
		{
			const auto count = servers.size() / server_limit;
			return count + (servers.size() % server_limit > 0);
		}

		size_t get_page_base_index()
		{
			return server_list_page * server_limit;
		}

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

		void parse_favourites()
		{
			std::lock_guard<std::mutex> _(mutex);

			nlohmann::json obj;
			if (!get_favourites_file(obj))
				return;

			for (auto& element : obj) 
			{
				if (!element.is_string())
					continue;

				game::netadr_s address{};
				game::NET_StringToAdr(element.get<std::string>().data(), &address);

				master_state.queued_servers[address] = 0;
			}
		}

		void refresh_server_list()
		{
			{
				std::lock_guard<std::mutex> _(mutex);
				servers.clear();

				for (tcp::PageData page : tcp::pages) {
					page.clear_servers();
				}
				tcp::pages.clear();
				tcp::current_page = 0;


				master_state.queued_servers.clear();
				server_list_page = 0;
			}

			ui_scripting::notify("updateGameList", {});
			ui_scripting::notify("updatePageCounter", {});
			// Use new TCP populate function
			auto* sort_type = game::Dvar_FindVar("ui_netSource");
			if (sort_type && sort_type->current.integer == 1)
			{
				// Internet
				if (server_list::tcp::getting_favourites) {
					server_list::tcp::interrupt_favourites = true;
				}

				server_list::tcp::populate_server_list_threaded();
			}
			else if (sort_type && sort_type->current.integer == 2)
			{
				// Favourites
				if (server_list::tcp::getting_server_list) {
					server_list::tcp::interrupt_server_list = true;
				}
	
				server_list::tcp::parse_favourites_tcp_threaded();
			}
		}

		void join_server(int, int, const int index)
		{
			std::lock_guard<std::mutex> _(mutex);

			const auto i = static_cast<size_t>(index) + get_page_base_index();
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
						party::connect(servers[i].address);
					}
					else {
						server_list::tcp::display_error("Failed to join server!", server_list::tcp::failed_to_join_reason);
					}
				}
			}
		}

		void trigger_refresh()
		{
			update_server_list = true;
		}

		int ui_feeder_count()
		{
			std::lock_guard<std::mutex> _(mutex);
			const auto count = static_cast<int>(servers.size());
			const auto index = get_page_base_index();
			const auto diff = count - index;
			return diff > server_limit ? server_limit : static_cast<int>(diff);
		}

		const char* ui_feeder_item_text(const int index, const int column)
		{
			std::lock_guard<std::mutex> _(mutex);

			const auto i = get_page_base_index() + index;

			if (i >= servers.size())
			{
				return "";
			}

			switch (column)
			{
			case 0:
				return servers[i].host_name.empty() ? "" : servers[i].host_name.data();
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
			default:
				return "";
			}
		}

		void insert_server(server_info&& server)
		{
			std::lock_guard<std::mutex> _(mutex);
			servers.emplace_back(std::move(server));
			sort_serverlist(list_sort_type);
			trigger_refresh();
		}

		void do_frame_work()
		{
			auto& queue = master_state.queued_servers;
			if (queue.empty())
			{
				return;
			}

			std::lock_guard<std::mutex> _(mutex);

			size_t queried_servers = 0;
			const size_t query_limit = 3;

			for (auto i = queue.begin(); i != queue.end();)
			{
				if (i->second)
				{
					const auto now = game::Sys_Milliseconds();
					if (now - i->second > 10'000)
					{
						i = queue.erase(i);
						continue;
					}
				}
				else if (queried_servers++ < query_limit)
				{
					i->second = game::Sys_Milliseconds();
					network::send(i->first, "getInfo", utils::cryptography::random::get_challenge());
				}

				++i;
			}
		}

		bool is_server_list_open()
		{
			return game::Menu_IsMenuOpenAndVisible(0, "menu_systemlink_join");
		}

		bool is_scrolling_disabled()
		{
			return update_server_list || (std::chrono::high_resolution_clock::now() - last_scroll) < 500ms;
		}

		bool scroll_down()
		{
			if (!is_server_list_open())
			{
				return false;
			}

			if (!is_scrolling_disabled() && server_list_page + 1 < get_page_count())
			{
				last_scroll = std::chrono::high_resolution_clock::now();
				++server_list_page;
				trigger_refresh();
			}

			return true;
		}

		bool scroll_up()
		{
			if (!is_server_list_open())
			{
				return false;
			}

			if (!is_scrolling_disabled() && server_list_page > 0)
			{
				last_scroll = std::chrono::high_resolution_clock::now();
				--server_list_page;
				trigger_refresh();
			}

			return true;
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

	void sort_serverlist(int sort_type)
	{
		list_sort_type = sort_type;

		std::vector<server_info> to_sort;

		for (auto& page : server_list::tcp::pages) 
		{
			for (server_info& server : page.listed_servers)
			{
				to_sort.emplace_back(server);
			}
		}

		// @Aphrodite TODO change this to sort pages
		std::stable_sort(to_sort.begin(), to_sort.end(), [sort_type](const server_info& a, const server_info& b)
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
			}

			return true;
		});

		// Clear pages
		tcp::pages.clear();

		int server_index = 0;
		for (server_info& server : to_sort) 
		{
			int page_number = tcp::get_page_number(server_index) - 1;
			tcp::add_server_to_page(page_number, server);
			server_index++;
		}

	}

	bool sl_key_event(const int key, const int down)
	{
		if (down)
		{
			if (key == game::keyNum_t::K_MWHEELUP)
			{
				return !scroll_up();
			}

			if (key == game::keyNum_t::K_MWHEELDOWN)
			{
				return !scroll_down();
			}
		}

		return true;
	}

	int get_player_count()
	{
		std::lock_guard<std::mutex> _(mutex);
		/*
		auto count = 0;
		for (const auto& server : servers)
		{
			count += server.clients - server.bots;
		}
		return count;*/

		int count = 0;
		for (server_list::tcp::PageData page : server_list::tcp::pages) {
			count += page.get_player_count();
		}
		return count;
	}

	int get_server_count()
	{
		std::lock_guard<std::mutex> _(mutex);
		//return static_cast<int>(servers.size());
		int count = 0;
		for (server_list::tcp::PageData page : server_list::tcp::pages) {
			count += page.get_server_count();
		}
		return count;
	}

	int get_server_limit()
	{
		return server_limit;
	}

	void add_favourite(int index)
	{
		nlohmann::json obj;
		if (!get_favourites_file(obj))
			return;

		if (tcp::current_page < 0 || tcp::current_page >= tcp::pages.size()) {
			return;
		}

		tcp::PageData& page = tcp::pages[tcp::current_page];

		if (index < 0 || index >= page.listed_servers.size()) {
			return;
		}

		server_info& info = page.listed_servers[index];

		if (obj.find(info.connect_address) != obj.end())
		{
			utils::toast::show("Error", "Server already marked as favourite.");
			return;
		}

		obj.emplace_back(info.connect_address);
		utils::io::write_file("players2/favourites.json", obj.dump());
		utils::toast::show("Success", "Server added to favourites.");

		console::debug("added %s to favourites", info.connect_address);
	}

	void delete_favourite(int index)
	{
		nlohmann::json obj;
		if (!get_favourites_file(obj)) {
			return;
		}

		if (tcp::current_page < 0 || tcp::current_page >= tcp::pages.size()) {
			return;
		}

		tcp::PageData& page = tcp::pages[tcp::current_page];

		if (index < 0 || index >= page.listed_servers.size()) {
			return;
		}

		server_info& info = page.listed_servers[index];

		for (auto it = obj.begin(); it != obj.end(); ++it)
		{
			if (!it->is_string()) {
				continue;
			}

			if (it->get<std::string>() == info.connect_address) // Check if the element matches the connect address
			{
				console::debug("removed %s from favourites", info.connect_address);
				obj.erase(it);
				break;
			}
		}

		if (!utils::io::write_file("players2/favourites.json", obj.dump())) {
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

	void sort_servers(int sort_type)
	{
		std::lock_guard<std::mutex> _(mutex);
		sort_serverlist(sort_type);
		trigger_refresh();
	}

	void server_list::tcp::populate_server_list()
	{
		std::string master_server_list = hmw_tcp_utils::GET_url(hmw_tcp_utils::MasterServer::get_master_server());

		// An error message is visible. We want to hide it now.
		if (error_is_displayed) {
			ui_scripting::notify("hideErrorMessage", {});
			error_is_displayed = false;
		}

		notification_message = "Refreshing server list...";
		ui_scripting::notify("showRefreshingNotification", {});

		int server_index = 0;

		// @Aphrodite todo, update this to be dynamic and not hard coded to 27017
		console::info("Checking if localhost server is running on default port (27017)");
		std::string port = "27017"; // Change this to the dynamic port range @todo
		bool localhost = hmw_tcp_utils::GameServer::is_localhost(port);
		if (localhost) {
			std::string local_res = hmw_tcp_utils::GET_url("localhost:27017/getInfo", true);
			add_server_to_list(local_res, "localhost:27017", server_index);
			ui_scripting::notify("updateGameList", {});
			server_index++;
		}

		// Master server did not respond
		if (master_server_list.empty()) {
			console::info("Failed to get response from master server!");
			getting_server_list = false;
			ui_scripting::notify("updateGameList", {});
			ui_scripting::notify("hideRefreshingNotification", {});
			display_error("MASTER SERVER ERROR!", "No response!");
			return;
		}

		nlohmann::json master_server_response_json = nlohmann::json::parse(master_server_list);

		// Parse server list
		for (const auto& element : master_server_response_json) {
			if (interrupt_server_list) {
				break;
			}

			std::string connect_address = element.get<std::string>(); 
			std::string game_server_info = connect_address + "/getInfo";
			std::string game_server_response = hmw_tcp_utils::GET_url(game_server_info.c_str(), true);
			
			if (game_server_response.empty()) {
				continue;
			}

			tcp::add_server_to_list(game_server_response, connect_address, server_index);
			ui_scripting::notify("updateGameList", {});
			server_index++;
		}

		load_page(0);
		interrupt_server_list = false;
		getting_server_list = false;
		ui_scripting::notify("updateGameList", {});
		ui_scripting::notify("hideRefreshingNotification", {});
	}

	void tcp::populate_server_list_threaded()
	{
		if (getting_server_list) {
			return;
		}

		scheduler::once([=]()
		{
			getting_server_list = true;
			populate_server_list();
		}, scheduler::pipeline::async);
	}

	void tcp::parse_favourites_tcp_threaded()
	{
		if (getting_favourites) {
			return;
		}

		scheduler::once([=]()
		{
			getting_favourites = true;
			parse_favourites_tcp();
		}, scheduler::pipeline::async);
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
		int total_servers = get_server_count();
		int total_pages = static_cast<int>(std::ceil(static_cast<double>(total_servers) / server_limit_per_page));
		return total_pages;
	}

	int tcp::get_page_number(int serverIndex)
	{
		int pageNumber = static_cast<int>(std::ceil(static_cast<double>(serverIndex) / server_limit_per_page));
		if (pageNumber == 0) {
			pageNumber++;
		}
		return pageNumber;
	}

	// Once this is working implement next and previous page functions

	void tcp::load_page(int pageNumber)
	{
		if (pageNumber < 0) {
			return;
		}
		if (pageNumber >= pages.size()) {
			return;
		}

		servers.clear();

		PageData page = pages[pageNumber];
		std::string log = "Load page " + std::to_string(pageNumber);
		console::info(log.c_str());
		for (server_info server : page.listed_servers) {
			insert_server(std::move(server));
		}
		ui_scripting::notify("updateGameList", {});
		ui_scripting::notify("updatePageCounter", {});
		current_page = pageNumber;
	}

	void tcp::next_page()
	{
		current_page++;
		if (current_page >= get_total_pages()) {
			load_page(0); // Load first page
			return;
		}
		load_page(current_page);
	}

	void tcp::previous_page()
	{
		current_page--;
		if (current_page < 0) {
			load_page(get_total_pages() - 1); // Load to last page
			return;
		}
		load_page(current_page);
	}

	void tcp::add_server_to_page(int pageIndex, server_info &serverInfo)
	{
		// Page index can't be negative
		if (pageIndex < 0) {
			return;
		}

		if (pageIndex >= pages.size()) {
			pages.resize(pageIndex + 1);
			pages[pageIndex].pageIndex = pageIndex;
		}

		pages[pageIndex].add_server(serverInfo);
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
		if (error_is_displayed) {
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
		}, scheduler::pipeline::main, 3s);
	}

	bool tcp::check_can_join(std::string& connect_address)
	{
		std::string game_server_info = connect_address + "/getInfo";
		std::string game_server_response = hmw_tcp_utils::GET_url(game_server_info.c_str(), true);

		if (game_server_response.empty()) {
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

		int max_clients = std::stoi(maxclients);
		int current_clients = std::stoi(clients);
		int current_bots = std::stoi(bots);
		int player_count = current_clients - current_bots;

		if (player_count == max_clients) {
			failed_to_join_reason = "Game is full.";
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

		game::netadr_s address{};
		if (game::NET_StringToAdr(connect_address.c_str(), &address))
		{
			server_info server{};
			server.address = address;
			server.host_name = game_server_response_json["hostname"];
			server.map_name = game_server_response_json["mapname"];

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

			server.ping = std::atoi(ping.c_str());

			std::string isPrivate = game_server_response_json["isPrivate"];
			server.is_private = atoi(isPrivate.c_str()) == 1;

			server.connect_address = connect_address;
			//insert_server(std::move(server)); // Old method of adding servers to the list. Replaced with paging

			int page_number = get_page_number(server_index) - 1;
			/* This breaks things ???
			if (page_number == 0) {
				insert_server(std::move(server)); // Populate the first page in real time
			}*/

			add_server_to_page(page_number, server);
		}
	}

	void tcp::parse_favourites_tcp()
	{
		notification_message = "Loading favourites...";
		ui_scripting::notify("showRefreshingNotification", {});
		int server_index = 0;
		nlohmann::json obj;
		if (!get_favourites_file(obj)) {
			ui_scripting::notify("updateGameList", {});
			return;
		}

		for (auto& element : obj) {
			if (interrupt_favourites) {
				break;
			}

			if (!element.is_string())
				continue;

			std::string connect_address = element;
			std::string game_server_info = connect_address + "/getInfo";
			std::string game_server_response = hmw_tcp_utils::GET_url(game_server_info.c_str(), true);

			// Don't show any non TCP servers
			if (game_server_response.empty()) {
				continue;
			}

			tcp::add_server_to_list(game_server_response, connect_address, server_index);
			ui_scripting::notify("updateGameList", {});
			server_index++;
		}
		load_page(0);
		console::info("Finished getting favourites!");
		interrupt_favourites = false;
		getting_favourites = false;
		ui_scripting::notify("updateGameList", {});
		ui_scripting::notify("hideRefreshingNotification", {});
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

			scheduler::loop(do_frame_work, scheduler::pipeline::main);
		}
	};
}

REGISTER_COMPONENT(server_list::component)
