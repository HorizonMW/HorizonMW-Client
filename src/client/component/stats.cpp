#include <std_include.hpp>
#include "loader/component_loader.hpp"

#include "scheduler.hpp"
#include "dvars.hpp"

#include "game/game.hpp"
#include "game/dvars.hpp"

#include "command.hpp"

#include <utils/nt.hpp>
#include <utils/hook.hpp>
#include <utils/flags.hpp>
#include <utils/string.hpp>

#include "console.hpp"
#include "network.hpp"
#include "stats.hpp"

namespace stats
{
	namespace
	{
		game::dvar_t* cg_unlock_all_items;
		game::dvar_t* cg_unlock_all_loot;

		utils::hook::detour is_item_unlocked_hook;
		utils::hook::detour is_item_unlocked_hook2;

		int is_item_unlocked_stub(int a1, void* a2, void* a3, void* a4, int a5, void* a6)
		{
			if (cg_unlock_all_items && cg_unlock_all_items->current.enabled)
			{
				return 0;
			}

			return is_item_unlocked_hook.invoke<int>(a1, a2, a3, a4, a5, a6);
		}

		int is_item_unlocked()
		{
			return 0;
		}

		int is_item_unlocked_stub2(void* a1, void* a2)
		{
			const auto state = is_item_unlocked_hook2.invoke<int>(a1, a2);
			if (state == 15 /*Not In Inventory*/ && cg_unlock_all_loot && cg_unlock_all_loot->current.enabled)
			{
				return 0;
			}
			return state;
		}
	}

	static bool LiveStorage_DoWeHaveStats(int controller)
	{
		return *(&game::controllerStatData + 0x1BC88 * controller + 0x943C * game::LiveStorage_GetActiveStatsSource(controller) + 0x9406);
	}

	static char* LiveStorage_GetStatsBuffer(int controller, int index)
	{
		auto v11 = 0x1BC88 * controller;
		return (char*)&game::controllerStatData + 0x943C * game::LiveStorage_GetActiveStatsSource(controller) + v11 + (1240 * index);
	}

	void send_stats()
	{
		if (*game::connect_state != nullptr && *game::connectionState >= game::CA_LOADING)
		{
			for (auto i = 0; i < 31; i++)
			{
				console::debug("Sending stat packet %i to server.", i);

				game::msg_t msg{};
				unsigned char buffer[2048]{};

				game::MSG_Init(&msg, buffer, sizeof(buffer));
				game::MSG_WriteString(&msg, "stats");

				char* statbuffer = nullptr;

				if (LiveStorage_DoWeHaveStats(0))
				{
					console::debug("We have stats!!!!");
					statbuffer = LiveStorage_GetStatsBuffer(0, i);
				}
				else
					console::debug("we have no stats?");

				game::MSG_WriteShort(&msg, *reinterpret_cast<short*>(0x142EC8510));
				game::MSG_WriteByte(&msg, static_cast<char>(i));

				if (statbuffer)
				{
					console::debug("we got a statbuffer and we are sending it to the server");
					game::MSG_WriteData(&msg, statbuffer, std::min(0x9400 - (i * 1240), 1240));
				}
				else console::debug("we failed to get a stat buffer");

				const auto target = (*game::connect_state)->address;
				console::debug("sending stats to %u.%u.%u.%u", target.ip[0], target.ip[1], target.ip[2], target.ip[3]);
				network::send_data(target, std::string(reinterpret_cast<char*>(msg.data), msg.cursize));
			}
		}
		else console::debug("not connected to server?!?");
	}

	bool Custom_Com_DDL_SetDataFromString(game::DDLState* state, game::DDLContext* buffer, const char* value)
	{
		bool result{ false };
		auto ddlType = -1;

		if (state && state->member != 0)
		{
			ddlType = state->member->type;
		}

		auto intValue = 0;
		auto floatValue = 0.f;
		switch (ddlType)
		{
		case 0:
		case 1:
		case 2:
		case 3:
			intValue = _atoi64(value);
			result = game::DDL_SetInt(state, buffer, intValue);
			break;
		case 4:
			intValue = _atoi64(value);
			result = game::DDL_SetUInt64(state, buffer, intValue);
			break;
		case 5:
			floatValue = atof(value);
			result = game::DDL_SetFloat(state, buffer, floatValue);
			break;
		case 6:
			floatValue = atof(value);
			result = game::DDL_SetFixedPoint(state, buffer, floatValue);
			break;
		case 7:
			result = game::DDL_SetString(state, buffer, value);
			break;
		case 9:
			result = game::DDL_SetEnum(state, buffer, value);
			break;
		default:
			result = false;
			break;
		}

		return result;
	}

	void Custom_LiveStorage_PlayerDataSetCmd(const char* ddlPath, int type = 0)
	{
		game::LiveStorage_EnsureMaySetPersistentData(0, "Custom_LiveStorage_PlayerDataSetCmd");
		auto activeStatsSource = game::LiveStorage_GetActiveStatsSource(0);
		game::DDLContext ddlContext{};
		auto result = game::CL_PlayerData_GetDDLBuffer(&ddlContext, 0, type, activeStatsSource);

		if (result)
		{
			auto ddlArgs = utils::string::split(std::string(ddlPath), '/');
			auto ddlArgsLength = ddlArgs.size();

			unsigned int* v11{};
			unsigned int path[16]{};
			char* v12{};
			char* v13{};
			char v14{};
			int i{};
			int v16{};
			auto v10 = 0;

			game::DDLState result{};
			game::DDLState* ddlState{};
			if (ddlArgsLength > 1)
			{
				v11 = path;
				do
				{
					v12 = (char*)ddlArgs[v10].c_str();
					v13 = v12;
					v14 = *v12;

					if ((*v12 - 48) > 9)
					{
						for (i = 0; *v13; i = v16 + 31 * i)
						{
							++v13;
							v16 = v14;
							v14 = *v13;
						}
					}
					else
					{
						i = atoi(v12);
					}
					*v11 = i;
					++v11;
					++v10;
				} while (v10 < ddlArgsLength);
			}

			ddlState = game::DDL_GetRoot(&result, ddlContext.def);
			auto can_navigate_ddl = game::DDL_MoveToPathByHash(&result, &result, ddlArgsLength - 1, path);

			if (can_navigate_ddl)
			{
				auto state_is_leaf = game::DDL_StateIsLeaf(&result);

				if (state_is_leaf)
				{
					auto& v9 = ddlArgs[ddlArgsLength - 1];

					auto set_data_from_string = Custom_Com_DDL_SetDataFromString(&result, &ddlContext, v9.c_str());

					if (set_data_from_string)
					{
						game::LiveStorage_StatsWriteNeeded(0);
					}
				}
				else
				{
					console::warn("not enough args passed to setplayerdata. %s\n", ddlPath);
				}
			}
			else
			{
				console::warn("cannot navigate ddl. %s\n", ddlPath);
			}
		}
	}

