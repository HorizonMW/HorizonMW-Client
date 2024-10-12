#include <std_include.hpp>
#include "loader/component_loader.hpp"

#include "clantags.hpp"

#include "game/game.hpp"
#include "utils/hook.hpp"
#include "utils/string.hpp"

namespace clantags
{
	namespace
	{
		utils::hook::detour lui_pushplayername_hook;
		utils::hook::detour gamerprofile_getclanname_hook;
		utils::hook::detour cl_getclientstatefromcurrentsnapshot_hook;

		__int64 lui_pushplayername_stub(__int64 state, int localClientNumber, signed char entityNumber)
		{
			if (entityNumber >= 18)
			{
				return lui_pushplayername_hook.invoke<__int64>(state, localClientNumber, entityNumber);
			}

			char* v7;
			const char* v11;
			DWORD* v12;
			const char* a4;

			auto* bgs_clientinfo_array = reinterpret_cast<game::clientInfo_t*>(reinterpret_cast<uintptr_t>(game::getCGArray()) + 0x174DA8);
			auto* clientinfo = &bgs_clientinfo_array[entityNumber];

			v11 = clientinfo->clanAbbrev;
			if (clientinfo->clanAbbrev[0] && !clientinfo->use_elite_clan_tag)
				goto LABEL_12;
			v11 = clientinfo->elite_clan_tag_text;

			if (clientinfo->elite_clan_tag_text[0])
			{
				switch (clientinfo->use_elite_clan_tag)
				{
				case 1:
				LABEL_12:
					for (auto& tag : clantags::tags)
					{
						if (!strcmp(v11, tag.first.data()))
						{
							v11 = utils::string::va("^%c%c%c%c%s", 1, tag.second.width, tag.second.height, 2, tag.second.short_name.data());
						}
					}

					a4 = utils::string::va("[%s]%s", v11, clientinfo->name);
				LABEL_5:
					v7 = (char*)a4;
					goto LABEL_6;
				case 2:
					a4 = utils::string::va("[^3%s^7]%s", v11, clientinfo->name);
					goto LABEL_5;
				case 3:
					a4 = utils::string::va("[^1%s^7]%s", v11, clientinfo->name);
					goto LABEL_5;
				}
			}
			v7 = clientinfo->name;
			if (!v7)
			{
				v12 = *(DWORD**)(state + 72);
				*v12 = 0;
				*(ULONGLONG*)(state + 72) = *(ULONGLONG*)(v12 + 4);
				return 1;
			}
		LABEL_6:

			auto v8 = -1;
			do
				++v8;
			while (v7[v8]);

			game::hksi_lua_pushlstring(state, (const char*)v7, static_cast<unsigned int>(v8));
			return 1;
		}

		void* cl_getclientstatefromcurrentsnapshot_stub(unsigned int a1, int a2)
		{
			auto* snapshot = cl_getclientstatefromcurrentsnapshot_hook.invoke<void*>(a1, a2);
			if (snapshot == nullptr)
			{
				return snapshot;
			}

			for (auto& tag : clantags::tags)
			{
				auto clantag_va = utils::string::va("^%c%c%c%c%s", 1, tag.second.width, tag.second.height, 2, tag.second.short_name.data());
				auto clantag = static_cast<char*>(snapshot) + 0x7C;
				if (clantag != nullptr)
				{
					if (!strcmp(clantag, clantag_va))
					{
						strcpy_s(clantag, sizeof(clantag), tag.first.data());
					}
				}
			}

			return snapshot;
		}

		// Check if clantag  == HEX(01-20)
		static inline bool is_hex_in_range(char c, int start, int end) {
			return (static_cast<unsigned char>(c) >= start && static_cast<unsigned char>(c) <= end);
		}
		static inline bool contains_hex_01_to_20(const std::string& str) {
			for (char c : str) {
				if (is_hex_in_range(c, 0x01, 0x20)) {
					return true;
				}
			}
			return false;
		}
		static inline char* copy(char* dst, const std::string& src, std::size_t len = std::string::npos)
		{
			if (len == std::string::npos)
			{
				return std::strcpy(dst, src.c_str());
			}
			else
			{
				return std::strncpy(dst, src.c_str(), len);
			}
		}
		const char* gamerprofile_getclanname_stub(int controller)
		{
			auto clantag = gamerprofile_getclanname_hook.invoke<const char*>(controller);
			if (contains_hex_01_to_20(clantag))
			{
				//optional: com_error
				copy(*game::clanName, "none");
				return "";
			}
			for (auto& tag : clantags::tags)
			{
				if (!strcmp(clantag, tag.first.data()) && game::UI_ActivisionClanTagAllowedForGamerTag(clantag, ""))
				{
					clantag = utils::string::va("^%c%c%c%c%s", 1, tag.second.width, tag.second.height, 2, tag.second.short_name.data());
					break;
				}
			}
			return clantag;
		}

	}

	class component final : public component_interface
	{
	public:
		void post_unpack() override
		{
			lui_pushplayername_hook.create(0x286410_b, lui_pushplayername_stub);
			gamerprofile_getclanname_hook.create(0x345450_b, gamerprofile_getclanname_stub);
			cl_getclientstatefromcurrentsnapshot_hook.create(0x344320_b, cl_getclientstatefromcurrentsnapshot_stub);

			utils::hook::set<byte>(0x28AE97_b, 0x85); // JNZ on membersclantag
		}
	};
}



REGISTER_COMPONENT(clantags::component)
