#include <std_include.hpp>
#include "game.hpp"

namespace game
{
	uint64_t base_address;

	int Cmd_Argc()
	{
		return cmd_args->argc[cmd_args->nesting];
	}

	const char* Cmd_Argv(const int index)
	{
		return cmd_args->argv[cmd_args->nesting][index];
	}

	int SV_Cmd_Argc()
	{
		return sv_cmd_args->argc[sv_cmd_args->nesting];
	}

	const char* SV_Cmd_Argv(const int index)
	{
		return sv_cmd_args->argv[sv_cmd_args->nesting][index];
	}

	bool VirtualLobby_Loaded()
	{
		return *virtualLobby_loaded == 1;
	}

	void SV_GameSendServerCommand(int client_num, svscmd_type type, const char* text)
	{
		const auto svs_clients_ = *game::svs_clients;
		if (svs_clients_ == nullptr)
		{
			return;
		}

		if (client_num == -1)
		{
			SV_SendServerCommand(0, type, "%s", text);
		}
		else
		{
			SV_SendServerCommand(&svs_clients_[client_num], type, "%s", text);
		}
	}

	void Cmd_TokenizeString(const char* text)
	{
		const auto a2 = 512 - *reinterpret_cast<int*>(0x3516F40_b);
		Cmd_TokenizeStringWithLimit(text, a2);
	}

	void Cmd_EndTokenizeString()
	{
		const auto nesting = cmd_args->nesting;
		const auto argc = cmd_args->argc[nesting];
		--cmd_args->nesting;
		cmd_argsPrivate->totalUsedArgvPool -= argc;
		cmd_argsPrivate->totalUsedTextPool -= cmd_argsPrivate->usedTextPool[nesting];
	}

	connstate_t CL_GetLocalClientConnectionState(const int localClientNum)
	{
		// cant find actual way to reference :/
		return static_cast<connstate_t>(*(keyCatchers + 1));
	}

	uint32_t BG_GetPerkBit(unsigned int perkIndex)
	{
		return (1 << (perkIndex & 0x1F));
	}

	uint32_t BG_GetPerkSlot(unsigned int perkIndex)
	{
		return (perkIndex >> 5);
	}

	bool BG_HasPerk(const unsigned int* perks, unsigned int perkIndex)
	{
		return (perks[BG_GetPerkSlot(perkIndex)] & BG_GetPerkBit(perkIndex)) != 0;
	}

	namespace environment
	{
		launcher::mode mode = launcher::mode::none;

		launcher::mode get_real_mode()
		{
			if (mode == launcher::mode::none)
			{
				throw std::runtime_error("Launcher mode not valid. Something must be wrong.");
			}

			return mode;
		}

		launcher::mode get_mode()
		{
			return get_real_mode();
		}

		bool is_mp()
		{
			return get_mode() == launcher::mode::multiplayer;
		}

		bool is_dedi()
		{
			return get_mode() == launcher::mode::server;
		}

		void set_mode(const launcher::mode _mode)
		{
			mode = _mode;
		}

		std::string get_string()
		{
			const auto current_mode = get_real_mode();
			switch (current_mode)
			{
			case launcher::mode::server:
				return "Dedicated Server";

			case launcher::mode::multiplayer:
				return "Multiplayer";

			case launcher::mode::none:
				return "None";

			default:
				return "Unknown (" + std::to_string(static_cast<int>(mode)) + ")";
			}
		}
	}
}

size_t operator"" _b(const size_t ptr)
{
	return game::base_address + ptr;
}

size_t reverse_b(const size_t ptr)
{
	return ptr - game::base_address;
}

size_t reverse_b(const void* ptr)
{
	return reverse_b(reinterpret_cast<size_t>(ptr));
}
