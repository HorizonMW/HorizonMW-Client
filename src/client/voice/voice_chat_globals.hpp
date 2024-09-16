#pragma once
#include <std_include.hpp>
#include "game/scripting/entity.hpp"

namespace voice_chat_globals {
	extern std::unordered_map<game::gclient_s*, game::gclient_s*> last_killed_by;
	extern std::unordered_map<game::gclient_s*, game::gclient_s*> last_player_killed;

	bool is_voice_message_shown();
	void set_voice_message_shown_state(bool state);
}