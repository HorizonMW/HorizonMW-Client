#include <std_include.hpp>
#include "loader/component_loader.hpp"

#include "game/game.hpp"
#include "game/dvars.hpp"
#include "command.hpp"
#include "console.hpp"

#include "utils/string.hpp"
#include "utils/io.hpp"

#include "theatre.hpp"

namespace theatre
{
	class component final : public component_interface
	{
	public:
		void post_unpack() override
		{

#ifdef DEBUG
			dvars::register_bool("cl_autoRecord", true, game::DVAR_FLAG_SAVED, "Automatically record games");
			dvars::register_int("cl_demosKeep", 30, 1, 999, game::DVAR_FLAG_SAVED, "How many demos to keep with autorecord");

			command::add("record", [](const command::params& params)
			{
				auto params_length = params.size();

				if (theatre::recording != nullptr)
				{
					console::info("already recording a demo.\n");
					return;
				}

				if (params_length != 2)
				{
					console::info("usage: record <demoname>\n");
					return;
				}

				if (*game::connectionState != game::CA_ACTIVE)
				{
					console::info("You must be in a level to record.\n");
					return;
				}

				auto demoname = params.get(1);
				auto full_demo_name = utils::string::va("demos/%s.dm_%d", demoname, 13);

				console::info("recording to %s.\n", full_demo_name);
				theatre::recording->name = demoname;
				theatre::recording->filename = full_demo_name;

				game::msg_t msg{};
				static unsigned char bufData[131072];
				game::MSG_Init(&msg, bufData, sizeof(bufData));

				auto* clc = game::clientConnections[0];
				game::MSG_WriteLong(&msg, clc->reliableSequence);
				game::MSG_WriteByte(&msg, 1);
				game::MSG_WriteLong(&msg, clc->serverCommandSequence);
				game::MSG_WriteString(&msg, game::Dvar_FindVar("mapname")->current.string);
				game::MSG_WriteString(&msg, game::Dvar_FindVar("g_gametype")->current.string);
				game::MSG_WriteByte(&msg, 2);

				auto v6 = 0;
				for (auto j = 0; j < 5617; j++)
				{
					auto next_config_string = game::CL_GetConfigString(j);
					if (next_config_string)
					{
						++v6;
					}
				}

				game::MSG_WriteShort(&msg, v6);

				// TODO: some loop over configs... again!
				game::MSG_WriteByte(&msg, 6);
				game::MSG_WriteLong(&msg, clc->clientNum);
				game::MSG_WriteLong(&msg, clc->checksumFeed);
				game::MSG_WriteByte(&msg, 6);

				/*
				static unsigned char cmpData[131072];
				const auto compressedSize = game::MSG_WriteBitsCompress(false, (unsigned char*)msg.data, cmpData, msg.cursize);
				const auto fileCompressedSize = compressedSize + 4;

				int byte8 = 8;
				*/

				unsigned char byte0 = 0;
				std::memcpy(&byte0, 0, 1);
			});

#endif // DEBUG
		}
	};
}

REGISTER_COMPONENT(theatre::component)