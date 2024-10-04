#pragma once

#include "structs.hpp"
#include "launcher/launcher.hpp"

#define SERVER_CD_KEY "H2MOD-CD-Key"

namespace game
{
	extern uint64_t base_address;

	namespace environment
	{
		launcher::mode get_mode();
		launcher::mode get_real_mode();

		bool is_mp();
		bool is_dedi();

		void set_mode(launcher::mode mode);

		std::string get_string();
	}

	template <typename T>
	class symbol
	{
	public:
		symbol(const size_t mp_address) : mp_object_(reinterpret_cast<T*>(mp_address))
		{
		}

		T* get() const
		{
			return reinterpret_cast<T*>((uint64_t)mp_object_ + base_address);
		}

		operator T* () const
		{
			return this->get();
		}

		T* operator->() const
		{
			return this->get();
		}

	private:
		T* mp_object_;
	};

	int Cmd_Argc();
	const char* Cmd_Argv(int index);

	int SV_Cmd_Argc();
	const char* SV_Cmd_Argv(int index);

	bool VirtualLobby_Loaded();

	void SV_GameSendServerCommand(int clientNum, svscmd_type type, const char* text);

	void Cmd_TokenizeString(const char* text);
	void Cmd_EndTokenizeString();

	connstate_t CL_GetLocalClientConnectionState(const int localClientNum);

	uint32_t BG_GetPerkBit(unsigned int perkIndex);
	uint32_t BG_GetPerkSlot(unsigned int perkIndex);
	bool BG_HasPerk(const unsigned int* perks, unsigned int perkIndex);
}

size_t operator"" _b(const size_t ptr);
size_t reverse_b(const size_t ptr);
size_t reverse_b(const void* ptr);

#include "symbols.hpp"
