#include <std_include.hpp>
#include "loader/component_loader.hpp"

#include "auth.hpp"
#include "clantags.hpp"
#include "command.hpp"
#include "console.hpp"
#include "discord.hpp"
#include "network.hpp"

#include "game/game.hpp"
#include "steam/steam.hpp"

#include <utils/hook.hpp>
#include <utils/string.hpp>
#include <utils/properties.hpp>
#include <utils/smbios.hpp>
#include <utils/info_string.hpp>
#include <utils/cryptography.hpp>
#include <utils/http.hpp>
#include <utils/obfus.hpp>

namespace auth
{
	namespace
	{
		std::string get_hdd_serial()
		{
			DWORD serial{};
			if (!GetVolumeInformationA("C:\\", nullptr, 0, &serial, nullptr, nullptr, nullptr, 0))
			{
				return {};
			}

			return utils::string::va("%08X", serial);
		}

		std::string get_hw_profile_guid()
		{
			auto hw_profile_path = (utils::properties::get_appdata_path() / "hwgd.pf").generic_string();
			if (utils::io::file_exists(hw_profile_path))
			{
				auto hw_profile_info = utils::io::read_file(hw_profile_path);

				if (!hw_profile_info.empty())
					return hw_profile_info;
			}


			HW_PROFILE_INFO info;
			if (!GetCurrentHwProfileA(&info))
			{
				return {};
			}

			auto hw_profile_info = std::string{ info.szHwProfileGuid, sizeof(info.szHwProfileGuid) };
			utils::io::write_file(hw_profile_path, hw_profile_info);

			return hw_profile_info;
		}

		std::string get_protected_data()
		{
			std::string input = "HMWMod-Auth";

			DATA_BLOB data_in{}, data_out{};
			data_in.pbData = reinterpret_cast<uint8_t*>(input.data());
			data_in.cbData = static_cast<DWORD>(input.size());
			if (CryptProtectData(&data_in, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_LOCAL_MACHINE, &data_out) != TRUE)
			{
				return {};
			}

			const auto size = std::min(data_out.cbData, 52ul);
			std::string result{ reinterpret_cast<char*>(data_out.pbData), size };
			LocalFree(data_out.pbData);

			return result;
		}

		std::string get_key_entropy()
		{
			std::string entropy{};
			entropy.append(utils::smbios::get_uuid());
			entropy.append(get_hw_profile_guid());
			entropy.append(get_protected_data());
			entropy.append(get_hdd_serial());

			if (entropy.empty())
			{
				entropy.resize(32);
				utils::cryptography::random::get_data(entropy.data(), entropy.size());
			}

			return entropy;
		}

		utils::cryptography::ecc::key& get_key()
		{
			static auto key = utils::cryptography::ecc::generate_key(512, get_key_entropy());
			return key;
		}

		// need to move this somewhere else probably
		std::string hash_string(const std::string& str)
		{
			const auto value = game::generateHashValue(str.data());
			return utils::string::va("0x%lX", value);
		}

		bool send_connect_data(game::netsrc_t sock, game::netadr_s* adr, const char* format, const int len)
		{
			std::string connect_string(format, len);
			game::SV_Cmd_TokenizeString(connect_string.data());
			const auto _ = gsl::finally([]()
				{
					game::SV_Cmd_EndTokenizedString();
				});

			const command::params_sv params;
			if (params.size() < 3)
			{
				return false;
			}

			utils::info_string info_string{ std::string{params[2]} };

			// add discord ID to connect info string
			info_string.set(hash_string("discord_id"), discord::get_discord_id());

			const auto challenge = info_string.get(hash_string("challenge"));

			connect_string.clear();
			connect_string.append(params[0]);
			connect_string.append(" ");
			connect_string.append(params[1]);
			connect_string.append(" ");
			connect_string.append("\"" + info_string.build() + "\"");

			proto::network::connect_info info;
			info.set_publickey(get_key().get_public_key());
			info.set_signature(sign_message(get_key(), challenge));
			info.set_infostring(connect_string);

			network::send(*adr, "connect", info.SerializeAsString());
			return true;
		}

