#include "std_include.hpp"
#include "hmw_voice_chat.hpp"
#include "voice_chat_globals.hpp"

#include "game/game.hpp"
#include <game/dvars.hpp>

#include <utils/hook.hpp>
#include <component/console.hpp>
#include <component/network.hpp>
#include <game/ui_scripting/execution.hpp>
#include "component/scripting.hpp"
#include "component/gsc/script_extension.hpp"

namespace hmw_voice_chat {

	namespace {
		static constexpr auto MAX_VOICE_PACKET_DATA = 256;
		static constexpr auto MAX_SERVER_QUEUED_VOICE_PACKETS = 2000;
		static constexpr std::size_t MAX_CLIENTS = 18;

		game::VoicePacket_t voice_packets[MAX_CLIENTS][MAX_SERVER_QUEUED_VOICE_PACKETS];
		int voice_packet_count[MAX_CLIENTS];

		bool mute_list[MAX_CLIENTS];
		bool s_playerMute[MAX_CLIENTS];
		int s_clientTalkTime[MAX_CLIENTS];

		int saved_talking_client = 0;

		game::dvar_t* sv_voice = nullptr;
		game::dvar_t* sv_voice_team = nullptr;
		game::dvar_t* sv_voice_death_chat = nullptr;
		game::dvar_t* sv_voice_intermission = nullptr;
		game::dvar_t* sv_voice_all = nullptr;
		game::dvar_t* sv_mapvote_active = nullptr;

		utils::hook::detour cl_write_voice_packet_hook;
		utils::hook::detour cl_is_player_talking_hook;
		utils::hook::detour ui_get_talker_client_num_hook;
		utils::hook::detour client_disconnect_hook;
		utils::hook::detour update_user_session_hook;
		utils::hook::detour session_is_user_registered_hook;
	}

#ifdef DEBUG
	game::dvar_t* voice_debug = nullptr;
#endif

	namespace Server {

		bool sv_voice_enabled()
		{
			return (sv_voice && sv_voice->current.enabled);
		}

		bool sv_voice_team_enabled()
		{
			return (sv_voice_team && sv_voice_team->current.enabled);
		}

		bool sv_voice_all_enabled()
		{
			return (sv_voice_all && sv_voice_all->current.enabled);
		}

		bool sv_voice_deathchat_enabled()
		{
			return (sv_voice_death_chat && sv_voice_death_chat->current.enabled);
		}

		bool sv_mapvote_active_enabled()
		{
			return (sv_mapvote_active && sv_mapvote_active->current.enabled);
		}


		int get_max_clients()
		{
			static const auto max_clients = game::Dvar_FindVar("sv_maxclients");
			if (max_clients)
			{
				return max_clients->current.integer;
			}
			return 1; // idk
		}

		void sv_queue_voice_packet(const int talker, const int client_num, const game::VoicePacket_t* packet)
		{
			if (client_num < 0 || client_num >= MAX_CLIENTS)
			{
				return;
			}

			auto packet_count = voice_packet_count[client_num];
			if (packet_count >= MAX_SERVER_QUEUED_VOICE_PACKETS)
			{
#ifdef DEBUG
				console::error("packet_count exceeds MAX_SERVER_QUEUED_VOICE_PACKETS (%d/%d)\n", packet_count, MAX_SERVER_QUEUED_VOICE_PACKETS);
#endif
				return;
			}


			if (packet->dataSize <= 0 || packet->dataSize > MAX_VOICE_PACKET_DATA)
			{
				return;
			}

			voice_packets[client_num][voice_packet_count[client_num]].dataSize = packet->dataSize;
			std::memcpy(voice_packets[client_num][voice_packet_count[client_num]].data, packet->data, packet->dataSize);

			voice_packets[client_num][voice_packet_count[client_num]].talker = static_cast<char>(talker);
			++voice_packet_count[client_num];
		}

