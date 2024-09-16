#include <std_include.hpp>
#include "loader/component_loader.hpp"

#include "dvars.hpp"
#include "console.hpp"
#include "scheduler.hpp"
#include "game/game.hpp"
#include "game/dvars.hpp"
#include "game/scripting/execution.hpp"
#include <utils/hook.hpp>


namespace renderer
{
#define font game::R_RegisterFont("fonts/fira_mono_regular.ttf", 25)

	namespace
	{
		utils::hook::detour r_init_draw_method_hook;
		utils::hook::detour r_update_front_end_dvar_options_hook;

		utils::hook::detour db_load_xassets_hook;

		game::dvar_t* r_red_dot_brightness_scale;
		game::dvar_t* r_use_custom_red_dot_brightness;
		float tonemap_highlight_range = 16.f;

#ifdef DEBUG
		game::dvar_t* r_drawModelNames;
		game::dvar_t* r_playerDrawDebugDistance;
#endif

		std::unordered_map<std::string, float> tonemap_highlight_range_overrides =
		{
			// all these are 16 by default (except mp_bog & mp_cargoship which have it at 0) which makes red dots hard to see

			// H1
			{"mp_convoy", 22.f},
			{"mp_backlot", 20.f},
			{"mp_bog", 12.f},
			{"mp_bloc", 30.f},
			{"mp_countdown", 20.f},
			{"mp_crash", 20.f},
			{"mp_creek", 20.f},
			{"mp_crossfire", 26.f},
			{"mp_citystreets", 30.f},
			{"mp_farm", 20.f},
			{"mp_overgrown", 22.f},
			{"mp_pipeline", 26.f},
			{"mp_shipment", 20.f},
			{"mp_showdown", 24.f},
			{"mp_strike", 24.f},
			{"mp_vacant", 20.f},
			{"mp_cargoship", 14.f},
			{"mp_crash_snow", 24.f},
			{"mp_bog_summer", 24.f},

			// H2
			{"airport", 16.f},
			{"boneyard", 15.f},
			{"cliffhanger", 17.f},
			{"contingency", 19.f},
			{"dcburning", 7.f},
			{"dc_whitehouse", 8.f},
			{"estate", 13.f},
			{"gulag", 15.f},
			{"oilrig", 16.f},

			// IW4
			{"mp_abandon", 16.f},
			{"mp_afghan", 16.f},
			{"mp_boneyard", 13.f},
			{"mp_brecourt", 14.f},
			{"mp_checkpoint", 15.f},
			{"mp_compact", 55.f}, // snow map, doesn't work
			{"mp_complex", 17.f},
			{"mp_derail", 55.f}, // snow map, doesn't work
			{"mp_estate", 16.f},
			{"mp_favela", 16.f},
			{"mp_fuel2", 13.f},
			{"mp_highrise", 9.f},
			{"mp_invasion", 12.f},
			{"mp_nightshift", 19.f},
			{"mp_quarry", 15.f},
			{"mp_rundown", 16.f},
			{"mp_rust", 10.f},
			{"mp_storm", 13.f},
			{"mp_subbase", 55.f}, // snow map, doesn't work
			{"mp_terminal", 12.f},
			{"mp_trailerpark", 8.f},
			{"mp_underpass", 15.f},

		};

		std::vector<std::string> use_colortweaks_maps = {
			{"mp_abandon"},
			{"mp_afghan"},
			{"mp_boneyard"},
			{"mp_brecourt"},
			{"mp_checkpoint"},
			{"mp_compact"},
			{"mp_complex"},
			{"mp_derail"},
			{"mp_estate"},
			{"mp_favela"},
			{"mp_fuel2"},
			{"mp_highrise"},
			{"mp_invasion"},
			{"mp_nightshift"},
			{"mp_quarry"},
			{"mp_rundown"},
			{"mp_rust"},
			{"mp_storm"},
			{"mp_subbase"},
			{"mp_terminal"},
			{"mp_trailerpark"},
			{"mp_underpass"}
		};

		int get_fullbright_technique()
		{
			switch (dvars::r_fullbright->current.integer)
			{
			case 2:
				return 13;
			default:
				return game::TECHNIQUE_UNLIT;
			}
		}

		void gfxdrawmethod()
		{
			game::gfxDrawMethod->drawScene = game::GFX_DRAW_SCENE_STANDARD;
			game::gfxDrawMethod->baseTechType = dvars::r_fullbright->current.enabled ? get_fullbright_technique() : game::TECHNIQUE_LIT;
			game::gfxDrawMethod->emissiveTechType = dvars::r_fullbright->current.enabled ? get_fullbright_technique() : game::TECHNIQUE_EMISSIVE;
			game::gfxDrawMethod->forceTechType = dvars::r_fullbright->current.enabled ? get_fullbright_technique() : 242;
		}