		void direct_connect(game::netadr_s* from, game::msg_t* msg)
		{
			const auto offset = sizeof("connect") + 4;

			proto::network::connect_info info;
			if (msg->cursize < offset || !info.ParseFromArray(msg->data + offset, msg->cursize - offset))
			{
				CALL(&network::send, *from, "error", "Invalid connect data!", '\n');
				return;
			}

			game::SV_Cmd_EndTokenizedString();
			game::SV_Cmd_TokenizeString(info.infostring().data());

			const command::params_sv params;
			if (params.size() < 3)
			{
				CALL(&network::send, *from, "error", "Invalid connect string!", '\n');
				return;
			}

			const utils::info_string info_string{ std::string{params[2]} };

			const auto steam_id = info_string.get(hash_string("xuid"));
			const auto challenge = info_string.get(hash_string("challenge"));

			if (steam_id.empty() || challenge.empty())
			{
				CALL(&network::send, *from, "error", "Invalid connect data!", '\n');
				return;
			}

			utils::cryptography::ecc::key key;
			key.set(info.publickey());

			const auto xuid = strtoull(steam_id.data(), nullptr, 16);

			if (xuid != key.get_hash())
			{
				CALL(&network::send, *from, "error",
					utils::string::va("XUID doesn't match the certificate: %llX != %llX", xuid, key.get_hash()), '\n');
				return;
			}

			if (!key.is_valid() || !verify_message(key, challenge, info.signature()))
			{
				CALL(&network::send, *from, "error", "Challenge signature was invalid!", '\n');
				return;
			}

			auto discordid = info_string.get(hash_string("discord_id"));

			if (game::VirtualLobby_Loaded())
			{
				game::SV_DirectConnect(from);
				return;
			}

			auto clantag = info_string.get(utils::string::va("0x%lX", 0x4D60A94B));
			if (!clantag.empty())
			{
				game::StringTable* gamertags_pc{};
				game::StringTable_GetAsset(OBF("mp/activisiongamertags_pc.csv"), &gamertags_pc);

				for (auto& tag_s : clantags::tags)
				{
					auto name_modified = utils::string::va("^%c%c%c%c%s", 1, tag_s.second.width, tag_s.second.height, 2, tag_s.second.short_name.data());
					if (tag_s.first == clantag || !strcmp(clantag.data(), name_modified))
					{
						if (!gamertags_pc || !gamertags_pc->rowCount)
						{
							CALL(&network::send, *from, OBF("error"), OBF("Failed to authenticate tag"), '\n');
							return;
						}

						if (!strcmp(clantag.data(), name_modified))
						{
							clantag = tag_s.first;
						}

						clantag = utils::string::to_upper(clantag);

						auto discord_id = info_string.get(hash_string("discord_id"));

						auto gamertags_row_count = game::StringTable_GetRowCount(gamertags_pc);
						for (auto row_i = 0; row_i < gamertags_row_count; ++row_i)
						{
							auto tag = game::StringTable_GetColumnValueForRow(gamertags_pc, row_i, 0);
							auto id = game::StringTable_GetColumnValueForRow(gamertags_pc, row_i, 1);

							if (!strcmp(discord_id.c_str(), id))
							{
								if (!strcmp(tag, "HMW"))
								{
									game::SV_DirectConnect(from);
									return;
								}
								if (!strcmp(tag, "H2M"))
								{
									
									if (strcmp(clantag.c_str(), "HMW") != 0)
									{
										game::SV_DirectConnect(from);
										return;
									}
								}
								if (!strcmp(tag, clantag.c_str()))
								{
									game::SV_DirectConnect(from);
									return;
								}
							}
						}

						CALL(&network::send, *from, OBF("error"), OBF("Invalid clantag"), '\n');
						return;
					}
				}
			}

			game::SV_DirectConnect(from);

		}

		void* get_direct_connect_stub()
		{
			return utils::hook::assemble([](utils::hook::assembler& a)
				{
					a.lea(rcx, qword_ptr(rsp, 0x20));
					a.movaps(xmmword_ptr(rsp, 0x20), xmm0);

					a.pushad64();
					a.mov(rdx, rsi);
					a.call_aligned(direct_connect);
					a.popad64();

					a.jmp(0x1CAF64_b);
				});
		}

		void* get_send_connect_data_stub()
		{
			return utils::hook::assemble([](utils::hook::assembler& a)
				{
					const auto false_ = a.newLabel();
					const auto original = a.newLabel();

					a.mov(ecx, eax);
					a.lea(r8, qword_ptr(rbp, 0x4C0));
					a.mov(r9d, ebx);
					a.lea(rdx, qword_ptr(rsp, 0x30));

					a.pushad64();
					a.call_aligned(send_connect_data);
					a.test(al, al);
					a.popad64();

					a.mov(rbx, qword_ptr(rsp, 0x9F0));
					a.jmp(0x12D446_b);
				});
		}
	}

	uint64_t get_guid()
	{
		if (game::environment::is_dedi())
		{
			return 0x110000100000000 | (::utils::cryptography::random::get_integer() & ~0x80000000);
		}

		return get_key().get_hash();
	}

	class component final : public component_interface
	{
	public:
		void post_unpack() override
		{
			// kill "disconnected from steam" error
			utils::hook::nop(0x1D61DF_b, 0x11);

			utils::hook::jump(0x1CAE70_b, get_direct_connect_stub(), true);
			utils::hook::jump(0x12D426_b, get_send_connect_data_stub(), true);

			// Don't instantly timeout the connecting client ? not sure about this
			utils::hook::set(0x12D93C_b, 0xC3);
		}
	};
}

REGISTER_COMPONENT(auth::component)