		bool on_same_team(const game::gentity_s* ent, const game::gentity_s* other_ent)
		{
			if (!ent->client || !other_ent->client)
			{
				return false;
			}

			return ent->client->team == other_ent->client->team;
		}

		bool is_session_state(game::gclient_s* client, game::sessionState_t state)
		{
			return (client && client->sessionState == state);
		}

		bool is_session_state_same(game::gclient_s* client, game::gclient_s* other_client)
		{
			return (client && other_client && client->sessionState == other_client->sessionState);
		}

		bool g_dead_chat_enabled()
		{
			static const auto dead_chat = game::Dvar_FindVar("g_deadchat");
			return (dead_chat && dead_chat->current.enabled);
		}

		bool was_target_client_last_killed_by_talker_client(game::gclient_s* target_client, game::gclient_s* talker_client) {
			return voice_chat_globals::last_killed_by[target_client] == talker_client;
		}

		bool was_target_client_his_last_kill_talker_client(game::gclient_s* talker_client, game::gclient_s* target_client) {
			return voice_chat_globals::last_player_killed[talker_client] = target_client;
		}

		inline int get_num_of_players()
		{
			const auto client_state = *game::client_state;
			if (client_state == nullptr)
			{
				return 0;
			}

			return (client_state->num_players == 0 ? 18 : client_state->num_players);
		}

		void g_broadcast_voice(game::gentity_s* talker, const game::VoicePacket_t* packet)
		{
#ifdef DEBUG
			if (voice_debug->current.enabled)
			{
				console::debug("broadcasting voice from %d to other players...\n", talker->s.number);
			}
#endif

			for (auto other_player = 0; other_player < get_num_of_players(); ++other_player)
			{
				auto* target_ent = &game::g_entities[other_player];
				auto* target_client = target_ent->client;

				auto* talker_client = talker->client;

				// Talker is not receiver of VoicePacket_ta
				if (target_client && talker != target_ent) {
					// Server-wide VC
					if (sv_voice_all_enabled()) {
						sv_queue_voice_packet(talker->s.number, other_player, packet);
						return;
					}

					// Everyone is in intermission, forward voice_packets to everyone.
					if ((is_session_state(target_client, game::SESS_STATE_INTERMISSION) || sv_mapvote_active_enabled()) && is_session_state_same(target_client, talker_client)) {
						sv_queue_voice_packet(talker->s.number, other_player, packet);
					}

					// Team VC is enabled
					if (sv_voice_team_enabled())
					{
						// Players are on the same team, and both are dead or alive. (Alive can't hear dead, Dead can't hear alive)
						if (on_same_team(talker, target_ent) && is_session_state_same(target_client, talker_client)) {
							sv_queue_voice_packet(talker->s.number, other_player, packet);
						}
					}

					// Spectator VC???
					if (talker_client->team == game::TEAM_FREE && is_session_state_same(target_client, talker_client)) {
						sv_queue_voice_packet(talker->s.number, other_player, packet);
					}

					// Deathchat is enabled
					if (sv_voice_deathchat_enabled())
					{
						// Forward VC to killer of talker
						if (was_target_client_last_killed_by_talker_client(target_client, talker_client) && is_session_state(talker_client, game::SESS_STATE_DEAD)) {
							sv_queue_voice_packet(talker->s.number, other_player, packet);
						}

						// Forward VC to talker of killer
						if (was_target_client_his_last_kill_talker_client(talker_client, target_client) && is_session_state(target_client, game::SESS_STATE_DEAD)) {
							sv_queue_voice_packet(talker->s.number, other_player, packet);
						}
					}
				}
			}
		}

		void sv_user_voice(game::client_t* cl_, game::msg_t* msg)
		{
			game::VoicePacket_t packet{};

			if (!sv_voice_enabled())
			{
				return;
			}

			const auto packet_count = game::MSG_ReadByte(msg);
			for (auto packet_itr = 0; packet_itr < packet_count; ++packet_itr)
			{
				packet.dataSize = game::MSG_ReadByte(msg);
				if (packet.dataSize <= 0 || packet.dataSize > MAX_VOICE_PACKET_DATA)
				{
					return;
				}

				game::MSG_ReadData(msg, packet.data, packet.dataSize);
				g_broadcast_voice(cl_->gentity, &packet);
			}
		}

