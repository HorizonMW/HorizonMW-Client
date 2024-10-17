#include <std_include.hpp>
#include "loader/component_loader.hpp"

#include "command.hpp"
#include "console.hpp"
#include "scheduler.hpp"
#include "network.hpp"
#include "party.hpp"
#include "scripting.hpp"

#include "game/game.hpp"
#include "game/scripting/execution.hpp"

#include <utils/hook.hpp>
#include <utils/string.hpp>
#include <utils/cryptography.hpp>
#include <utils/io.hpp>

namespace bots
{
	namespace
	{
		bool can_add()
		{
			return party::get_client_count() < *game::svs_numclients
				&& game::SV_Loaded() && !game::VirtualLobby_Loaded();
		}

		void bot_team_join(const int entity_num, bool spawnAtMe = false)
		{
			const game::scr_entref_t entref{static_cast<uint16_t>(entity_num), 0};
			scheduler::once([entref, spawnAtMe, entity_num]
			{
				scripting::notify(entref, "luinotifyserver", {"team_select", 2});
				scheduler::once([entref, spawnAtMe, entity_num]
				{
					auto* _class = utils::string::va("class%d", utils::cryptography::random::get_integer() % 5);
					scripting::notify(entref, "luinotifyserver", {"class_select", _class});

					if (spawnAtMe)
					{
						scheduler::once([entity_num]
						{
							auto botEntity = scripting::call("getentbynum", { entity_num }).as<scripting::entity>();
							auto host = scripting::call("getentbynum", { 0 }).as<scripting::entity>();
							auto hostOrigin = host.call("getorigin").as<scripting::vector>();
							botEntity.call("freezecontrols", { true });
							botEntity.call("setorigin", { hostOrigin });
						}, scheduler::pipeline::server, 2s);
					}

				}, scheduler::pipeline::server, 2s);
			}, scheduler::pipeline::server, 2s);
		}

		void spawn_bot(const int entity_num, bool spawnAtMe = false)
		{
			game::SV_SpawnTestClient(&game::g_entities[entity_num]);
			if (game::Com_GetCurrentCoDPlayMode() == game::CODPLAYMODE_CORE)
			{
				bot_team_join(entity_num, spawnAtMe);
			}
		}

		void add_bot()
		{
			if (!can_add())
			{
				return;
			}

			const auto* const bot_name = game::SV_BotGetRandomName();
			const auto* bot_ent = game::SV_AddBot(bot_name);

			if (bot_ent)
			{
				spawn_bot(bot_ent->s.number, false);
			}
			else
			{
				scheduler::once([]
				{
					add_bot();
				}, scheduler::pipeline::server, 100ms);
			}
		}

		void add_bot_at_me()
		{
			if (!can_add())
			{
				return;
			}

			const auto* const bot_name = game::SV_BotGetRandomName();
			const auto* bot_ent = game::SV_AddBot(bot_name);

			if (bot_ent)
			{
				spawn_bot(bot_ent->s.number, true);
			}
			else
			{
				scheduler::once([]
				{
					add_bot_at_me();
				}, scheduler::pipeline::server, 100ms);
			}
		}

#ifdef DEBUG
		void teleport_to_me(const int client)
		{
			auto botEntity = scripting::call("getentbynum", { client }).as<scripting::entity>();

			if (botEntity != NULL)
			{
				auto host = scripting::call("getentbynum", { 0 }).as<scripting::entity>();
				auto hostOrigin = host.call("getorigin").as<scripting::vector>();
				botEntity.call("freezecontrols", { true });
				botEntity.call("setorigin", { hostOrigin });
			}
			else
			{
				console::error("entity %d does not exist.", client);
			}
		}
#endif

		utils::hook::detour get_bot_name_hook;
		std::vector<std::string> bot_names{};

		void load_bot_data()
		{
			static const char* bots_txt = "hmw-mod/bots.txt";

			std::string bots_content;
			if (!utils::io::read_file(bots_txt, &bots_content))
			{
				return;
			}

			auto names = utils::string::split(bots_content, '\n');
			for (auto& name : names)
			{
				name = utils::string::replace(name, "\r", "");
				if (!name.empty())
				{
					bot_names.emplace_back(name);
				}
			}
		}
		
		size_t bot_id = 0;

		const char* get_random_bot_name()
		{
			if (bot_names.empty())
			{
				load_bot_data();
			}

			// only use bot names once, no dupes in names
			if (!bot_names.empty() && bot_id < bot_names.size())
			{
				bot_id %= bot_names.size();
				const auto& entry = bot_names.at(bot_id++);
				return utils::string::va("%.*s", static_cast<int>(entry.size()), entry.data());
			}

			return get_bot_name_hook.invoke<const char*>();
		}
	}

	class component final : public component_interface
	{
	public:
		void post_unpack() override
		{
			command::add("spawnBot", [](const command::params& params)
			{
				if (!can_add())
				{
					return;
				}

				auto num_bots = 1;
				if (params.size() == 2)
				{
					num_bots = atoi(params.get(1));
				}

				num_bots = std::min(num_bots, *game::svs_numclients);

				for (auto i = 0; i < num_bots; i++)
				{
					scheduler::once(add_bot, scheduler::pipeline::server, 100ms * i);
				}
			});

#ifdef DEBUG
			command::add("spawnbotatme", [](const command::params& params)
			{
				if (!can_add())
				{
					return;
				}

				auto num_bots = 1;
				if (params.size() == 2)
				{
					num_bots = atoi(params.get(1));
				}
				num_bots = std::min(num_bots, *game::svs_numclients);

				for (auto i = 0; i < num_bots; i++)
				{
					scheduler::once(add_bot_at_me, scheduler::pipeline::server, 100ms * i);
				}
			});

			command::add("teleporttome", [](const command::params& params)
			{
				if (params.size() < 2)
				{
					return;
				}

				auto client = atoi(params.get(1));

				if (client == 0)
				{
					console::debug("cannot teleport the host to themselves.");
					return;
				}

				if ((client + 1) > party::get_client_count())
				{
					console::debug("Entity does not exist.");
					return;
				}

				scheduler::once([client]
				{
					teleport_to_me(client);
				}, scheduler::pipeline::server, 100ms);
			});
#endif // DEBUG

			// Clear bot names and reset ID on game shutdown to allow new names to be added without restarting
			scripting::on_shutdown([](bool /*free_scripts*/, bool post_shutdown)
			{
				if (!post_shutdown)
				{
					bot_names.clear();
					bot_id = 0;
				}
			});
		}
	};
}

REGISTER_COMPONENT(bots::component)