		void r_init_draw_method_stub()
		{
			gfxdrawmethod();
		}

		bool r_update_front_end_dvar_options_stub()
		{
			if (dvars::r_fullbright->modified)
			{
				game::Dvar_ClearModified(dvars::r_fullbright);
				game::R_SyncRenderThread();

				gfxdrawmethod();
			}

			return r_update_front_end_dvar_options_hook.invoke<bool>();
		}

		void set_tonemap_highlight_range()
		{
			auto* mapname = game::Dvar_FindVar("mapname");
			if (mapname != nullptr && tonemap_highlight_range_overrides.find(mapname->current.string)
				!= tonemap_highlight_range_overrides.end())
			{
				tonemap_highlight_range = tonemap_highlight_range_overrides[mapname->current.string];
			}
			else
			{
				tonemap_highlight_range = 16.f;
			}
		}

		void set_colorscale_use_tweaks()
		{
			auto* mapname = game::Dvar_FindVar("mapname");
			if (mapname != nullptr && std::find(use_colortweaks_maps.begin(), use_colortweaks_maps.end(), mapname->current.string) != use_colortweaks_maps.end())
			{
				game::Dvar_FindVar("r_colorscaleusetweaks")->current.enabled = true;
			}
			else
			{
				game::Dvar_FindVar("r_colorscaleusetweaks")->current.enabled = false;
			}
		}

		void db_load_xassets_stub(void* a1, void* a2, void* a3)
		{
			set_tonemap_highlight_range();
			scheduler::once([]
			{
				set_colorscale_use_tweaks();
			}, scheduler::main);

			db_load_xassets_hook.invoke<void>(a1, a2, a3);
		}

		int get_red_dot_brightness()
		{
			auto value = r_red_dot_brightness_scale->current.value;
			return *reinterpret_cast<int*>(r_use_custom_red_dot_brightness->current.enabled ? &value : &tonemap_highlight_range);
		}

		void* get_tonemap_highlight_range_stub()
		{
			return utils::hook::assemble([](utils::hook::assembler& a)
			{
				a.push(r9);
				a.push(rax);
				a.pushad64();
				a.call_aligned(get_red_dot_brightness);
				a.mov(qword_ptr(rsp, 0x80), rax);
				a.popad64();
				a.pop(rax);
				a.pop(r9);

				a.mov(dword_ptr(r9, 0x1E84), eax);

				a.jmp(0x1C4136_b);
			});
		}

		void r_preload_shaders_stub(utils::hook::assembler& a)
		{
			const auto is_zero = a.newLabel();

			a.mov(rax, qword_ptr(0x111DC230_b));
			a.test(rax, rax);
			a.jz(is_zero);

			a.mov(rcx, qword_ptr(rax, 0x540C68));
			a.jmp(0x6E76FF_b);

			a.bind(is_zero);
			a.jmp(0x6E7722_b);
		}

#ifdef DEBUG
		void VectorSubtract(const float va[3], const float vb[3], float out[3])
		{
			out[0] = va[0] - vb[0];
			out[1] = va[1] - vb[1];
			out[2] = va[2] - vb[2];
		}

		float Vec3SqrDistance(const float v1[3], const float v2[3])
		{
			float out[3];

			VectorSubtract(v2, v1, out);

			return (out[0] * out[0]) + (out[1] * out[1]) + (out[2] * out[2]);
		}