		void sv_pre_game_user_voice(game::client_t* client, game::msg_t* msg)
		{
			if (!sv_voice_enabled())
			{
				return;
			}

			game::VoicePacket_t packet{};

			const auto talker = static_cast<int>(client - *game::svs_clients);

			const auto packet_count = game::MSG_ReadByte(msg);
			for (auto packet_itr = 0; packet_itr < packet_count; ++packet_itr)
			{
				packet.dataSize = game::MSG_ReadByte(msg);
				if (packet.dataSize <= 0 || packet.dataSize > MAX_VOICE_PACKET_DATA)
				{
					return;
				}

				game::MSG_ReadData(msg, packet.data, packet.dataSize);
				for (auto other_player = 0; other_player < get_num_of_players(); ++other_player)
				{
					if (other_player != talker &&
						game::svs_clients[other_player]->header.state >= game::CS_CONNECTED)
					{
						sv_queue_voice_packet(talker, other_player, &packet);
					}
				}
			}
		}

		void sv_voice_packet(game::netadr_s from, game::msg_t* msg)
		{
			const auto qport = game::MSG_ReadShort(msg);
			auto* client = utils::hook::invoke<game::client_t*>(0x1CB1F0_b, from, qport);
			if (!client || client->header.state == game::CS_ZOMBIE)
			{
				return;
			}

			client->lastPacketTime = *game::svs_time;
			if (client->header.state < game::CS_ACTIVE)
			{
				//TODO: Fix this post-release
				//sv_pre_game_user_voice(client, msg);
			}
			else
			{
				sv_user_voice(client, msg);
			}
		}

		void sv_write_voice_data_to_client(const int client_num, game::msg_t* msg)
		{
			game::MSG_WriteByte(msg, static_cast<char>(voice_packet_count[client_num]));
			for (auto packet = 0; packet < voice_packet_count[client_num]; ++packet)
			{
				game::MSG_WriteByte(msg, voice_packets[client_num][packet].talker);

				game::MSG_WriteByte(msg, static_cast<char>(voice_packets[client_num][packet].dataSize));
				game::MSG_WriteData(msg, voice_packets[client_num][packet].data, voice_packets[client_num][packet].dataSize);
			}
		}

		void sv_send_client_voice_data(game::client_t* client)
		{
			game::msg_t msg{};
			const auto client_num = static_cast<int>(client - *game::svs_clients);

			const auto msg_buf_large = std::make_unique<unsigned char[]>(0x20000);
			auto* msg_buf = msg_buf_large.get();

			if (client->header.state != game::CS_ACTIVE || voice_packet_count[client_num] < 1)
			{
				return;
			}

			game::MSG_Init(&msg, msg_buf, 0x20000);

			game::MSG_WriteString(&msg, "v");
			sv_write_voice_data_to_client(client_num, &msg);

			if (msg.overflowed)
			{
				console::warn("voice msg overflowed for %s\n", client->name);
			}
			else
			{
				game::NET_OutOfBandVoiceData(game::NS_SERVER, const_cast<game::netadr_s*>(&client->header.remoteAddress), msg.data, msg.cursize);
				voice_packet_count[client_num] = 0;
			}
		}

		void sv_send_client_messages_stub(game::client_t* client, game::msg_t* msg, unsigned char* buf)
		{
			// SV_EndClientSnapshot
			utils::hook::invoke<void>(0x561B50_b, client, msg, buf);

			sv_send_client_voice_data(client);
		}
	}

	namespace Client {
		bool cl_voice_enabled()
		{
			static const auto cl_voice = game::Dvar_FindVar("cl_voice");
			return (cl_voice && cl_voice->current.enabled);
		}

