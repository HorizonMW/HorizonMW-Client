#include "std_include.hpp"
#include "hmw_tcp_utils.hpp"

#include "game/dvars.hpp"
#include "game/game.hpp"

#include "component/scheduler.hpp"
#include "component/fastfiles.hpp"
#include "component/party.hpp"
#include "component/command.hpp"

#include "steam/steam.hpp"

#include "utils/hash.hpp"

#include <utils/properties.hpp>
#include <utils/string.hpp>
#include <utils/info_string.hpp>
#include <utils/cryptography.hpp>
#include <utils/hook.hpp>
#include <utils/io.hpp>
#include <utils/http.hpp>

#include <curl/curl.h>
#include "version.hpp"

namespace hmw_tcp_utils {

	std::string get_version()
	{
		std::string version(VERSION);
		return version;
	}

	namespace MasterServer {

		std::string master_server_url = "https://ms.s2mod.to/game-servers";

		bool get_dvar_netip_for_heartbeat(std::string& addr)
		{
			const std::string& dvar = "net_ip";
			auto* dvar_value = game::Dvar_FindVar(dvar.data());
			if (dvar_value && dvar_value->current.string) {
				std::string ip_str = dvar_value->current.string;
				struct sockaddr_in sa;
				if (ip_str != "0.0.0.0" && inet_pton(AF_INET, ip_str.c_str(), &(sa.sin_addr)) == 1)
				{
					addr = std::string(dvar_value->current.string);
					return true;
				}
			}

			return false;
		}

		void send_heartbeat()
		{
			std::string info_json = getInfo_Json();
			CURL* curl = curl_easy_init();

			if (curl) {
				const std::string url = master_server_url;

				const std::string data = info_json;

				curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
				curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");
				curl_easy_setopt(curl, CURLOPT_POSTFIELDS, data.c_str());

				std::string interface_ip;
				if (get_dvar_netip_for_heartbeat(interface_ip))
				{
					curl_easy_setopt(curl, CURLOPT_INTERFACE, interface_ip);
				}

				struct curl_slist* headers = nullptr;
				headers = curl_slist_append(headers, "Content-Type: application/json");
				curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

				CURLcode res = curl_easy_perform(curl);

				if (res != CURLE_OK) {
					std::cerr << "Failed to send heartbeat: " << curl_easy_strerror(res) << std::endl;
				}
				else {
					long response_code;
					curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);

					switch (response_code) {
						case 204:
							// No content (This is our success)
							console::info("Heartbeat sent to master server!");
							break;
						case 400:
							// Bad request
							console::info("Master server returned a bad request.");
							break;
						case 524:
						{
							const char* port = utils::string::va("%i", party::get_dvar_int("net_port"));
							std::string error = "Failed to send heartbeat; check TCP portforwarding on port " + std::string(port);
							console::info(error.c_str());
							break;
						}
						default:
							// Unknown
							break;
					}
				}

				curl_slist_free_all(headers);
				curl_easy_cleanup(curl);
			}
		}
		