		void debug_draw_model_names()
		{
			if (!r_drawModelNames || r_drawModelNames->current.integer == 0)
				return;

			auto player = *game::playerState;
			float playerPosition[3]{ player->origin[0], player->origin[1], player->origin[2] };

			auto mapname = game::Dvar_FindVar("mapname");
			auto gfxAsset = game::DB_FindXAssetHeader(game::XAssetType::ASSET_TYPE_GFX_MAP, utils::string::va("maps/mp/%s.d3dbsp", mapname->current.string), 0).gfxWorld;
			if (gfxAsset == nullptr)
			{
				gfxAsset = game::DB_FindXAssetHeader(game::XAssetType::ASSET_TYPE_GFX_MAP, utils::string::va("maps/%s.d3dbsp", mapname->current.string), 0).gfxWorld;
				if (gfxAsset == nullptr)
				{
					return;
				}
			}

			auto distance = r_playerDrawDebugDistance->current.integer;
			auto sqrDist = distance * static_cast<float>(distance);

			float staticModelsColor[4] = { 1.0f, 0.0f, 1.0f, 1.0f };
			float sceneModelsColor[4] = { 1.0f, 1.0f, 0.0f, 1.0f };
			float dobjsColor[4] = { 0.0f, 1.0f, 1.0f, 1.0f };
			auto scene = *game::scene;

			switch (r_drawModelNames->current.integer)
			{
			case 1:
				for (auto i = 0; i < scene.sceneModelCount; i++)
				{
					if (!scene.sceneModel[i].model)
						continue;

					if (Vec3SqrDistance(playerPosition, scene.sceneModel[i].placement.base.origin) < static_cast<float>(sqrDist))
					{
						auto screenPlace = game::ScrPlace_GetActivePlacement();
						game::vec2_t screen{};
						if (game::CG_WorldPosToScreenPosReal(0, screenPlace, scene.sceneModel[i].placement.base.origin, screen))
						{
							game::R_AddCmdDrawText(scene.sceneModel[i].model->name, 0x7FFFFFFF, font, screen[0], screen[1], 1.f, 1.f, 0.0f, sceneModelsColor, 6);
						}
					}
				}
				break;
			case 2:
				for (size_t i = 0; i < gfxAsset->dpvs.smodelCount; i++)
				{
					auto staticModel = gfxAsset->dpvs.smodelDrawInsts[i];
					if (staticModel.model)
					{
						const auto dist = Vec3SqrDistance(playerPosition, staticModel.placement.origin);
						if (dist < static_cast<float>(sqrDist))
						{

							auto screenPlace = game::ScrPlace_GetActivePlacement();
							game::vec2_t screen{};
							if (game::CG_WorldPosToScreenPosReal(0, screenPlace, staticModel.placement.origin, screen))
							{
								game::R_AddCmdDrawText(staticModel.model->name, 0x7FFFFFFF, font, screen[0], screen[1], 1.f, 1.f, 0.0f, staticModelsColor, 6);
							}
						}
					}
				}
				break;
			}
		}
#endif
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

#ifdef DEBUG
			dvars::r_fullbright = dvars::register_int("r_fullbright", 0, 0, 2, game::DVAR_FLAG_SAVED, "Toggles rendering without lighting");
#else
			dvars::r_fullbright = dvars::register_int("r_fullbright", 0, 0, 0, game::DVAR_FLAG_READ, "");
#endif

			r_init_draw_method_hook.create(0x669580_b, &r_init_draw_method_stub);
			r_update_front_end_dvar_options_hook.create(0x6A78C0_b, &r_update_front_end_dvar_options_stub);

			// use "saved" flags
			dvars::override::register_enum("r_normalMap", game::DVAR_FLAG_SAVED);
			dvars::override::register_enum("r_specularMap", game::DVAR_FLAG_SAVED);
			dvars::override::register_enum("r_specOccMap", game::DVAR_FLAG_SAVED);

			if (game::environment::is_mp())
			{
				// Adjust red dot brightness
				utils::hook::jump(0x1C4125_b, get_tonemap_highlight_range_stub(), true);
				db_load_xassets_hook.create(0x397500_b, db_load_xassets_stub);

				r_red_dot_brightness_scale = dvars::register_float("r_redDotBrightness", 1.f, 0.1f, 55.f, game::DVAR_FLAG_CHEAT, "Adjust red-dot tonemap highlighting");
				r_use_custom_red_dot_brightness = dvars::register_bool("r_useCustomRedDotBrightness", false, game::DVAR_FLAG_CHEAT, "Use custom red-dot tonemap highlighting");
			}

			// patch r_preloadShaders crash at init
			utils::hook::jump(0x6E76F1_b, utils::hook::assemble(r_preload_shaders_stub), true);
			dvars::override::register_bool("r_preloadShaders", false, game::DVAR_FLAG_SAVED);

#ifdef DEBUG
			r_drawModelNames = dvars::register_int("r_drawModelNames", 0, 0, 2, game::DVAR_FLAG_CHEAT, "Draw all model names");
			r_playerDrawDebugDistance = dvars::register_int("r_drawDebugDistance", 1000, 0, 50000, game::DVAR_FLAG_SAVED, "r_draw debug functions draw distance relative to the player");

			scheduler::loop([]
			{
				static const auto* in_firing_range = game::Dvar_FindVar("virtualLobbyInFiringRange");
				if (!in_firing_range)
				{
					return;
				}

				if ( game::CL_IsCgameInitialized() || (game::VirtualLobby_Loaded() && in_firing_range->current.enabled) )
				{
					debug_draw_model_names();
				}
			}, scheduler::renderer);
#endif
		}
	};
}

REGISTER_COMPONENT(renderer::component)