		inline int get_num_of_players()
		{
			const auto client_state = *game::client_state;
			if (client_state == nullptr)
			{
				return 0;
			}

			return (client_state->num_players == 0 ? 18 : client_state->num_players);
		}

		void cl_clear_muted_list()
		{
			std::memset(mute_list, 0, sizeof(mute_list));
		}

		void cl_write_voice_packet_stub(const int local_client_num)
		{
			if (!game::CL_IsCgameInitialized() || game::VirtualLobby_Loaded() || !cl_voice_enabled())
			{
				return;
			}

			const auto connection_state = game::CL_GetLocalClientConnectionState(local_client_num);
			if (connection_state < game::CA_LOADING)
			{
				return;
			}

			const auto* clc = game::clientConnections[local_client_num];
			const auto vc = *game::cl_voiceCommunication;

			unsigned char packet_buf[0x800]{};
			game::msg_t msg{};

			game::MSG_Init(&msg, packet_buf, sizeof(packet_buf));
			game::MSG_WriteString(&msg, "v");
			game::MSG_WriteShort(&msg, static_cast<short>(clc->qport));
			game::MSG_WriteByte(&msg, vc.voicePacketCount);

			for (auto packet = 0; packet < vc.voicePacketCount; ++packet)
			{
				game::MSG_WriteByte(&msg, vc.voicePackets[packet].dataSize);
				game::MSG_WriteData(&msg, vc.voicePackets[packet].data, vc.voicePackets[packet].dataSize);
			}

			game::NET_OutOfBandVoiceData(clc->netchan.sock, const_cast<game::netadr_s*>(&clc->serverAddress), msg.data, msg.cursize);
		}

		bool is_player_talking(int client_num)
		{
			auto current_time = game::Sys_Milliseconds();
			auto client_talk_time = s_clientTalkTime[client_num];
			if (!client_talk_time)
			{
				return false;
			}

			auto res = (current_time - client_talk_time) < 300;
			return res;
		}


		bool cl_is_player_talking_stub(void* session, int client_num)
		{
			if (client_num >= MAX_CLIENTS || client_num < 0)
			{
				return false;
			}

			return is_player_talking(client_num);
		}


		bool voice_is_xuid_talking_stub(void* session, uint64_t xuid)
		{
			// Patoke @note: use saved_talking_client so we don't rely on the session and xuid which might not be populated
			return is_player_talking(saved_talking_client);
		}


		// Patoke @note: unused
		int ui_get_talker_client_num_stub(__int64 a1, int a2)
		{
			auto idek = 0;
			auto current_client_index = 0;

			auto num_of_players = get_num_of_players();
			while (1)
			{
				auto server_sess = &(*game::g_serverSession);

				[[maybe_unused]] auto is_user_registered = true; //utils::hook::invoke<bool>(0x56B5C0_b, server_sess, current_client_index);
				auto is_player_talking = utils::hook::invoke<bool>(0x135950_b, server_sess, current_client_index);

				auto v5 = *game::cl_maxLocalClients;
				for (auto i = 0; i < v5; ++i)
				{
					if (**(&game::keyCatchers + 1) > 8)            // wtf
					{
						if (utils::hook::invoke<__int64>(0x3162D0_b) == current_client_index)
							goto increment_and_check;

						v5 = *game::cl_maxLocalClients;
					}
				}

				if (current_client_index >= num_of_players)
					goto increment_and_check;

				if (!is_player_talking)
					goto increment_and_check;

				if (idek == a2)
					return current_client_index;

				++idek;

			increment_and_check:
				if (++current_client_index >= 18)
					return -1;
			}
		}

		bool cl_is_player_muted_stub(void* session, int mute_client_index)
		{
			return s_playerMute[mute_client_index];
		}