		const char* get_master_server() {
			return master_server_url.c_str();
		}
	}

	namespace GameServer {
		void mg_fn(mg_connection* c, int ev, void* ev_data)
		{
			if (ev == MG_EV_HTTP_MSG) {
				struct mg_http_message* hm = (struct mg_http_message*)ev_data;
				if (mg_match(hm->uri, mg_str("/status"), NULL)) {
					mg_http_reply(c, 200, "", "{%m:%d}\n", MG_ESC("status"), 1);
				}
				else if (mg_match(hm->uri, mg_str("/getInfo"), NULL)) { 
					if (ev == MG_EV_OPEN) { // Init stuff here
						// Nothing atm
					}
					else if (ev == MG_EV_CLOSE) {
						// Connection closed
						return;
					}

					std::map<std::string, std::string> headers;

					for (size_t i = 0; i < MG_MAX_HTTP_HEADERS && hm->headers[i].name.len > 0; i++) {
						struct mg_str* name = &hm->headers[i].name;
						struct mg_str* value = &hm->headers[i].value;

						// Convert the mg_str to std::string
						std::string headerName(name->buf, name->len);
						std::string headerVal(value->buf, value->len);
						headers.emplace(headerName, headerVal);

						// FUTURE: Process any header names here
						
					}

					std::string data = getInfo_Json();
					mg_http_reply(c, 200, "", data.c_str());
				}
			}
		}

		void start_mg_server(std::string url)
		{
			const char* c_url = url.c_str();
			console::info("Starting server on: %s", c_url);
			struct mg_mgr mgr;
			mg_mgr_init(&mgr);
			// @Aphrodite, Conn is unused but defined incase it needs to be used in the future.
			mg_connection* conn = mg_http_listen(&mgr, c_url, mg_fn, NULL);
			(void)conn;

			for (;;) {
				mg_mgr_poll(&mgr, 1000);
			}
		}

		void start_server(std::string url)
		{
			std::thread server_thread(start_mg_server, url);
			server_thread.detach();
		}

		bool is_localhost(std::string port)
		{
			std::string url = "http://127.0.0.1:" + port + "/status";
			std::string res = GET_url(url.c_str(), {}, false, 1500L, false, 1);
			return res != "";
		}
	
		bool check_download_mod_tcp(const nlohmann::json infoJson, std::vector<download::file_t>& files)
		{
			static const auto fs_game = game::Dvar_FindVar("fs_game");
			const auto client_fs_game = utils::string::to_lower(fs_game->current.string);
			auto t = infoJson.dump();
			std::string res_fs_game = infoJson["fs_game"];
			const auto server_fs_game = utils::string::to_lower(res_fs_game);

			if (server_fs_game.empty() && client_fs_game.empty())
			{
				return false;
			}

			if (server_fs_game.empty() && !client_fs_game.empty())
			{
				//mods::set_mod("");
				return true;
			}

			if (!server_fs_game.starts_with("mods/") || server_fs_game.contains('.') || server_fs_game.contains("::"))
			{
				throw std::runtime_error(utils::string::va("Invalid server fs_game value '%s'", server_fs_game.data()));
			}

			auto needs_restart = false;
			for (const auto& file : party::mod_files)
			{
				const auto source_hash = infoJson[file.name];
				if (source_hash.empty())
				{
					if (file.optional)
					{
						continue;
					}

					throw std::runtime_error(
						utils::string::va("Server '%s' is empty", file.name.data()));
				}

				const auto file_path = server_fs_game + "/mod" + file.extension;
				auto has_to_download = !utils::io::file_exists(file_path);

				if (!has_to_download)
				{
					const auto hash = party::get_file_hash(file_path);
					console::debug("has_to_download = %s != %s\n", source_hash, hash.data());
					has_to_download = source_hash != hash;
				}

				if (has_to_download)
				{
					// unload mod before downloading it
					if (client_fs_game == server_fs_game)
					{
						//mods::set_mod("", true);
						return true;
					}
					else
					{
						files.emplace_back(file_path, source_hash);
					}
				}
				else if (client_fs_game != server_fs_game)
				{
					//mods::set_mod(server_fs_game);
					needs_restart = true;
				}
			}

			return needs_restart;
		}

		void check_download_map_tcp(const nlohmann::json infoJson, std::vector<download::file_t>& files)
		{
			const std::string mapname = infoJson["mapname"];
			if (fastfiles::is_stock_map(mapname))
			{
				return;
			}

			if (mapname.contains('.') || mapname.contains("::"))
			{
				throw std::runtime_error(utils::string::va("Invalid server mapname value '%s'", mapname.data()));
			}

			const auto check_file = [&](const party::usermap_file& file)
				{
					const std::string filename = utils::string::va("h2m-usermaps/%s/%s%s",
						mapname.data(), mapname.data(), file.extension.data());
					const std::string source_hash = infoJson[file.name];
					if (source_hash.empty())
					{
						if (!file.optional)
						{
							std::string missing_value = "Server '%s' is empty";
							if (file.name == "usermap_hash"s)
							{
								missing_value += " (or you are missing content for map '%s')";
							}
							throw std::runtime_error(utils::string::va(missing_value.data(), file.name.data(), mapname.data()));
						}

						return;
					}

					const auto hash = party::get_file_hash(filename);
					console::debug("hash != source_hash => %s != %s\n", source_hash.data(), hash.data());
					if (hash != source_hash)
					{
						files.emplace_back(filename, source_hash);
						return;
					}
				};

			for (const auto& file : party::usermap_files)
			{
				check_file(file);
			}
		}

		bool download_files_tcp(const game::netadr_s& target, nlohmann::json infoJson, bool allow_download)
		{
			try
			{
				std::vector<download::file_t> files{};

				const auto needs_restart = check_download_mod_tcp(infoJson, files);

				party::needs_vid_restart = party::needs_vid_restart || needs_restart;
				check_download_map_tcp(infoJson, files);
				if (files.size() > 0)
				{
					if (!allow_download)
					{
						return true;
					}

					download::stop_download();
					download::start_download_tcp(target, infoJson, files);
					return true;
				}
				else if (needs_restart || party::needs_vid_restart)
				{
					command::execute("vid_restart");
					party::needs_vid_restart = false;
					scheduler::once([=]()
						{
							//mods::read_stats();
							party::connect(target);
						}, scheduler::pipeline::main);
					return true;
				}
			}
			catch (const std::exception& e)
			{
				party::menu_error(e.what());
				return true;
			}

			return false;
		}

}

