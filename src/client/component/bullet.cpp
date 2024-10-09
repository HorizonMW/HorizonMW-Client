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
		game::dvar_t* bg_penetrate_all;
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

			// Patoke @todo: finish implementing
			//bg_penetrate_all = dvars::register_bool("bg_penetrateAll", false, flags,
			//	"Forces bullet to penetrate all surfaces");

			//// patch BG_GetSurfacePenetrationDepth call
			//utils::hook::jump(0x3F36C2_b, utils::hook::assemble([](utils::hook::assembler& a)
			//{
			//	const auto return_stub = a.newLabel();

			//	a.push(rax);
			//	a.mov(rax, qword_ptr(reinterpret_cast<int64_t>(&bg_penetrate_all)));
			//	a.mov(al, byte_ptr(rax, 0x10));
			//	a.cmp(al, 1);
			//	a.pop(rax);

			//	a.jz(return_stub);

			//	a.push(eax);
			//	a.mov(eax, FLT_MAX);
			//	a.movd(xmm7, eax);
			//	a.pop(eax);

			//	// skip whole penetration setup code
			//	a.jmp(0x3F372D_b);

			//	a.bind(return_stub);

			//	// original code
			//	a.test(rax, rax);
			//	a.jz(0x3F36E8_b);
			//	a.test(dword_ptr(rax, 0x1DC0), 0x80000);

			//	a.jmp(0x3F36D4_b);
			//}), true);

			//// patch number of bullet penetrations possible
			//utils::hook::jump(0x3F35CA_b, utils::hook::assemble([](utils::hook::assembler& a)
			//{
			//	const auto return_stub = a.newLabel();

			//	a.push(rax);
			//	a.mov(rax, qword_ptr(reinterpret_cast<int64_t>(&bg_penetrate_all)));
			//	a.mov(al, byte_ptr(rax, 0x10));
			//	a.cmp(al, 1);
			//	a.pop(rax);

			//	a.mov(ecx, 5);
			//	a.mov(ptr(rsp, 0x80), 5);

			//	a.jz(return_stub);

			//	a.mov(ecx, INT_MAX);
			//	a.mov(ptr(rsp, 0x80), INT_MAX);

			//	a.jmp(0x3F3608_b);

			//	a.bind(return_stub);

			//	// original code
			//	a.test(rax, rax);
			//	a.jz(0x3F35E3_b);

			//	a.jmp(0x3F35D7_b);
			//}), true);
		}
	};
}

REGISTER_COMPONENT(bullet::component)