		void voice_mute_member_stub(void* session, const int mute_client_index)
		{
			console::info("Muting client %d", mute_client_index);
#ifdef DEBUG
			if (voice_debug->current.enabled)
			{
				console::debug("muting client number %d\n", mute_client_index);
			}
#endif
			ui_scripting::notify("muteplayer", { {"client_index", mute_client_index} });
			s_playerMute[mute_client_index] = true;
		}

		void voice_unmute_member_stub(void* session, const int mute_client_index)
		{
			console::info("Unmuting client %d", mute_client_index);
#ifdef DEBUG
			if (voice_debug->current.enabled)
			{
				console::debug("unmuting client number %d\n", mute_client_index);
			}
#endif
			ui_scripting::notify("unmuteplayer", { {"client_index", mute_client_index} });
			s_playerMute[mute_client_index] = false;
		}

		void cl_toggle_player_mute(const int local_client_num, const int mute_client_index)
		{
			if (cl_is_player_muted_stub(nullptr, mute_client_index))
			{
				voice_unmute_member_stub(nullptr, mute_client_index);
			}
			else
			{
				voice_mute_member_stub(nullptr, mute_client_index);
			}
		}

		void cl_voice_packet(game::netadr_s* address, game::msg_t* msg)
		{
			if (!game::CL_IsCgameInitialized() || game::VirtualLobby_Loaded() || !cl_voice_enabled())
			{
				return;
			}

			auto* clc = game::clientConnections[0];
			if (!utils::hook::invoke<bool>(0x4F1850_b, clc->serverAddress, *address))
			{
				return;
			}

			const auto num_packets = game::MSG_ReadByte(msg);
			if (num_packets < 0 || num_packets > MAX_SERVER_QUEUED_VOICE_PACKETS)
			{
#ifdef DEBUG
				console::error("num_packets was less than 0 or greater than MAX_SERVER_QUEUED_VOICE_PACKETS (%d/%d)\n", num_packets, MAX_SERVER_QUEUED_VOICE_PACKETS);
#endif
				return;
			}

			game::VoicePacket_t packet{};
			for (auto packet_itr = 0; packet_itr < num_packets; ++packet_itr)
			{
				packet.talker = static_cast<char>(game::MSG_ReadByte(msg));
				packet.dataSize = game::MSG_ReadByte(msg);
				if (packet.dataSize <= 0 || packet.dataSize > MAX_VOICE_PACKET_DATA)
				{
					return;
				}

				game::MSG_ReadData(msg, packet.data, packet.dataSize);

				if (packet.talker >= MAX_CLIENTS)
				{
					return;
				}

				if (!cl_is_player_muted_stub(nullptr, packet.talker))
				{
#ifdef DEBUG
					if (voice_debug->current.enabled)
					{
						console::debug("calling Voice_IncomingVoiceData with talker %d's data\n", packet.talker);
					}
#endif

					// Voice_IncomingVoiceData
					utils::hook::invoke<void>(0x5BF370_b, nullptr, packet.talker, reinterpret_cast<unsigned char*>(packet.data), packet.dataSize);
					s_clientTalkTime[packet.talker] = game::Sys_Milliseconds();
				}
			}
		}

		void client_disconnect_stub(__int64 client_num, const char* a2)
		{
			client_disconnect_hook.invoke<void>(client_num, a2);
		}

		void update_user_session_stub(game::client_t* cl_)
		{
			update_user_session_hook.invoke<void>(cl_);
		}

		int stricmp_stub(const char* a1, const char* a2)
		{
			return utils::hook::invoke<int>(0x5AF5F0_b, a1, "v");
		}

