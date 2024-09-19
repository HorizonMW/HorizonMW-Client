#include <std_include.hpp>
#include "loader/component_loader.hpp"

#include "game/game.hpp"
#include "game/dvars.hpp"

#include "scheduler.hpp"

#include <utils/hook.hpp>

namespace thirdperson
{
	namespace
	{
		game::dvar_t* cg_thirdPerson = nullptr;
		game::dvar_t* cg_thirdPersonRange = nullptr;
		game::dvar_t* cg_thirdPersonAngle = nullptr;

		int update_third_person_stub(int local_client_num, game::cg_s* cgame_glob)
		{
			auto next_snap = cgame_glob->nextSnap;
			if (next_snap->ps.pm_type < 7u)
			{
				int link_flags = next_snap->ps.linkFlags;
				if ((link_flags & 2) == 0 && (next_snap->ps.otherFlags & 4) == 0)
				{
					auto client_globals = cgame_glob;
					if (!client_globals->inKillCam || client_globals->killCamEntityType == game::KC_NO_ENTITY)
					{
						if (cg_thirdPerson && cg_thirdPerson->current.enabled)
						{
							return 1;
						}

						if (!(link_flags & (1 << 0xE)) || client_globals->killCamEntityType != game::KC_NO_ENTITY)
							return (link_flags >> 27) & 1;
						if (link_flags & (1 << 0x1D))
							return 0;
						if (!(link_flags & (1 << 0x1C)))
							return cgame_glob->spectatingThirdPerson;
					}
				}
			}
			return 1;
		}

		void cg_offset_third_person_view_internal_stub(int local_client_num, float third_person_angle, float third_person_range, 
			const int third_person_no_yaw, const int third_person_no_pitch,
			const int limit_pitch_angles, const int look_at_killer)
		{
			third_person_angle = cg_thirdPersonAngle->current.value;
			third_person_range = cg_thirdPersonRange->current.value;
			utils::hook::invoke<void>(0x10C280_b, local_client_num, third_person_angle, third_person_range, 
				third_person_no_yaw, third_person_no_pitch, limit_pitch_angles, look_at_killer);
		}
		
	}

	class component final : public component_interface
	{
	public:
		void post_unpack() override
		{
			if (!game::environment::is_mp())
			{
				return;
			}

			scheduler::once([]()
			{
				cg_thirdPerson = dvars::register_bool("cg_thirdPerson", 0, 4, "Use third person view");
				cg_thirdPersonAngle = dvars::register_float("cg_thirdPersonAngle", 356.0f, -180.0f, 360.0f, 4,
					"The angle of the camera from the player in third person view");
				cg_thirdPersonRange = dvars::register_float("cg_thirdPersonRange", 120.0f, 0.0f, 1024.0f, 4,
					"The range of the camera from the player in third person view");
			}, scheduler::main);

			utils::hook::jump(0x1D5950_b, update_third_person_stub);
			utils::hook::call(0x10C26B_b, cg_offset_third_person_view_internal_stub);
		}
	};
}

REGISTER_COMPONENT(thirdperson::component)
