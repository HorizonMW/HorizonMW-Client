#include <std_include.hpp>
#include "voice_chat_globals.hpp"

#include <component/console.hpp>

namespace voice_chat_globals {
	std::unordered_map<game::gclient_s*, game::gclient_s*> last_killed_by;
	std::unordered_map<game::gclient_s*, game::gclient_s*> last_player_killed;

	bool has_shown_voice_message = false;

	bool is_voice_message_shown()
	{
		return has_shown_voice_message;
	}

	void set_voice_message_shown_state(bool state) {
		has_shown_voice_message = state;
	}
}