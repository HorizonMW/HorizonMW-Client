#include <std_include.hpp>
#include "loader/component_loader.hpp"

#include "materials.hpp"
#include "console.hpp"
#include "filesystem.hpp"
#include "scheduler.hpp"

#include "game/game.hpp"
#include "game/dvars.hpp"

#include <utils/hook.hpp>
#include <utils/memory.hpp>
#include <utils/io.hpp>
#include <utils/string.hpp>
#include <utils/image.hpp>
#include <utils/concurrency.hpp>

namespace materials
{
	namespace
	{
		utils::hook::detour db_material_streaming_fail_hook;
		utils::hook::detour db_get_material_index_hook;
		utils::hook::detour cl_drawstretchpic_hook;
		utils::hook::detour CL_drawquadpicstperspective_hook;

		utils::hook::detour material_compare_hook;

#ifdef DEBUG
		const game::dvar_t* debug_materials = nullptr;
#endif

		game::MaterialConstantDef constant_table{};
		
		int db_material_streaming_fail_stub(game::Material* material)
		{
			if (material->constantTable == &constant_table)
			{
				return 0;
			}

			return db_material_streaming_fail_hook.invoke<int>(material);
		}

		unsigned int db_get_material_index_stub(game::Material* material)
		{
			if (material->constantTable == &constant_table)
			{
				return 0;
			}

			return db_get_material_index_hook.invoke<unsigned int>(material);
		}

		char material_compare_stub(unsigned int index_a, unsigned int index_b)
		{
			char result = 0;

			__try
			{
				result = material_compare_hook.invoke<char>(index_a, index_b);
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
#ifdef DEBUG
				const auto* material_a = utils::hook::invoke<game::Material*>(0x395FE0_b, index_a);
				const auto* material_b = utils::hook::invoke<game::Material*>(0x395FE0_b, index_b);
				console::error("Material_Compare: %s - %s (%d - %d)", 
					material_a->name, material_b->name, material_a->info.sortKey, material_b->info.sortKey);
#endif
			}

			return result;
		}

#ifdef DEBUG
		void print_material(const game::Material* material)
		{
			if (!debug_materials || !debug_materials->current.enabled)
			{
				return;
			}

			console::debug("current material is \"%s\"\n", material->name);
		}

		void print_current_material_stub(utils::hook::assembler& a)
		{
			const auto loc_6AD59B = a.newLabel();

			a.pushad64();
			a.mov(rcx, r15);
			a.call_aligned(print_material);
			a.popad64();

			a.cmp(byte_ptr(rbx), 5);
			a.mov(rax, ptr(r15, 0x130));

			a.jnz(loc_6AD59B);
			a.nop(dword_ptr(rax, rax, 0x00000000));

			a.jmp(0x6AD570_b);

			a.bind(loc_6AD59B);
			a.jmp(0x6AD59B_b);
		}
#endif

		bool is_scrambler_on()
		{
			auto omnvar_index = game::Omnvar_GetIndexByName("ui_uav_scrambler_on");
			auto omnvar_data = game::CG_Omnvar_GetData(0, omnvar_index);
			return omnvar_data->current.integer;
		}

		void cl_drawstretchpic_stub(const game::ScreenPlacement* a1, float a2, float a3, float a4, float a5, int a6, int a7, float s1, float t1, float s2, float t2, const float* color, game::Material* material)
		{
			if (utils::string::starts_with(material->info.name, "compass_map_"))
			{
				if (is_scrambler_on())
				{
					material = game::Material_RegisterHandle("compass_scrambled");
				}
			}

			cl_drawstretchpic_hook.invoke<void>(a1, a2, a3, a4, a5, a6, a7, s1, t1, s2, t2, color, material);
		}

		void CL_drawquadpicstperspective_stub(const game::ScreenPlacement* a1, const float(*a2)[4], float a3, float a4, float a5, float a6, const float* a7, game::Material* material)
		{
			if (utils::string::starts_with(material->info.name, "compass_map_"))
			{
				if (is_scrambler_on())
				{
					material = game::Material_RegisterHandle("compass_scrambled");
				}
			}

			CL_drawquadpicstperspective_hook.invoke<void>(a1, a2, a3, a4, a5, a6, a7, material);
		}

		utils::hook::detour CG_CompassDrawPlayerPointers_MP_hook;
		void CG_CompassDrawPlayerPointers_MP_Stub(int localClientNumber, int CompassType, void* rectDef1, void* rectDef2, game::Material* material, float const* a1, float const* a2)
		{
			if (is_scrambler_on())
				return;

			CG_CompassDrawPlayerPointers_MP_hook.invoke<void>(localClientNumber, CompassType, rectDef1, rectDef2, material, a1, a2);
		}

		utils::hook::detour CG_CompassDrawPlayer_hook;
		void CG_CompassDrawPlayer_Stub(int localClientNumber, int CompassType, void* rectDef1, void* rectDef2, game::Material* material, float const* a1)
		{
			if (is_scrambler_on())
				return;

			CG_CompassDrawPlayer_hook.invoke<void>(localClientNumber, CompassType, rectDef1, rectDef2, material, a1);
		}

		utils::hook::detour CG_CompassDrawFriendlies_hook;
		void CG_CompassDrawFriendlies_Stub(int localClientNumber, int CompassType, void* rectDef1, void* rectDef2, float const* a1, float const* a2)
		{
			if (is_scrambler_on())
				return;

			CG_CompassDrawFriendlies_hook.invoke<void>(localClientNumber, CompassType, rectDef1, rectDef2, a1, a2);
		}

