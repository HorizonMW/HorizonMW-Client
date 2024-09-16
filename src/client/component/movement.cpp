#include <std_include.hpp>
#include "loader/component_loader.hpp"

#include "console.hpp"
#include "gsc/script_extension.hpp"

#include "game/dvars.hpp"
#include "game/game.hpp"

#include <utils/hook.hpp>
#include <utils/vector.hpp>

// https://github.com/xoxor4d/iw3xo-dev/blob/develop/src/components/modules/movement.cpp :)

namespace movement
{
	enum
	{
		CF_BIT_NOCLIP = (1 << 0),
		CF_BIT_UFO = (1 << 1),
		CF_BIT_FROZEN = (1 << 2),
		CF_BIT_DISABLE_USABILITY = (1 << 3),
		CF_BIT_NO_KNOCKBACK = (1 << 4),
	};

	enum
	{
		PWF_RELOAD = 1 << 0,
		PWF_USING_OFFHAND = 1 << 1,
		PWF_HOLDING_BREATH = 1 << 2,
		PWF_FRIENDLY_FIRE = 1 << 3,
		PWF_ENEMY_FIRE = 1 << 4,
		PWF_NO_ADS = 1 << 5,
		PWF_USING_NIGHTVISION = 1 << 6,
		PWF_DISABLE_WEAPONS = 1 << 7,
		PWF_TRIGGER_LEFT_FIRE = 1 << 8,
		PWF_TRIGGER_DOUBLE_FIRE = 1 << 9,
		PWF_USING_RECOILSCALE = 1 << 10,
		PWF_DISABLE_WEAPON_SWAPPING = 1 << 11,
		PWF_DISABLE_OFFHAND_WEAPONS = 1 << 12,
		PWF_SWITCHING_TO_RIOTSHIELD = 1 << 13,
		// IW5 flags backported
		PWF_DISABLE_WEAPON_PICKUP = 1 << 16
	};

	namespace
	{
		utils::hook::detour pm_airmove_hook;
		utils::hook::detour pm_is_ads_allowed_hook;
		utils::hook::detour pm_weapon_process_hand_hook;
		utils::hook::detour pm_weapon_fire_weapon_hook;

		game::dvar_t* pm_cs_airAccelerate;
		game::dvar_t* pm_cs_airSpeedCap;
		game::dvar_t* pm_cs_strafing;

		void pm_air_accelerate(game::vec3_t wishdir, float wishspeed, game::playerState_s* ps, game::pml_t* pml)
		{
			float wishspd = wishspeed, accelspeed, currentspeed, addspeed;

			auto accel = pm_cs_airAccelerate->current.value;
			auto airspeedcap = pm_cs_airSpeedCap->current.value;

			if (wishspd > airspeedcap)
			{
				wishspd = airspeedcap;
			}

			currentspeed = utils::vector::product(ps->velocity, wishdir);
			addspeed = wishspd - currentspeed;

			if (addspeed > 0)
			{
				accelspeed = pml->frametime * accel * wishspeed * 1.0f;

				if (accelspeed > addspeed)
				{
					accelspeed = addspeed;
				}

				for (auto i = 0; i < 3; i++)
				{
					ps->velocity[i] += wishdir[i] * accelspeed;
				}
			}
		}

		void pm_clip_velocity(game::vec3_t in, game::vec3_t normal, game::vec3_t out, float overbounce)
		{
			float backoff, change, angle, adjust;

			angle = normal[2];
			backoff = utils::vector::product(in, normal) * overbounce;

			for (auto i = 0; i < 3; i++)
			{
				change = normal[i] * backoff;
				out[i] = in[i] - change;
			}

			adjust = utils::vector::product(out, normal);

			if (adjust < 0)
			{
				game::vec3_t reduce{};

				utils::vector::scale(normal, adjust, reduce);
				utils::vector::subtract(out, reduce, out);
			}
		}

		void pm_try_playermove(game::pmove_t* pm, game::pml_t* pml)
		{
			const auto surf_slope = 0.7f;
			const auto ps = pm->ps;

			game::vec3_t end{};
			game::trace_t trace{};

			if (utils::vector::length(ps->velocity) == 0)
			{
				return;
			}

			utils::vector::ma(ps->origin, pml->frametime, ps->velocity, end);
			utils::hook::invoke<void>(0x2D14C0_b, pm, &trace, ps->origin, end, 
				&pm->bounds, ps->clientNum, pm->tracemask); // PM_playerTrace

			if (trace.fraction == 1)
			{
				return;
			}

			if (trace.normal[2] > surf_slope)
			{
				return;
			}

			pm_clip_velocity(ps->velocity, trace.normal, ps->velocity, 1.0f);
		}

