#include <std_include.hpp>
#include "loader/component_loader.hpp"

#include "network.hpp"
#include "version.hpp"
#include "console.hpp"
#include "command.hpp"

#include <utils/properties.hpp>
#include <utils/string.hpp>
#include <utils/info_string.hpp>
#include <utils/cryptography.hpp>

#include "server_info.hpp"

namespace server_info
{
	std::string get_dvar_string(const std::string& dvar)
	{
		auto* dvar_value = game::Dvar_FindVar(dvar.data());
		if (dvar_value && dvar_value->current.string)
		{
			return dvar_value->current.string;
		}

		return {};
	}

	utils::info_string server_info::GetInfo()
	{
		auto maxClientCount = *game::svs_numclients;
		const auto password = get_dvar_string("g_password");

		if (!maxClientCount)
		{
			auto max_players = game::Dvar_FindVar("party_maxplayers");
			auto max_players_count = 18;

			if (max_players)
				max_players_count = max_players->current.integer;

			maxClientCount = max_players_count;
		}

		auto server_info = game::Dvar_InfoString_Big(1 << 10);
		utils::info_string info(std::format("{}"s, server_info));

		info.set("gamename", "HMW");
		info.set("sv_maxclients", std::to_string(maxClientCount));
		info.set("protocol", std::to_string(PROTOCOL));
		info.set("version", VERSION);
		info.set("mapname", get_dvar_string("mapname"));
		info.set("isPrivate", get_dvar_string("g_password").empty() ? "0" : "1");
		info.set("checksum", utils::string::va("%X", utils::cryptography::jenkins_one_at_a_time::compute(std::to_string(game::Sys_Milliseconds()))));

		// Ensure mapname is set
		if (info.get("mapname").empty())
		{
			info.set("mapname", get_dvar_string("ui_mapname"));
		}

		return info;
	}

	class component final : public component_interface
	{
	public:
		void post_unpack() override
		{
			network::on("getStatus", [](const game::netadr_s& target, [[maybe_unused]] const std::string& data)
			{
				std::string playerList;
				utils::info_string info;
				info.set("challenge", data);

				for (int i = 0; i < 18; ++i)
				{
					auto score = 0;
					auto ping = 0;
					std::string name;

					if (game::environment::is_dedi())
					{
						const auto* svs_clients = *game::svs_clients;
						if (!svs_clients) continue;
						if (svs_clients[i].header.state < 5) continue;
						if (!svs_clients[i].gentity || !svs_clients[i].gentity->client) continue;

						const auto* client = svs_clients[i].gentity->client;
						const auto team = client->team;
						if (game::SV_BotIsBot(i) || team == 3)
						{
							continue;
						}

						score = game::G_GetClientScore(i);
						ping = game::SV_GetClientPing(i);
						name = svs_clients[i].name;

						playerList.append(std::format("{} {} \"{}\"\n", score, ping, name));
					}
				}

				network::send(target, "statusResponse", info.build() + "\n"s + playerList + "\n"s);
			});

#ifdef DEBUG
			network::on("statusResponse", [](const game::netadr_s& target, [[maybe_unused]] const std::string& data)
			{
				const auto pos = data.find_first_of('\n');
				if (pos == std::string::npos)
				{
					return;
				}

				const utils::info_string info(data.substr(0, pos));

				console::debug("%s", data.c_str());
			});
#endif // DEBUG
		}
	};
}

REGISTER_COMPONENT(server_info::component)