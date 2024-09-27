#include <std_include.hpp>
#include "loader/component_loader.hpp"

#include "game/game.hpp"
#include "game/dvars.hpp"

#include <utils/hook.hpp>

namespace bullet
{
	namespace
	{
		game::dvar_t* bg_surface_penetration;
		game::dvar_t* bg_fallback_max_range;
		utils::hook::detour bg_get_surface_penetration_depth_hook;

		float bg_get_surface_penetration_depth_stub(game::Weapon weapon, bool is_alternate, int surface_type)
		{
			if (bg_surface_penetration->current.value > 0.0f)
			{
				return bg_surface_penetration->current.value;
			}

			return bg_get_surface_penetration_depth_hook.invoke<float>(weapon, is_alternate, surface_type);
		}

		void bullet_fire_internal_stub(utils::hook::assembler& a)
		{
			a.push(rax);
			a.mov(rax, qword_ptr(reinterpret_cast<int64_t>(&bg_fallback_max_range)));
			// floats don't change order with little endianness, that's why we read at 0x20 instead of 0x10
			a.movss(xmm6, dword_ptr(rax, 0x20));
			a.pop(rax);

			a.mov(r15d, 1);

			a.jmp(0x3F400D_b);
		}
	}

	class component final : public component_interface
	{
	public:
		void post_unpack() override
		{
			auto flags = game::DVAR_FLAG_NONE | game::DVAR_FLAG_REPLICATED;
			if (!game::environment::is_dedi())
			{
				flags |= game::DVAR_FLAG_CHEAT;
			}

			bg_surface_penetration = dvars::register_float("bg_surfacePenetration", 0.0f, 0.0f, std::numeric_limits<float>::max(), flags,
				"Set to a value greater than 0 to override the bullet surface penetration depth");

			bg_get_surface_penetration_depth_hook.create(0x2E1110_b, &bg_get_surface_penetration_depth_stub);
			
			bg_fallback_max_range = dvars::register_float_hashed("bg_fallbackMaxRange", 262144.0f, 0.0f, std::numeric_limits<float>::max(), flags,
				"Modifies the max range for weapons without a defined damage range value (eg. Intervention)");

			utils::hook::jump(0x3F3FFF_b, utils::hook::assemble(bullet_fire_internal_stub), true);
		}
	};
}

REGISTER_COMPONENT(bullet::component)
