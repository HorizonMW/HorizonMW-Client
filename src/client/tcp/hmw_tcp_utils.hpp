#pragma once

#include <string>
#include <mongoose.h>
#include <json.hpp>

#include <component/download.hpp>

namespace hmw_tcp_utils {

	std::string get_version();

	// MG functions

	namespace MasterServer {
		void send_heartbeat();

		const char* get_master_server();
	}

	namespace GameServer {
		void mg_fn(struct mg_connection* c, int ev, void* ev_data);

		void start_mg_server(std::string url);

		void start_server(std::string url);

		bool is_localhost(std::string port);

		bool check_download_mod_tcp(const nlohmann::json infoJson, std::vector<download::file_t>& files);

		void check_download_map_tcp(const nlohmann::json infoJson, std::vector<download::file_t>& files);

		bool download_files_tcp(const game::netadr_s& target, nlohmann::json infoJson, bool allow_download);

	}

#pragma region Misc functions
	std::string getInfo_Json();
	std::string GET_url(const char* url, const std::map<std::string, std::string>& headers = {}, bool addPing = false, long timeout = 1500L, bool doRetry = false, int retryMax = 4);

	size_t GET_url_WriteCallback(void* contents, size_t size, size_t nmemb, void* userp);
#pragma endregion

}