		// Patoke @note: skip user registered checks related to voice chat
		bool session_is_user_registered_stub(void* session, char client_num)
		{
			auto ret = reinterpret_cast<uintptr_t>(_ReturnAddress());
			// also should check cl_is_player_talking, but that's completely overwritten by one of our hooks
			if (ret == 0x1E0991_b ||// UI_IsClientTalking
				ret == 0x1E0481_b)	// UI_GetTalkerClientNum
			{
				// save client number, that way we don't need to get it thru their xuid
				saved_talking_client = client_num;
				return true;
			}

			// stop mute functions from not proceeding if a player doesn't have a registered session
			if (ret == 0x12E607_b ||// CL_CopyPlayerMutes
				ret == 0x1388DB_b ||// CL_MuteAllPlayers
				ret == 0x13896B_b ||// CL_MuteAllPlayersButFriends
				ret == 0x1389EB_b ||// CL_MuteAllPlayersButParty
				ret == 0x13B9AB_b ||// CL_UnmuteAllPlayers
				ret == 0x13BA1B_b ||// CL_UnmuteAllPlayersButFriends
				ret == 0x13BA9B_b)	// CL_UnmuteAllPlayersButParty
			{
				return true;
			}

			return session_is_user_registered_hook.invoke<bool>(session, client_num);
		}

		// Patoke @note: allow us to modify mute states from the mute players menu
		bool cl_can_change_player_mute_stub(void* session, int client_num)
		{
			return true;
		}
	}

	void setup_hooks()
	{
		std::memset(voice_packets, 0, sizeof(voice_packets));
		std::memset(voice_packet_count, 0, sizeof(voice_packet_count));

		hmw_voice_chat::Client::cl_clear_muted_list();

		//events::on_steam_disconnect(cl_clear_muted_list);
		client_disconnect_hook.create(0x404730_b, Client::client_disconnect_stub);

		// write voice packets to server instead of other clients
		cl_write_voice_packet_hook.create(0x13DB10_b, Client::cl_write_voice_packet_stub);

		// disable 'v' OOB handler and use our own
		utils::hook::set<std::uint8_t>(0x12F2AD_b, 0xEB);
		network::on_raw("v", Client::cl_voice_packet);

		if (!game::environment::is_dedi())
		{
			session_is_user_registered_hook.create(0x56B5C0_b, Client::session_is_user_registered_stub);
			utils::hook::jump(0x135950_b, Client::cl_is_player_talking_stub, true);
			utils::hook::jump(0x5BF7F0_b, Client::voice_is_xuid_talking_stub, true);

			// Patoke @note: unused hooks
			//Client::cl_is_player_talking_hook.create(0x135950_b, Client::cl_is_player_talking_stub);
			//Client::ui_get_talker_client_num_hook.create(0x1E0420_b, Client::ui_get_talker_client_num_stub);
		}
		utils::hook::jump(0x1358B0_b, Client::cl_is_player_muted_stub, true);
		utils::hook::jump(0x12C6D0_b, Client::cl_can_change_player_mute_stub, true);

		utils::hook::call(0x5624F3_b, Server::sv_send_client_messages_stub);

		// recycle server packet handler for icanthear
		utils::hook::call(0x1CAF25_b, Client::stricmp_stub); // use v instead
		utils::hook::call(0x1CAF3E_b, Server::sv_voice_packet);

		utils::hook::jump(0x5BF8B0_b, Client::voice_mute_member_stub, true);
		utils::hook::jump(0x5BFC20_b, Client::voice_unmute_member_stub, true);

		sv_voice = dvars::register_bool("sv_voice", true, game::DVAR_FLAG_NONE, "Use server side voice communications");
		sv_voice_all = dvars::register_bool("sv_voice_all", false, game::DVAR_FLAG_NONE, "Enable all comms server wide voice communications");
		sv_voice_team = dvars::register_bool("sv_voice_team", true, game::DVAR_FLAG_NONE, "Enable team voice communications");
		sv_voice_death_chat = dvars::register_bool("sv_voice_death_chat", true, game::DVAR_FLAG_NONE, "Enable deathchat voice communications");

		sv_mapvote_active = dvars::register_bool("sv_mapvote_active", false, game::DVAR_FLAG_NONE, "mapvote is active");
#ifdef DEBUG
		voice_debug = dvars::register_bool("voice_debug", true, game::DVAR_FLAG_NONE, "Debug voice chat stuff");
#endif
	}


}