#pragma region Misc funcs
std::string getInfo_Json()
	{
		nlohmann::json data;
		const auto mapname = party::get_dvar_string("mapname");

		//data["challenge"] = ""; // This is the server challenge, its unused now.
		data["gamename"] = "H2M";
		data["gameversion"] = get_version();
		data["hostname"] = party::get_dvar_string("sv_hostname");
		data["gametype"] = party::get_dvar_string("g_gametype");
		data["sv_motd"] = party::get_dvar_string("sv_motd");
		data["xuid"] = utils::string::va("%llX", steam::SteamUser()->GetSteamID().bits);
		data["mapname"] = mapname;
		data["isPrivate"] = party::get_dvar_string("g_password").empty() ? "0" : "1";
		data["clients"] = utils::string::va("%i", party::get_client_count());
		data["bots"] = utils::string::va("%i", party::get_bot_count());
		data["sv_maxclients"] = utils::string::va("%i", *game::svs_numclients);
		data["protocol"] = utils::string::va("%i", PROTOCOL);
		data["playmode"] = utils::string::va("%i", game::Com_GetCurrentCoDPlayMode());
		data["sv_running"] = utils::string::va("%i", party::get_dvar_bool("sv_running") && !game::VirtualLobby_Loaded());
		data["dedicated"] = utils::string::va("%i", party::get_dvar_bool("dedicated"));
		data["sv_wwwBaseUrl"] = party::get_dvar_string("sv_wwwBaseUrl");
		data["sv_discordImageUrl"] = party::get_dvar_string("sv_discordImageUrl");
		data["sv_discordImageText"] = party::get_dvar_string("sv_discordImageText");
		data["port"] = utils::string::va("%i", party::get_dvar_int("net_port"));
		data["sv_privateClients"] = utils::string::va("%i", party::get_dvar_int("sv_privateClients"));

		if (!fastfiles::is_stock_map(mapname))
		{
			for (const auto& file : party::usermap_files)
			{
				const auto path = party::get_usermap_file_path(mapname, file.extension);
				const auto hash = party::get_file_hash(path);
				data[file.name] = hash;
			}
		}

		const auto fs_game = party::get_dvar_string("fs_game");
		data["fs_game"] = fs_game;

		if (!fs_game.empty())
		{
			for (const auto& file : party::mod_files)
			{
				const auto hash = party::get_file_hash(utils::string::va("%s/mod%s",
					fs_game.data(), file.extension.data()));
				data[file.name] = hash;
			}
		}

		std::string jsonString = data.dump();
		return jsonString;
	}

