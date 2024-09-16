#include <game/structs.hpp>

namespace hmw_voice_chat
{

	namespace Server {
		bool sv_voice_enabled();
		bool sv_voice_team_enabled();
		bool sv_voice_deathchat_enabled();
		int get_max_clients();
		void sv_queue_voice_packet(const int talker, const int client_num, const game::VoicePacket_t* packet);
		bool on_same_team(const game::gentity_s* ent, const game::gentity_s* other_ent);
		bool is_session_state(game::gclient_s* client, game::sessionState_t state);
		bool is_session_state_same(game::gclient_s* client, game::gclient_s* other_client);
		bool g_dead_chat_enabled();
		void g_broadcast_voice(game::gentity_s* talker, const game::VoicePacket_t* packet);
		void sv_user_voice(game::client_t* cl, game::msg_t* msg);
		void sv_pre_game_user_voice(game::client_t* cl, game::msg_t* msg);
		void sv_voice_packet(game::netadr_s from, game::msg_t* msg);
		void sv_write_voice_data_to_client(const int client_num, game::msg_t* msg);
		void sv_send_client_voice_data(game::client_t* client);
		void sv_send_client_messages_stub(game::client_t* client, game::msg_t* msg, unsigned char* buf);
	}

	namespace Client {
		bool cl_voice_enabled();
		void cl_clear_muted_list();
		void cl_write_voice_packet_stub(const int local_client_num);
		//void voice_is_xuid_talking_stub([[maybe_unused]] void* session, __int64 xuid);
		bool cl_is_player_muted_stub([[maybe_unused]] void* session, int mute_client_index);
		void voice_mute_member_stub([[maybe_unused]] void* session, const int mute_client_index);
		void voice_unmute_member_stub([[maybe_unused]] void* session, const int mute_client_index);
		void cl_toggle_player_mute(const int local_client_num, const int mute_client_index);
		void cl_voice_packet(game::netadr_s* address, game::msg_t* msg);
		void ui_mute_player_stub([[maybe_unused]] void* session, const int some_index);
		void client_disconnect_stub(__int64 client_num, const char* a2);
		int stricmp_stub(const char* a1, const char* a2);
	}

	void setup_hooks();
}