		void pm_airmove_stub(game::pmove_t* pm, game::pml_t* pml)
		{
			if (!pm_cs_strafing->current.enabled)
			{
				return pm_airmove_hook.invoke<void>(pm, pml);
			}

			const auto ps = pm->ps;

			ps->sprintState.sprintButtonUpRequired = 1;

			float fmove{}, smove{}, wishspeed{};
			game::vec3_t wishvel{}, wishdir{};

			fmove = pm->cmd.forwardmove;
			smove = pm->cmd.rightmove;

			pml->forward[2] = 0.0f;
			pml->right[2] = 0.0f;

			utils::vector::normalize(pml->forward);
			utils::vector::normalize(pml->right);

			for (auto i = 0; i < 2; i++)
			{
				wishvel[i] = pml->forward[i] * fmove + pml->right[i] * smove;
			}

			wishvel[2] = 0;

			utils::vector::copy(wishvel, wishdir);
			wishspeed = utils::vector::normalize(wishdir);

			if (wishspeed != 0 && (wishspeed > 320.0f))
			{
				utils::vector::scale(wishvel, 320.0f / wishspeed, wishvel);
				wishspeed = 320.0f;
			}

			pm_air_accelerate(wishdir, wishspeed, ps, pml);

			utils::hook::invoke<void>(0x2D3380_b, pm, pml, 1, 1); // PM_StepSlideMove

			pm_try_playermove(pm, pml);
		}

		utils::hook::detour begin_weapon_change_hook;
		void begin_weapon_change_stub(game::pmove_t* pm, game::Weapon new_weap, bool is_new_alt, bool quick, unsigned int* holdrand)
		{
			auto keep_anim = false;

			auto anim = pm->ps->weaponState[0x0].weapAnim;
			auto anim_two = pm->ps->weaponState[0x1].weapAnim;

			auto* ps = reinterpret_cast<game::playerState_s_SprintState*>(pm->ps);
			auto should_stall = (ps->sprintState.lastSprintStart > ps->sprintState.lastSprintEnd);
			if (should_stall)
			{
				keep_anim = true;
			}

			begin_weapon_change_hook.invoke<void>(pm, new_weap, is_new_alt, quick, holdrand);

			if (keep_anim)
			{
				pm->ps->weaponState[0x0].weapAnim = anim;
				pm->ps->weaponState[0x1].weapAnim = anim_two;
			}
		}

		inline bool is_previous_anim(int anim)
		{
			return (anim == 1 || anim == 31 || anim == 2049 || anim == 2079);
		}

		utils::hook::detour start_weapon_anim_hook;
		void start_weapon_anim_stub(int local_client_num, int a2, int hand, unsigned int next_anim, unsigned int anim, float transition_time)
		{
			auto* cg_array = reinterpret_cast<game::cg_s_ps*>(game::getCGArray());
			auto* playerstate = &cg_array[local_client_num].predictedPlayerState;

			auto should_sprint = (playerstate->sprintState.lastSprintStart < playerstate->sprintState.lastSprintEnd);

			if ((anim == 49 || anim == 52) && is_previous_anim(playerstate->weaponState[hand].weapAnim) && should_sprint)
			{
				anim = 44;
				transition_time = 0.5f;
			}

			start_weapon_anim_hook.invoke<void>(local_client_num, a2, hand, next_anim, anim, transition_time);
		}
	}

	class component final : public component_interface
	{
	public:
		void post_unpack() override
		{
			pm_airmove_hook.create(0x2C93B0_b, pm_airmove_stub);

			// fix moveSpeedScale not being used
			utils::hook::set<uint32_t>(0x4406FE_b, 0x1DC);

			pm_cs_airAccelerate = dvars::register_float("pm_cs_airAccelerate", 100.0f, 1.0f, 500.0f,
				game::DVAR_FLAG_REPLICATED | game::DVAR_FLAG_CHEAT,
				"Defines player acceleration mid-air");

			pm_cs_airSpeedCap = dvars::register_float("pm_cs_airSpeedCap", 30.0f, 1.0f, 500.0f,
				game::DVAR_FLAG_REPLICATED | game::DVAR_FLAG_CHEAT,
				"Maximum speed mid-air");

			pm_cs_strafing = dvars::register_bool("pm_cs_strafing", false,
				game::DVAR_FLAG_REPLICATED | game::DVAR_FLAG_CHEAT,
				"Enable CS like strafing");

			// solitude mechanics, makes things a bit more fluid
			begin_weapon_change_hook.create(0x2D57E0_b, begin_weapon_change_stub);

			// glides (thank you @girlmachinery for the help on this)
			start_weapon_anim_hook.create(0x1D5CA0_b, start_weapon_anim_stub);

			// force_play_weap_anim(anim_id, both_hands)
			gsc::method::add("force_play_weap_anim", [](const game::scr_entref_t ent, const gsc::function_args& args)
			{
				if (ent.classnum != 0)
				{
					throw std::runtime_error("invalid entity");
				}

				auto* client = game::g_entities[ent.entnum].client;
				if (client == nullptr)
				{
					throw std::runtime_error("not a player entity");
				}

				auto anim_id = args[0].as<int>();

				client->weaponState[0].weapAnim = anim_id;
				if (args[1].as<int>())
					client->weaponState[1].weapAnim = anim_id;

				return scripting::script_value{};
			});
		}
	};
}

REGISTER_COMPONENT(movement::component)
