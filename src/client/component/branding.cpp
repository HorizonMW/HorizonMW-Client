#include <std_include.hpp>
#include "loader/component_loader.hpp"

#include "command.hpp"
#include "dvars.hpp"
#include "localized_strings.hpp"
#include "scheduler.hpp"
#include "version.hpp"

#include "game/game.hpp"

#include <utils/hook.hpp>
#include <utils/string.hpp>

// fonts/default.otf, fonts/defaultBold.otf, fonts/fira_mono_regular.ttf, fonts/fira_mono_bold.ttf

namespace branding
{
	namespace
	{
		utils::hook::detour ui_get_formatted_build_number_hook;
		const char* ui_get_formatted_build_number_stub()
		{
			return VERSION;
		}
	}

	class component final : public component_interface
	{
	public:
		void post_unpack() override
		{
			if (game::environment::is_dedi())
			{
				return;
			}

			ui_get_formatted_build_number_hook.create(0x1DF300_b, ui_get_formatted_build_number_stub);
		}
	};
}

REGISTER_COMPONENT(branding::component)