std::string GET_url(const char* url, const std::map<std::string, std::string>& headers, bool addPing, long timeout, bool doRetry, int retryMax) {
	CURL* curl;
	CURLcode res;

	std::string response = "";
	int retryCount = 0;

	while (retryCount <= retryMax) {
		curl = curl_easy_init();
		if (!curl) {
			std::cerr << "Failed to initialize CURL" << std::endl;
			return "";
		}

		std::string readBuffer;
		curl_easy_setopt(curl, CURLOPT_URL, url);
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, GET_url_WriteCallback);
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
		curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, timeout);

		struct curl_slist* curlHeaders = nullptr;
		for (const auto& header : headers) {
			std::string headerString = header.first + ": " + header.second;
			curlHeaders = curl_slist_append(curlHeaders, headerString.c_str());
		}
		curl_easy_setopt(curl, CURLOPT_HTTPHEADER, curlHeaders);  // Set the headers option

		res = curl_easy_perform(curl);

		if (res == CURLE_OK) {
			response = readBuffer;

			if (addPing) {
				double totalTime = 0.0;
				double connectTime = 0.0;

				curl_easy_getinfo(curl, CURLINFO_TOTAL_TIME, &totalTime);
				int responseTime = static_cast<int>(totalTime * 1000.0);

				curl_easy_getinfo(curl, CURLINFO_CONNECT_TIME, &connectTime);
				int overhead = static_cast<int>(connectTime * 1000.0);

				int ping = responseTime - overhead;
				if (ping == 0) ping = 1;

				if (!response.empty() && response.back() == '}') {
					response.pop_back();
					response += ", \"ping\": \"" + std::to_string(ping) + "\"}";
				}
			}

			curl_easy_cleanup(curl);
			return response;  // Success, return the response
		}
		else if (res == CURLE_COULDNT_RESOLVE_HOST) {
			console::info("Failed to resolve host. Aborting...");
			curl_easy_cleanup(curl);
			break;  // Stop retrying if host can't be resolved
		}
		else if (res == CURLE_COULDNT_CONNECT) {
			console::info("Could not connect to host. Retrying...");
		}
		else if (res == CURLE_OPERATION_TIMEDOUT) {
			console::info("Request timed out. Retrying...");
		}
		else if (res == CURLE_HTTP_RETURNED_ERROR) {
			long response_code = 0;
			curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);

			console::info("HTTP error: %ld", response_code);  // Using %ld for long
			if (response_code >= 500) {
				console::info("Retrying due to server error (5xx)...");
			}
			else {
				console::info("Non-retryable error (4xx). Aborting...");
				curl_easy_cleanup(curl);
				break;  // Abort on client-side errors
			}
		}
		else if (res == CURLE_SEND_ERROR || res == CURLE_RECV_ERROR) {
			console::info("Network send/receive error. Retrying...");
		}
		else {
			std::cerr << "GET request failed: " << curl_easy_strerror(res) << std::endl;
			curl_easy_cleanup(curl);
			break;  // Abort for non-retryable errors
		}

		// If we do not actually want this GET request to retry, only used by localhost check
		if (!doRetry) {
			// We do not want to retry
			console::info("Retry aborted.");
			break;
		}

		retryCount++;
		if (retryCount > retryMax) {
			console::info("Reached max retries (%d). Aborting...", retryMax - 1);
			break;  // Stop retrying if max retries are reached
		}

		timeout *= 2;  // Exponential backoff
		console::debug("Retrying request #%d with timeout %ld ms...", retryCount, timeout);

		curl_easy_cleanup(curl);  // Clean up before retrying
	}

	return response;  // Return an empty response if all retries failed
}

	size_t GET_url_WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
		((std::string*)userp)->append((char*)contents, size * nmemb);
		return size * nmemb;
	}
#pragma endregion
}