	static void reset_ranked_data()
	{
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/0/equipment/0/h1_fraggrenade_mp");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/1/equipment/0/h2_semtex_mp");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/2/equipment/0/h2_semtex_mp");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/3/equipment/0/h1_fraggrenade_mp");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/4/equipment/0/h1_fraggrenade_mp");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/5/equipment/0/h1_fraggrenade_mp");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/6/equipment/0/h1_fraggrenade_mp");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/7/equipment/0/h1_fraggrenade_mp");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/8/equipment/0/h1_fraggrenade_mp");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/9/equipment/0/h1_fraggrenade_mp");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/10/equipment/0/h1_fraggrenade_mp");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/11/equipment/0/h1_fraggrenade_mp");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/12/equipment/0/h1_fraggrenade_mp");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/13/equipment/0/h1_fraggrenade_mp");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/14/equipment/0/h1_fraggrenade_mp");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/15/equipment/0/h1_fraggrenade_mp");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/16/equipment/0/h1_fraggrenade_mp");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/17/equipment/0/h1_fraggrenade_mp");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/18/equipment/0/h1_fraggrenade_mp");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/19/equipment/0/h1_fraggrenade_mp");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/0/equipment/1/h1_flashgrenade_mp");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/1/equipment/1/h1_flashgrenade_mp");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/2/equipment/1/h1_flashgrenade_mp");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/3/equipment/1/h1_smokegrenade_mp");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/4/equipment/1/h1_flashgrenade_mp");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/5/equipment/1/h1_flashgrenade_mp");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/6/equipment/1/h1_flashgrenade_mp");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/7/equipment/1/h1_flashgrenade_mp");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/8/equipment/1/h1_flashgrenade_mp");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/9/equipment/1/h1_flashgrenade_mp");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/10/equipment/1/h1_flashgrenade_mp");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/11/equipment/1/h1_flashgrenade_mp");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/12/equipment/1/h1_flashgrenade_mp");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/13/equipment/1/h1_flashgrenade_mp");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/14/equipment/1/h1_flashgrenade_mp");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/15/equipment/1/h1_flashgrenade_mp");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/16/equipment/1/h1_flashgrenade_mp");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/17/equipment/1/h1_flashgrenade_mp");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/18/equipment/1/h1_flashgrenade_mp");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/19/equipment/1/h1_flashgrenade_mp");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/0/weaponSetups/0/reticle/none");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/0/weaponSetups/1/reticle/none");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/1/weaponSetups/0/reticle/none");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/1/weaponSetups/1/reticle/none");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/2/weaponSetups/0/reticle/none");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/2/weaponSetups/1/reticle/none");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/3/weaponSetups/0/reticle/none");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/3/weaponSetups/1/reticle/none");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/4/weaponSetups/0/reticle/none");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/4/weaponSetups/1/reticle/none");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/5/weaponSetups/0/reticle/none");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/5/weaponSetups/1/reticle/none");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/6/weaponSetups/0/reticle/none");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/6/weaponSetups/1/reticle/none");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/7/weaponSetups/0/reticle/none");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/7/weaponSetups/1/reticle/none");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/8/weaponSetups/0/reticle/none");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/8/weaponSetups/1/reticle/none");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/9/weaponSetups/0/reticle/none");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/9/weaponSetups/1/reticle/none");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/10/weaponSetups/0/reticle/none");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/10/weaponSetups/1/reticle/none");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/11/weaponSetups/0/reticle/none");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/11/weaponSetups/1/reticle/none");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/12/weaponSetups/0/reticle/none");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/12/weaponSetups/1/reticle/none");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/13/weaponSetups/0/reticle/none");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/13/weaponSetups/1/reticle/none");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/14/weaponSetups/0/reticle/none");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/14/weaponSetups/1/reticle/none");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/15/weaponSetups/0/reticle/none");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/15/weaponSetups/1/reticle/none");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/16/weaponSetups/0/reticle/none");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/16/weaponSetups/1/reticle/none");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/17/weaponSetups/0/reticle/none");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/17/weaponSetups/1/reticle/none");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/18/weaponSetups/0/reticle/none");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/18/weaponSetups/1/reticle/none");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/19/weaponSetups/0/reticle/none");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/19/weaponSetups/1/reticle/none");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/0/weaponSetups/0/weapon/h2_m4");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/0/weaponSetups/1/weapon/h2_usp");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/1/weaponSetups/0/weapon/h2_mp5k");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/1/weaponSetups/1/weapon/h2_spas12");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/2/weaponSetups/0/weapon/h2_rpd");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/2/weaponSetups/1/weapon/at4");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/3/weaponSetups/0/weapon/h2_cheytac");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/3/weaponSetups/1/weapon/h2_pp2000");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/4/weaponSetups/0/weapon/h2_sa80");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/4/weaponSetups/1/weapon/h2_pp2000");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/5/weaponSetups/0/weapon/h2_m4");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/5/weaponSetups/1/weapon/h2_usp");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/6/weaponSetups/0/weapon/h2_mp5k");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/6/weaponSetups/1/weapon/h2_spas12");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/7/weaponSetups/0/weapon/h2_rpd");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/7/weaponSetups/1/weapon/at4");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/8/weaponSetups/0/weapon/h2_cheytac");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/8/weaponSetups/1/weapon/h2_pp2000");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/9/weaponSetups/0/weapon/h2_sa80");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/9/weaponSetups/1/weapon/h2_pp2000");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/10/weaponSetups/0/weapon/h2_m4");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/10/weaponSetups/1/weapon/h2_usp");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/11/weaponSetups/0/weapon/h2_mp5k");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/11/weaponSetups/1/weapon/h2_spas12");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/12/weaponSetups/0/weapon/h2_rpd");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/12/weaponSetups/1/weapon/at4");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/13/weaponSetups/0/weapon/h2_cheytac");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/13/weaponSetups/1/weapon/h2_pp2000");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/14/weaponSetups/0/weapon/h2_sa80");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/14/weaponSetups/1/weapon/h2_pp2000");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/15/weaponSetups/0/weapon/h2_m4");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/15/weaponSetups/1/weapon/h2_usp");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/16/weaponSetups/0/weapon/h2_mp5k");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/16/weaponSetups/1/weapon/h2_spas12");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/17/weaponSetups/0/weapon/h2_rpd");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/17/weaponSetups/1/weapon/at4");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/18/weaponSetups/0/weapon/h2_cheytac");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/18/weaponSetups/1/weapon/h2_pp2000");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/19/weaponSetups/0/weapon/h2_sa80");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/19/weaponSetups/1/weapon/h2_pp2000");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/0/weaponSetups/0/kit/attachKit/0");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/1/weaponSetups/0/kit/attachKit/0");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/2/weaponSetups/0/kit/attachKit/0");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/3/weaponSetups/0/kit/attachKit/0");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/4/weaponSetups/0/kit/attachKit/0");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/5/weaponSetups/0/kit/attachKit/0");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/6/weaponSetups/0/kit/attachKit/0");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/7/weaponSetups/0/kit/attachKit/0");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/8/weaponSetups/0/kit/attachKit/0");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/9/weaponSetups/0/kit/attachKit/0");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/10/weaponSetups/0/kit/attachKit/0");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/11/weaponSetups/0/kit/attachKit/0");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/12/weaponSetups/0/kit/attachKit/0");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/13/weaponSetups/0/kit/attachKit/0");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/14/weaponSetups/0/kit/attachKit/0");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/15/weaponSetups/0/kit/attachKit/0");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/16/weaponSetups/0/kit/attachKit/0");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/17/weaponSetups/0/kit/attachKit/0");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/18/weaponSetups/0/kit/attachKit/0");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/19/weaponSetups/0/kit/attachKit/0");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/0/weaponSetups/0/kit/furnitureKit/0");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/1/weaponSetups/0/kit/furnitureKit/0");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/2/weaponSetups/0/kit/furnitureKit/0");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/3/weaponSetups/0/kit/furnitureKit/0");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/4/weaponSetups/0/kit/furnitureKit/0");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/5/weaponSetups/0/kit/furnitureKit/0");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/6/weaponSetups/0/kit/furnitureKit/0");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/7/weaponSetups/0/kit/furnitureKit/0");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/8/weaponSetups/0/kit/furnitureKit/0");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/9/weaponSetups/0/kit/furnitureKit/0");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/10/weaponSetups/0/kit/furnitureKit/0");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/11/weaponSetups/0/kit/furnitureKit/0");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/12/weaponSetups/0/kit/furnitureKit/0");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/13/weaponSetups/0/kit/furnitureKit/0");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/14/weaponSetups/0/kit/furnitureKit/0");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/15/weaponSetups/0/kit/furnitureKit/0");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/16/weaponSetups/0/kit/furnitureKit/0");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/17/weaponSetups/0/kit/furnitureKit/0");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/18/weaponSetups/0/kit/furnitureKit/0");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/19/weaponSetups/0/kit/furnitureKit/0");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/0/weaponSetups/1/kit/furnitureKit/0");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/1/weaponSetups/1/kit/furnitureKit/0");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/2/weaponSetups/1/kit/furnitureKit/0");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/3/weaponSetups/1/kit/furnitureKit/0");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/4/weaponSetups/1/kit/furnitureKit/0");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/5/weaponSetups/1/kit/furnitureKit/0");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/6/weaponSetups/1/kit/furnitureKit/0");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/7/weaponSetups/1/kit/furnitureKit/0");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/8/weaponSetups/1/kit/furnitureKit/0");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/9/weaponSetups/1/kit/furnitureKit/0");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/10/weaponSetups/1/kit/furnitureKit/0");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/11/weaponSetups/1/kit/furnitureKit/0");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/12/weaponSetups/1/kit/furnitureKit/0");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/13/weaponSetups/1/kit/furnitureKit/0");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/14/weaponSetups/1/kit/furnitureKit/0");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/15/weaponSetups/1/kit/furnitureKit/0");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/16/weaponSetups/1/kit/furnitureKit/0");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/17/weaponSetups/1/kit/furnitureKit/0");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/18/weaponSetups/1/kit/furnitureKit/0");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/19/weaponSetups/1/kit/furnitureKit/0");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/0/weaponSetups/0/camo/none");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/0/weaponSetups/1/camo/none");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/1/weaponSetups/0/camo/none");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/1/weaponSetups/1/camo/none");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/2/weaponSetups/0/camo/none");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/2/weaponSetups/1/camo/none");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/3/weaponSetups/0/camo/none");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/3/weaponSetups/1/camo/none");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/4/weaponSetups/0/camo/none");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/4/weaponSetups/1/camo/none");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/5/weaponSetups/0/camo/none");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/5/weaponSetups/1/camo/none");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/6/weaponSetups/0/camo/none");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/6/weaponSetups/1/camo/none");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/7/weaponSetups/0/camo/none");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/7/weaponSetups/1/camo/none");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/8/weaponSetups/0/camo/none");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/8/weaponSetups/1/camo/none");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/9/weaponSetups/0/camo/none");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/9/weaponSetups/1/camo/none");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/10/weaponSetups/0/camo/none");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/10/weaponSetups/1/camo/none");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/11/weaponSetups/0/camo/none");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/11/weaponSetups/1/camo/none");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/12/weaponSetups/0/camo/none");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/12/weaponSetups/1/camo/none");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/13/weaponSetups/0/camo/none");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/13/weaponSetups/1/camo/none");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/14/weaponSetups/0/camo/none");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/14/weaponSetups/1/camo/none");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/15/weaponSetups/0/camo/none");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/15/weaponSetups/1/camo/none");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/16/weaponSetups/0/camo/none");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/16/weaponSetups/1/camo/none");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/17/weaponSetups/0/camo/none");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/17/weaponSetups/1/camo/none");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/18/weaponSetups/0/camo/none");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/18/weaponSetups/1/camo/none");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/19/weaponSetups/0/camo/none");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/19/weaponSetups/1/camo/none");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/0/perkSlots/0/specialty_fastreload");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/0/perkSlots/1/specialty_bulletdamage");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/0/perkSlots/2/specialty_bulletaccuracy");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/1/perkSlots/0/specialty_longersprint");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/1/perkSlots/1/specialty_lightweight");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/1/perkSlots/2/specialty_extendedmelee");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/2/perkSlots/0/specialty_fastreload");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/2/perkSlots/1/specialty_bulletdamage");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/2/perkSlots/2/specialty_extendedmelee");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/3/perkSlots/0/specialty_fastreload");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/3/perkSlots/1/specialty_bulletdamage");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/3/perkSlots/2/specialty_bulletaccuracy");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/4/perkSlots/0/specialty_longersprint");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/4/perkSlots/1/specialty_lightweight");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/4/perkSlots/2/specialty_extendedmelee");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/5/perkSlots/0/specialty_null");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/5/perkSlots/1/specialty_null");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/5/perkSlots/2/specialty_null");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/6/perkSlots/0/specialty_null");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/6/perkSlots/1/specialty_null");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/6/perkSlots/2/specialty_null");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/7/perkSlots/0/specialty_null");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/7/perkSlots/1/specialty_null");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/7/perkSlots/2/specialty_null");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/8/perkSlots/0/specialty_null");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/8/perkSlots/1/specialty_null");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/8/perkSlots/2/specialty_null");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/9/perkSlots/0/specialty_null");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/9/perkSlots/1/specialty_null");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/9/perkSlots/2/specialty_null");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/10/perkSlots/0/specialty_null");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/10/perkSlots/1/specialty_null");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/10/perkSlots/2/specialty_null");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/11/perkSlots/0/specialty_null");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/11/perkSlots/1/specialty_null");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/11/perkSlots/2/specialty_null");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/12/perkSlots/0/specialty_null");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/12/perkSlots/1/specialty_null");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/12/perkSlots/2/specialty_null");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/13/perkSlots/0/specialty_null");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/13/perkSlots/1/specialty_null");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/13/perkSlots/2/specialty_null");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/14/perkSlots/0/specialty_null");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/14/perkSlots/1/specialty_null");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/14/perkSlots/2/specialty_null");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/15/perkSlots/0/specialty_null");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/15/perkSlots/1/specialty_null");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/15/perkSlots/2/specialty_null");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/16/perkSlots/0/specialty_null");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/16/perkSlots/1/specialty_null");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/16/perkSlots/2/specialty_null");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/17/perkSlots/0/specialty_null");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/17/perkSlots/1/specialty_null");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/17/perkSlots/2/specialty_null");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/18/perkSlots/0/specialty_null");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/18/perkSlots/1/specialty_null");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/18/perkSlots/2/specialty_null");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/19/perkSlots/0/specialty_null");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/19/perkSlots/1/specialty_null");
		Custom_LiveStorage_PlayerDataSetCmd("rankedMatchData/customClasses/19/perkSlots/2/specialty_null");
	}

	static void reset_private_data()
	{
		Custom_LiveStorage_PlayerDataSetCmd("privateMatchData/privateMatchCustomClasses/0/weaponSetups/0/weapon/h2_m4", 1);
		Custom_LiveStorage_PlayerDataSetCmd("privateMatchData/privateMatchCustomClasses/0/weaponSetups/1/weapon/h2_usp", 1);
		Custom_LiveStorage_PlayerDataSetCmd("privateMatchData/privateMatchCustomClasses/1/weaponSetups/0/weapon/h2_mp5k", 1);
		Custom_LiveStorage_PlayerDataSetCmd("privateMatchData/privateMatchCustomClasses/1/weaponSetups/1/weapon/h2_spas12", 1);
		Custom_LiveStorage_PlayerDataSetCmd("privateMatchData/privateMatchCustomClasses/2/weaponSetups/0/weapon/h2_rpd", 1);
		Custom_LiveStorage_PlayerDataSetCmd("privateMatchData/privateMatchCustomClasses/2/weaponSetups/1/weapon/at4", 1);
		Custom_LiveStorage_PlayerDataSetCmd("privateMatchData/privateMatchCustomClasses/3/weaponSetups/0/weapon/h2_cheytac", 1);
		Custom_LiveStorage_PlayerDataSetCmd("privateMatchData/privateMatchCustomClasses/3/weaponSetups/1/weapon/h2_pp2000", 1);
		Custom_LiveStorage_PlayerDataSetCmd("privateMatchData/privateMatchCustomClasses/4/weaponSetups/0/weapon/h2_sa80", 1);
		Custom_LiveStorage_PlayerDataSetCmd("privateMatchData/privateMatchCustomClasses/4/weaponSetups/1/weapon/h2_pp2000", 1);
		Custom_LiveStorage_PlayerDataSetCmd("privateMatchData/privateMatchCustomClasses/5/weaponSetups/0/weapon/h2_m4", 1);
		Custom_LiveStorage_PlayerDataSetCmd("privateMatchData/privateMatchCustomClasses/5/weaponSetups/1/weapon/h2_usp", 1);
		Custom_LiveStorage_PlayerDataSetCmd("privateMatchData/privateMatchCustomClasses/6/weaponSetups/0/weapon/h2_mp5k", 1);
		Custom_LiveStorage_PlayerDataSetCmd("privateMatchData/privateMatchCustomClasses/6/weaponSetups/1/weapon/h2_spas12", 1);
		Custom_LiveStorage_PlayerDataSetCmd("privateMatchData/privateMatchCustomClasses/7/weaponSetups/0/weapon/h2_rpd", 1);
		Custom_LiveStorage_PlayerDataSetCmd("privateMatchData/privateMatchCustomClasses/7/weaponSetups/1/weapon/at4", 1);
		Custom_LiveStorage_PlayerDataSetCmd("privateMatchData/privateMatchCustomClasses/8/weaponSetups/0/weapon/h2_cheytac", 1);
		Custom_LiveStorage_PlayerDataSetCmd("privateMatchData/privateMatchCustomClasses/8/weaponSetups/1/weapon/h2_pp2000", 1);
		Custom_LiveStorage_PlayerDataSetCmd("privateMatchData/privateMatchCustomClasses/9/weaponSetups/0/weapon/h2_sa80", 1);
		Custom_LiveStorage_PlayerDataSetCmd("privateMatchData/privateMatchCustomClasses/9/weaponSetups/1/weapon/h2_pp2000", 1);
		Custom_LiveStorage_PlayerDataSetCmd("privateMatchData/privateMatchCustomClasses/0/weaponSetups/0/kit/attachKit/0", 1);
		Custom_LiveStorage_PlayerDataSetCmd("privateMatchData/privateMatchCustomClasses/1/weaponSetups/0/kit/attachKit/0", 1);
		Custom_LiveStorage_PlayerDataSetCmd("privateMatchData/privateMatchCustomClasses/2/weaponSetups/0/kit/attachKit/0", 1);
		Custom_LiveStorage_PlayerDataSetCmd("privateMatchData/privateMatchCustomClasses/3/weaponSetups/0/kit/attachKit/0", 1);
		Custom_LiveStorage_PlayerDataSetCmd("privateMatchData/privateMatchCustomClasses/4/weaponSetups/0/kit/attachKit/0", 1);
		Custom_LiveStorage_PlayerDataSetCmd("privateMatchData/privateMatchCustomClasses/5/weaponSetups/0/kit/attachKit/0", 1);
		Custom_LiveStorage_PlayerDataSetCmd("privateMatchData/privateMatchCustomClasses/6/weaponSetups/0/kit/attachKit/0", 1);
		Custom_LiveStorage_PlayerDataSetCmd("privateMatchData/privateMatchCustomClasses/7/weaponSetups/0/kit/attachKit/0", 1);
		Custom_LiveStorage_PlayerDataSetCmd("privateMatchData/privateMatchCustomClasses/8/weaponSetups/0/kit/attachKit/0", 1);
		Custom_LiveStorage_PlayerDataSetCmd("privateMatchData/privateMatchCustomClasses/9/weaponSetups/0/kit/attachKit/0", 1);
		Custom_LiveStorage_PlayerDataSetCmd("privateMatchData/privateMatchCustomClasses/0/weaponSetups/1/kit/attachKit/0", 1);
		Custom_LiveStorage_PlayerDataSetCmd("privateMatchData/privateMatchCustomClasses/1/weaponSetups/1/kit/attachKit/0", 1);
		Custom_LiveStorage_PlayerDataSetCmd("privateMatchData/privateMatchCustomClasses/2/weaponSetups/1/kit/attachKit/0", 1);
		Custom_LiveStorage_PlayerDataSetCmd("privateMatchData/privateMatchCustomClasses/3/weaponSetups/1/kit/attachKit/0", 1);
		Custom_LiveStorage_PlayerDataSetCmd("privateMatchData/privateMatchCustomClasses/4/weaponSetups/1/kit/attachKit/0", 1);
		Custom_LiveStorage_PlayerDataSetCmd("privateMatchData/privateMatchCustomClasses/5/weaponSetups/1/kit/attachKit/0", 1);
		Custom_LiveStorage_PlayerDataSetCmd("privateMatchData/privateMatchCustomClasses/6/weaponSetups/1/kit/attachKit/0", 1);
		Custom_LiveStorage_PlayerDataSetCmd("privateMatchData/privateMatchCustomClasses/7/weaponSetups/1/kit/attachKit/0", 1);
		Custom_LiveStorage_PlayerDataSetCmd("privateMatchData/privateMatchCustomClasses/8/weaponSetups/1/kit/attachKit/0", 1);
		Custom_LiveStorage_PlayerDataSetCmd("privateMatchData/privateMatchCustomClasses/9/weaponSetups/1/kit/attachKit/0", 1);
		Custom_LiveStorage_PlayerDataSetCmd("privateMatchData/privateMatchCustomClasses/0/weaponSetups/0/kit/furnitureKit/0", 1);
		Custom_LiveStorage_PlayerDataSetCmd("privateMatchData/privateMatchCustomClasses/1/weaponSetups/0/kit/furnitureKit/0", 1);
		Custom_LiveStorage_PlayerDataSetCmd("privateMatchData/privateMatchCustomClasses/2/weaponSetups/0/kit/furnitureKit/0", 1);
		Custom_LiveStorage_PlayerDataSetCmd("privateMatchData/privateMatchCustomClasses/3/weaponSetups/0/kit/furnitureKit/0", 1);
		Custom_LiveStorage_PlayerDataSetCmd("privateMatchData/privateMatchCustomClasses/4/weaponSetups/0/kit/furnitureKit/0", 1);
		Custom_LiveStorage_PlayerDataSetCmd("privateMatchData/privateMatchCustomClasses/5/weaponSetups/0/kit/furnitureKit/0", 1);
		Custom_LiveStorage_PlayerDataSetCmd("privateMatchData/privateMatchCustomClasses/6/weaponSetups/0/kit/furnitureKit/0", 1);
		Custom_LiveStorage_PlayerDataSetCmd("privateMatchData/privateMatchCustomClasses/7/weaponSetups/0/kit/furnitureKit/0", 1);
		Custom_LiveStorage_PlayerDataSetCmd("privateMatchData/privateMatchCustomClasses/8/weaponSetups/0/kit/furnitureKit/0", 1);
		Custom_LiveStorage_PlayerDataSetCmd("privateMatchData/privateMatchCustomClasses/9/weaponSetups/0/kit/furnitureKit/0", 1);
		Custom_LiveStorage_PlayerDataSetCmd("privateMatchData/privateMatchCustomClasses/0/weaponSetups/1/kit/furnitureKit/0", 1);
		Custom_LiveStorage_PlayerDataSetCmd("privateMatchData/privateMatchCustomClasses/1/weaponSetups/1/kit/furnitureKit/0", 1);
		Custom_LiveStorage_PlayerDataSetCmd("privateMatchData/privateMatchCustomClasses/2/weaponSetups/1/kit/furnitureKit/0", 1);
		Custom_LiveStorage_PlayerDataSetCmd("privateMatchData/privateMatchCustomClasses/3/weaponSetups/1/kit/furnitureKit/0", 1);
		Custom_LiveStorage_PlayerDataSetCmd("privateMatchData/privateMatchCustomClasses/4/weaponSetups/1/kit/furnitureKit/0", 1);
		Custom_LiveStorage_PlayerDataSetCmd("privateMatchData/privateMatchCustomClasses/5/weaponSetups/1/kit/furnitureKit/0", 1);
		Custom_LiveStorage_PlayerDataSetCmd("privateMatchData/privateMatchCustomClasses/6/weaponSetups/1/kit/furnitureKit/0", 1);
		Custom_LiveStorage_PlayerDataSetCmd("privateMatchData/privateMatchCustomClasses/7/weaponSetups/1/kit/furnitureKit/0", 1);
		Custom_LiveStorage_PlayerDataSetCmd("privateMatchData/privateMatchCustomClasses/8/weaponSetups/1/kit/furnitureKit/0", 1);
		Custom_LiveStorage_PlayerDataSetCmd("privateMatchData/privateMatchCustomClasses/9/weaponSetups/1/kit/furnitureKit/0", 1);
		Custom_LiveStorage_PlayerDataSetCmd("privateMatchData/privateMatchCustomClasses/0/equipment/0/h1_fraggrenade_mp", 1);
		Custom_LiveStorage_PlayerDataSetCmd("privateMatchData/privateMatchCustomClasses/1/equipment/0/h2_semtex_mp", 1);
		Custom_LiveStorage_PlayerDataSetCmd("privateMatchData/privateMatchCustomClasses/2/equipment/0/h2_semtex_mp", 1);
		Custom_LiveStorage_PlayerDataSetCmd("privateMatchData/privateMatchCustomClasses/3/equipment/0/h1_fraggrenade_mp", 1);
		Custom_LiveStorage_PlayerDataSetCmd("privateMatchData/privateMatchCustomClasses/4/equipment/0/h1_fraggrenade_mp", 1);
		Custom_LiveStorage_PlayerDataSetCmd("privateMatchData/privateMatchCustomClasses/5/equipment/0/h1_fraggrenade_mp", 1);
		Custom_LiveStorage_PlayerDataSetCmd("privateMatchData/privateMatchCustomClasses/6/equipment/0/h1_fraggrenade_mp", 1);
		Custom_LiveStorage_PlayerDataSetCmd("privateMatchData/privateMatchCustomClasses/7/equipment/0/h1_fraggrenade_mp", 1);
		Custom_LiveStorage_PlayerDataSetCmd("privateMatchData/privateMatchCustomClasses/8/equipment/0/h1_fraggrenade_mp", 1);
		Custom_LiveStorage_PlayerDataSetCmd("privateMatchData/privateMatchCustomClasses/9/equipment/0/h1_fraggrenade_mp", 1);
		Custom_LiveStorage_PlayerDataSetCmd("privateMatchData/privateMatchCustomClasses/0/equipment/1/h1_flashgrenade_mp", 1);
		Custom_LiveStorage_PlayerDataSetCmd("privateMatchData/privateMatchCustomClasses/1/equipment/1/h1_flashgrenade_mp", 1);
		Custom_LiveStorage_PlayerDataSetCmd("privateMatchData/privateMatchCustomClasses/2/equipment/1/h1_flashgrenade_mp", 1);
		Custom_LiveStorage_PlayerDataSetCmd("privateMatchData/privateMatchCustomClasses/3/equipment/1/h1_smokegrenade_mp", 1);
		Custom_LiveStorage_PlayerDataSetCmd("privateMatchData/privateMatchCustomClasses/4/equipment/1/h1_flashgrenade_mp", 1);
		Custom_LiveStorage_PlayerDataSetCmd("privateMatchData/privateMatchCustomClasses/5/equipment/1/h1_flashgrenade_mp", 1);
		Custom_LiveStorage_PlayerDataSetCmd("privateMatchData/privateMatchCustomClasses/6/equipment/1/h1_flashgrenade_mp", 1);
		Custom_LiveStorage_PlayerDataSetCmd("privateMatchData/privateMatchCustomClasses/7/equipment/1/h1_flashgrenade_mp", 1);
		Custom_LiveStorage_PlayerDataSetCmd("privateMatchData/privateMatchCustomClasses/8/equipment/1/h1_flashgrenade_mp", 1);
		Custom_LiveStorage_PlayerDataSetCmd("privateMatchData/privateMatchCustomClasses/9/equipment/1/h1_flashgrenade_mp", 1);
		Custom_LiveStorage_PlayerDataSetCmd("privateMatchData/privateMatchCustomClasses/0/perkSlots/0/specialty_fastreload", 1);
		Custom_LiveStorage_PlayerDataSetCmd("privateMatchData/privateMatchCustomClasses/0/perkSlots/1/specialty_bulletdamage", 1);
		Custom_LiveStorage_PlayerDataSetCmd("privateMatchData/privateMatchCustomClasses/0/perkSlots/2/specialty_bulletaccuracy", 1);
		Custom_LiveStorage_PlayerDataSetCmd("privateMatchData/privateMatchCustomClasses/1/perkSlots/0/specialty_longersprint", 1);
		Custom_LiveStorage_PlayerDataSetCmd("privateMatchData/privateMatchCustomClasses/1/perkSlots/1/specialty_lightweight", 1);
		Custom_LiveStorage_PlayerDataSetCmd("privateMatchData/privateMatchCustomClasses/1/perkSlots/2/specialty_extendedmelee", 1);
		Custom_LiveStorage_PlayerDataSetCmd("privateMatchData/privateMatchCustomClasses/2/perkSlots/0/specialty_fastreload", 1);
		Custom_LiveStorage_PlayerDataSetCmd("privateMatchData/privateMatchCustomClasses/2/perkSlots/1/specialty_bulletdamage", 1);
		Custom_LiveStorage_PlayerDataSetCmd("privateMatchData/privateMatchCustomClasses/2/perkSlots/2/specialty_extendedmelee", 1);
		Custom_LiveStorage_PlayerDataSetCmd("privateMatchData/privateMatchCustomClasses/3/perkSlots/0/specialty_fastreload", 1);
		Custom_LiveStorage_PlayerDataSetCmd("privateMatchData/privateMatchCustomClasses/3/perkSlots/1/specialty_bulletdamage", 1);
		Custom_LiveStorage_PlayerDataSetCmd("privateMatchData/privateMatchCustomClasses/3/perkSlots/2/specialty_bulletaccuracy", 1);
		Custom_LiveStorage_PlayerDataSetCmd("privateMatchData/privateMatchCustomClasses/4/perkSlots/0/specialty_longersprint", 1);
		Custom_LiveStorage_PlayerDataSetCmd("privateMatchData/privateMatchCustomClasses/4/perkSlots/1/specialty_lightweight", 1);
		Custom_LiveStorage_PlayerDataSetCmd("privateMatchData/privateMatchCustomClasses/4/perkSlots/2/specialty_extendedmelee", 1);
		Custom_LiveStorage_PlayerDataSetCmd("privateMatchData/privateMatchCustomClasses/5/perkSlots/0/specialty_null", 1);
		Custom_LiveStorage_PlayerDataSetCmd("privateMatchData/privateMatchCustomClasses/5/perkSlots/1/specialty_null", 1);
		Custom_LiveStorage_PlayerDataSetCmd("privateMatchData/privateMatchCustomClasses/5/perkSlots/2/specialty_null", 1);
		Custom_LiveStorage_PlayerDataSetCmd("privateMatchData/privateMatchCustomClasses/6/perkSlots/0/specialty_null", 1);
		Custom_LiveStorage_PlayerDataSetCmd("privateMatchData/privateMatchCustomClasses/6/perkSlots/1/specialty_null", 1);
		Custom_LiveStorage_PlayerDataSetCmd("privateMatchData/privateMatchCustomClasses/6/perkSlots/2/specialty_null", 1);
		Custom_LiveStorage_PlayerDataSetCmd("privateMatchData/privateMatchCustomClasses/7/perkSlots/0/specialty_null", 1);
		Custom_LiveStorage_PlayerDataSetCmd("privateMatchData/privateMatchCustomClasses/7/perkSlots/1/specialty_null", 1);
		Custom_LiveStorage_PlayerDataSetCmd("privateMatchData/privateMatchCustomClasses/7/perkSlots/2/specialty_null", 1);
		Custom_LiveStorage_PlayerDataSetCmd("privateMatchData/privateMatchCustomClasses/8/perkSlots/0/specialty_null", 1);
		Custom_LiveStorage_PlayerDataSetCmd("privateMatchData/privateMatchCustomClasses/8/perkSlots/1/specialty_null", 1);
		Custom_LiveStorage_PlayerDataSetCmd("privateMatchData/privateMatchCustomClasses/8/perkSlots/2/specialty_null", 1);
		Custom_LiveStorage_PlayerDataSetCmd("privateMatchData/privateMatchCustomClasses/9/perkSlots/0/specialty_null", 1);
		Custom_LiveStorage_PlayerDataSetCmd("privateMatchData/privateMatchCustomClasses/9/perkSlots/1/specialty_null", 1);
		Custom_LiveStorage_PlayerDataSetCmd("privateMatchData/privateMatchCustomClasses/9/perkSlots/2/specialty_null", 1);
		Custom_LiveStorage_PlayerDataSetCmd("privateMatchData/privateMatchCustomClasses/0/weaponSetups/0/camo/none", 1);
		Custom_LiveStorage_PlayerDataSetCmd("privateMatchData/privateMatchCustomClasses/0/weaponSetups/1/camo/none", 1);
		Custom_LiveStorage_PlayerDataSetCmd("privateMatchData/privateMatchCustomClasses/1/weaponSetups/0/camo/none", 1);
		Custom_LiveStorage_PlayerDataSetCmd("privateMatchData/privateMatchCustomClasses/1/weaponSetups/1/camo/none", 1);
		Custom_LiveStorage_PlayerDataSetCmd("privateMatchData/privateMatchCustomClasses/2/weaponSetups/0/camo/none", 1);
		Custom_LiveStorage_PlayerDataSetCmd("privateMatchData/privateMatchCustomClasses/2/weaponSetups/1/camo/none", 1);
		Custom_LiveStorage_PlayerDataSetCmd("privateMatchData/privateMatchCustomClasses/3/weaponSetups/0/camo/none", 1);
		Custom_LiveStorage_PlayerDataSetCmd("privateMatchData/privateMatchCustomClasses/3/weaponSetups/1/camo/none", 1);
		Custom_LiveStorage_PlayerDataSetCmd("privateMatchData/privateMatchCustomClasses/4/weaponSetups/0/camo/none", 1);
		Custom_LiveStorage_PlayerDataSetCmd("privateMatchData/privateMatchCustomClasses/4/weaponSetups/1/camo/none", 1);
		Custom_LiveStorage_PlayerDataSetCmd("privateMatchData/privateMatchCustomClasses/5/weaponSetups/0/camo/none", 1);
		Custom_LiveStorage_PlayerDataSetCmd("privateMatchData/privateMatchCustomClasses/5/weaponSetups/1/camo/none", 1);
		Custom_LiveStorage_PlayerDataSetCmd("privateMatchData/privateMatchCustomClasses/6/weaponSetups/0/camo/none", 1);
		Custom_LiveStorage_PlayerDataSetCmd("privateMatchData/privateMatchCustomClasses/6/weaponSetups/1/camo/none", 1);
		Custom_LiveStorage_PlayerDataSetCmd("privateMatchData/privateMatchCustomClasses/7/weaponSetups/0/camo/none", 1);
		Custom_LiveStorage_PlayerDataSetCmd("privateMatchData/privateMatchCustomClasses/7/weaponSetups/1/camo/none", 1);
		Custom_LiveStorage_PlayerDataSetCmd("privateMatchData/privateMatchCustomClasses/8/weaponSetups/0/camo/none", 1);
		Custom_LiveStorage_PlayerDataSetCmd("privateMatchData/privateMatchCustomClasses/8/weaponSetups/1/camo/none", 1);
		Custom_LiveStorage_PlayerDataSetCmd("privateMatchData/privateMatchCustomClasses/9/weaponSetups/0/camo/none", 1);
		Custom_LiveStorage_PlayerDataSetCmd("privateMatchData/privateMatchCustomClasses/9/weaponSetups/1/camo/none", 1);
		Custom_LiveStorage_PlayerDataSetCmd("privateMatchData/privateMatchCustomClasses/0/weaponSetups/0/reticle/none", 1);
		Custom_LiveStorage_PlayerDataSetCmd("privateMatchData/privateMatchCustomClasses/0/weaponSetups/1/reticle/none", 1);
		Custom_LiveStorage_PlayerDataSetCmd("privateMatchData/privateMatchCustomClasses/1/weaponSetups/0/reticle/none", 1);
		Custom_LiveStorage_PlayerDataSetCmd("privateMatchData/privateMatchCustomClasses/1/weaponSetups/1/reticle/none", 1);
		Custom_LiveStorage_PlayerDataSetCmd("privateMatchData/privateMatchCustomClasses/2/weaponSetups/0/reticle/none", 1);
		Custom_LiveStorage_PlayerDataSetCmd("privateMatchData/privateMatchCustomClasses/2/weaponSetups/1/reticle/none", 1);
		Custom_LiveStorage_PlayerDataSetCmd("privateMatchData/privateMatchCustomClasses/3/weaponSetups/0/reticle/none", 1);
		Custom_LiveStorage_PlayerDataSetCmd("privateMatchData/privateMatchCustomClasses/3/weaponSetups/1/reticle/none", 1);
		Custom_LiveStorage_PlayerDataSetCmd("privateMatchData/privateMatchCustomClasses/4/weaponSetups/0/reticle/none", 1);
		Custom_LiveStorage_PlayerDataSetCmd("privateMatchData/privateMatchCustomClasses/4/weaponSetups/1/reticle/none", 1);
		Custom_LiveStorage_PlayerDataSetCmd("privateMatchData/privateMatchCustomClasses/5/weaponSetups/0/reticle/none", 1);
		Custom_LiveStorage_PlayerDataSetCmd("privateMatchData/privateMatchCustomClasses/5/weaponSetups/1/reticle/none", 1);
		Custom_LiveStorage_PlayerDataSetCmd("privateMatchData/privateMatchCustomClasses/6/weaponSetups/0/reticle/none", 1);
		Custom_LiveStorage_PlayerDataSetCmd("privateMatchData/privateMatchCustomClasses/6/weaponSetups/1/reticle/none", 1);
		Custom_LiveStorage_PlayerDataSetCmd("privateMatchData/privateMatchCustomClasses/7/weaponSetups/0/reticle/none", 1);
		Custom_LiveStorage_PlayerDataSetCmd("privateMatchData/privateMatchCustomClasses/7/weaponSetups/1/reticle/none", 1);
		Custom_LiveStorage_PlayerDataSetCmd("privateMatchData/privateMatchCustomClasses/8/weaponSetups/0/reticle/none", 1);
		Custom_LiveStorage_PlayerDataSetCmd("privateMatchData/privateMatchCustomClasses/8/weaponSetups/1/reticle/none", 1);
		Custom_LiveStorage_PlayerDataSetCmd("privateMatchData/privateMatchCustomClasses/9/weaponSetups/0/reticle/none", 1);
		Custom_LiveStorage_PlayerDataSetCmd("privateMatchData/privateMatchCustomClasses/9/weaponSetups/1/reticle/none", 1);
	}

	utils::hook::detour DDL_GetType_Detour;
	int DDL_GetType_Stub(game::DDLState* state)
	{
		if (state && state->member != 0)
		{
			return state->member->type;
		}

		return -1;
	}

	class component final : public component_interface
	{
	public:
		void post_unpack() override
		{
			utils::hook::jump(0x19E6E0_b, is_item_unlocked, true);
			utils::hook::set<byte>(0x5503A6_b, 4);

			DDL_GetType_Detour.create(0x793A30_b, DDL_GetType_Stub);


			utils::hook::nop(0x704DD_b, 5); 	// disable setRankedStat
			utils::hook::nop(0x704F7_b, 5);		// disable setPrivateStat


			if (game::environment::is_dedi())
			{
				utils::hook::jump(0x19E070_b, is_item_unlocked, true);
				utils::hook::jump(0x19D390_b, is_item_unlocked, true);
				utils::hook::jump(0x19D140_b, is_item_unlocked, true);
			}
			else
			{
				is_item_unlocked_hook.create(0x19E070_b, is_item_unlocked_stub);
				is_item_unlocked_hook2.create(0x19D140_b, is_item_unlocked_stub2);

				dvars::register_bool("cg_xpbar", true, game::DVAR_FLAG_SAVED, "Show experience bar in ranked matches");

			#ifdef DEBUG
				cg_unlock_all_items = dvars::register_bool("cg_unlockall_items", false, game::DVAR_FLAG_SAVED,
					"Whether items should be locked based on the player's stats or always unlocked.");
				dvars::register_bool("cg_unlockall_classes", false, game::DVAR_FLAG_SAVED,
					"Whether classes should be locked based on the player's stats or always unlocked.");
				cg_unlock_all_loot = dvars::register_bool("cg_unlockall_loot", false, game::DVAR_FLAG_SAVED,
					"Whether loot should be locked based on the player's stats or always unlocked.");
			#endif
			}
		
		
			command::add("init_stats", [](const command::params& params)
			{
				if (params.size() < 2)
				{
#ifdef DEBUG
					console::warn("not enough arguments <init_stats> <value>");
#endif // DEBUG
					return;
				}

				auto param_1 = atoi(params.get(1));

				auto type = std::min(1, param_1);

				if (type == 0) // setRankedStat
				{
					reset_ranked_data();
				}
				else if (type == 1) // setPrivateStat
				{
					reset_private_data();
				}
			});
		}
	};
}

REGISTER_COMPONENT(stats::component)