		utils::hook::detour CG_CompassDrawEnemies_hook;
		void CG_CompassDrawEnemies_Stub(int localClientNumber, int CompassType, void* rectDef1, void* rectDef2, float const* a1, float const* a2)
		{
			if (is_scrambler_on())
				return;

			CG_CompassDrawEnemies_hook.invoke<void>(localClientNumber, CompassType, rectDef1, rectDef2, a1, a2);
		}

		utils::hook::detour Compass_TurretDraw_hook;
		void Compass_TurretDraw_Stub(int localClientNumber, int CompassType, void* rectDef1, void* rectDef2, float const* a1, float const* a2)
		{
			if (is_scrambler_on())
				return;

			Compass_TurretDraw_hook.invoke<void>(localClientNumber, CompassType, rectDef1, rectDef2, a1, a2);
		}

		utils::hook::detour CG_CompassDrawPlanes_hook;
		void CG_CompassDrawPlanes_Stub(int localClientNumber, int CompassType, void* rectDef1, void* rectDef2, float const* a1, float const* a2)
		{
			if (is_scrambler_on())
				return;

			CG_CompassDrawPlanes_hook.invoke<void>(localClientNumber, CompassType, rectDef1, rectDef2, a1, a2);
		}
	}

	bool setup_material_image(game::Material* material, const std::string& data)
	{
		if (*game::d3d11_device == nullptr)
		{
			console::error("Tried to create texture while d3d11 device isn't initialized\n");
			return false;
		}

		const auto image = material->textureTable->u.image;
		*(int*)&image->mapType = 0x1000003;
		*(int*)&image->picmip = -1;

		auto raw_image = utils::image{data};

		D3D11_SUBRESOURCE_DATA resource_data{};
		resource_data.SysMemPitch = raw_image.get_width() * 4;
		resource_data.SysMemSlicePitch = resource_data.SysMemPitch * raw_image.get_height();
		resource_data.pSysMem = raw_image.get_buffer();

		game::Image_Setup(image, raw_image.get_width(), raw_image.get_height(), image->depth, image->numElements,
			image->mapType, DXGI_FORMAT_R8G8B8A8_UNORM, image->name, &resource_data);
		return true;
	}

	game::Material* create_material(const std::string& name)
	{
		const auto white = game::Material_RegisterHandle("$white");
		const auto material = utils::memory::allocate<game::Material>();
		const auto texture_table = utils::memory::allocate<game::MaterialTextureDef>();
		const auto image = utils::memory::allocate<game::GfxImage>();

		std::memcpy(material, white, sizeof(game::Material));
		std::memcpy(texture_table, white->textureTable, sizeof(game::MaterialTextureDef));
		std::memcpy(image, white->textureTable->u.image, sizeof(game::GfxImage));

		material->constantTable = &constant_table;
		material->name = utils::memory::duplicate_string(name);
		image->name = material->name;

		image->texture.map = nullptr;
		image->texture.shaderView = nullptr;
		image->texture.shaderViewAlternate = nullptr;
		texture_table->u.image = image;

		material->textureTable = texture_table;

		return material;
	}

	void free_material(game::Material* material)
	{
		const auto try_release = []<typename T>(T** resource)
		{
			if (*resource != nullptr)
			{
				(*resource)->Release();
				*resource = nullptr;
			}
		};

		try_release(&material->textureTable->u.image->texture.map);
		try_release(&material->textureTable->u.image->texture.shaderView);
		try_release(&material->textureTable->u.image->texture.shaderViewAlternate);

		utils::memory::free(material->textureTable->u.image);
		utils::memory::free(material->textureTable);
		utils::memory::free(material->name);
		utils::memory::free(material);
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

			db_material_streaming_fail_hook.create(0x3A1600_b, db_material_streaming_fail_stub);
			db_get_material_index_hook.create(0x396000_b, db_get_material_index_stub);

			material_compare_hook.create(0x693B90_b, material_compare_stub);

#ifdef DEBUG
			utils::hook::jump(0x6AD55C_b, utils::hook::assemble(print_current_material_stub), true);

			scheduler::once([]
			{
				debug_materials = dvars::register_bool("debug_materials", false, game::DVAR_FLAG_NONE, "Print current material and images");
			}, scheduler::main);
#endif

			cl_drawstretchpic_hook.create(0x33B110_b, cl_drawstretchpic_stub); //used for map selectors
			CL_drawquadpicstperspective_hook.create(0x33AFE0_b, CL_drawquadpicstperspective_stub); //used for FULL minimap
			CG_CompassDrawPlayerPointers_MP_hook.create(0x2F9770_b, CG_CompassDrawPlayerPointers_MP_Stub);
			CG_CompassDrawPlayer_hook.create(0x2F7860_b, CG_CompassDrawPlayer_Stub);
			CG_CompassDrawFriendlies_hook.create(0x2FC610_b, CG_CompassDrawFriendlies_Stub);
			CG_CompassDrawEnemies_hook.create(0x2FBDC0_b, CG_CompassDrawEnemies_Stub);
			Compass_TurretDraw_hook.create(0x3003D0_b, Compass_TurretDraw_Stub);
			CG_CompassDrawPlanes_hook.create(0x2FD210_b, CG_CompassDrawPlanes_Stub);
		}
	};
}

REGISTER_COMPONENT(materials::component)
