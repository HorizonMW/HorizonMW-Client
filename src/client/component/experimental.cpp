#include <std_include.hpp>
#include "loader/component_loader.hpp"

#include "command.hpp"
#include "console.hpp"
#include "dvars.hpp"
#include "filesystem.hpp"
#include "network.hpp"
#include "scheduler.hpp"

#include "gsc/script_extension.hpp" 

#include "game/game.hpp"
#include "game/dvars.hpp"

#include "scripting.hpp"

#include <utils/hook.hpp>
#include "fastfiles.hpp"
#include "console.hpp"
#include <utils/io.hpp>
#include <bitset>

#include <voice/hmw_voice_chat.hpp>

namespace experimental
{
	game::dvar_t* sv_open_menu_mapvote = nullptr;
#ifdef DEBUG
	game::dvar_t* cg_draw_material = nullptr;
#endif
	namespace 
	{
		void open_lui_mapvote() 
		{
			if (sv_open_menu_mapvote && sv_open_menu_mapvote->current.enabled)
			{
				command::execute("lui_open menu_mapvote");
			}
		}

		utils::hook::detour bg_customization_get_model_name_hook;

		char* bg_customization_get_model_name_stub(game::CustomizationType type, unsigned short modelIndex)
		{
			auto* current_model = bg_customization_get_model_name_hook.invoke<char*>(type, modelIndex);

			const auto* env_mod = game::GameInfo_GetCurrentMapCustom("envmod");
			if (!env_mod || !*env_mod)
			{
				env_mod = "none";
			}

			const auto costume_model_table = game::DB_FindXAssetHeader(game::ASSET_TYPE_STRINGTABLE, "mp/costumemodeltable.csv", false).stringTable;
			if (costume_model_table == nullptr)
			{
				return current_model;
			}

			// if the environment isn't none, its most likely a wet environment and can use the wetmodel
			if (strcmp(env_mod, "none"))
			{
				if (type == game::CustomizationType::SHIRT || type == game::CustomizationType::HEAD)
				{
					auto wet_model = game::StringTable_Lookup(costume_model_table, 0, current_model, 18);
					if (wet_model && *wet_model != '\0')
					{
						return wet_model;
					}
				}
			}

			return current_model;
		}

		struct game::WeaponDef* BG_GetWeaponDef(unsigned int weaponIndex)
		{
			return *(&game::bg_weaponDefs)[weaponIndex];
		}

		utils::hook::detour missile_trajectory_controlled_hook;
		void missile_trajectory_controlled_stub(game::gentity_s* entity, game::vec3_t* result)
		{
			entity->clipmask &= 0xFFFFF7FF;

			game::vec3_t dirOrig{};
			game::AngleVectors(&entity->currentAngles, &dirOrig, nullptr, nullptr);

			auto missile_speed = (dirOrig[0] * entity->trDelta[0]) + (dirOrig[1] * entity->trDelta[1]) + (dirOrig[2] * entity->trDelta[2]);

			auto entity_handle = entity->owner;
			auto owner_entity = game::g_entities[entity_handle - 1];
			if (!owner_entity.client)
			{
				missile_trajectory_controlled_hook.invoke<void>(entity, result);
				return;
			}

			auto is_boosting = ((owner_entity.client->sess.cmd.buttons & 0x1) != 0);

			if (!is_boosting && _bittest((long*)&entity->missile_flags, 9u)) //(entity->missile_flags & 0x200)
			{
				missile_speed = 750.0f;
			}
			else if (is_boosting && !(entity->missile_flags & 4)) //(entity->missile_flags & 1)
			{
				entity->missile_flags |= 0x4; // set the boost flag so this code doesn't run again
				missile_speed = missile_speed * 2.5f;
			}
			else
			{
				if ((3000.0f - missile_speed) <= 0.001f)
				{
					if (missile_speed < 3000.0f)
					{
						missile_speed = fmaxf(3000.0f, missile_speed - 25.0f);
					}
				}
				else
				{
					missile_speed = fminf(3000.0f, missile_speed + 100.0f);
				}
			}

			entity->trDelta[0] = dirOrig[0] * missile_speed;
			entity->trDelta[1] = dirOrig[1] * missile_speed;
			entity->trDelta[2] = dirOrig[2] * missile_speed;

			game::vec3_t base{};
			game::Trajectory_GetTrBase(&entity->_padding[0x5E], &base);

			(*result)[0] = (entity->trDelta[0] * 0.050000001f) + base[0];
			(*result)[1] = (entity->trDelta[1] * 0.050000001f) + base[1];
			(*result)[2] = (entity->trDelta[2] * 0.050000001f) + base[2];

			game::Trajectory_SetTrBase(&entity->_padding[0x5E], result);

			memset(&base, 0, sizeof(base));
		}

		utils::hook::detour CG_GetFootstepVolumeScale_Detour;
		//float CG_GetFootstepVolumeScale_Stub(int localClientNum, __int64 cent, __int64 aliasList)
		//{
		//	auto perk_footstepVolumeEnemy = game::Dvar_FindVar("perk_footstepVolumeEnemy");

		//	float result;
		//	if (aliasList == 2 || aliasList == 3 || aliasList == 4 || (result = aliasList - 2, (aliasList - 2) <= 2))
		//	{		
		//		result = game::sub_14045E280(cent);
		//		if (result && (*(DWORD*)(result + 7612) & 0x2000000) != 0)
		//			result = perk_footstepVolumeEnemy->current.value;
		//	}

		//	return result;

		//	//return CG_GetFootstepVolumeScale_Detour.invoke<int>(localClientNum, cent, aliasList);
		//}
		float __fastcall CG_GetFootstepVolumeScale_Stub(int localClientNum, game::fake_centity_s* cent, __int64 aliasList)
		{
			float finalSoundVolume = 1.0;
			float real_sound_vol = 0;
			game::fake_entlist* localClient{};
			game::fake_entlist* otherEnt{};

			auto perk_footstepVolumeEnemy = game::Dvar_FindVar("perk_footstepVolumeEnemy");

			switch (aliasList)
			{
			case 2:
				real_sound_vol = cent->alias1; //*(float*)(cent + 3736);
			LABEL_9:
				if (real_sound_vol != 0.0)
					finalSoundVolume = real_sound_vol;
				goto LABEL_11;
			case 3:
				real_sound_vol = cent->alias2; //*(float*)(cent + 3740);
				goto LABEL_9;
			case 4:
				real_sound_vol = cent->alias3; //*(float*)(cent + 3744);
				goto LABEL_9;
			}
			if ((aliasList - 2) > 2)
				return 1.0;
		LABEL_11:
			localClient = game::sub_14045E280(*&localClientNum);
			otherEnt = game::sub_14045E280(*&cent->number);
			if (localClient && (localClient->perk & 0x1000) != 0)
				finalSoundVolume = finalSoundVolume * 0.0625f;
			if (otherEnt)
			{
				if ((otherEnt->perk & 0x2000000) != 0)
					finalSoundVolume = finalSoundVolume * (perk_footstepVolumeEnemy->current.value * perk_footstepVolumeEnemy->current.value);
			}

			console::info("Playing Sound Volume %.f\n", finalSoundVolume);
			return finalSoundVolume;
		}

		inline bool has_commando_perk(game::fake_entlist* entity)
		{
			/*
			auto response = utils::hook::invoke<unsigned int>(0x2C6270_b, "specialty_extendmelee");
			auto has_perk = (entity->perk == response);
			return has_perk;
			*/
			return false;
		}

		void commando_lunge_stub(utils::hook::assembler& a)
		{
			const auto has_commando = a.newLabel();
			const auto charging_3_jump = a.newLabel();

			a.cmp(dword_ptr(rbx, 0x3B8), 1); // 7
			a.setz(r12b);		// 4
			a.xor_(edi, edi);	// 2

			a.pushad64();
			a.mov(rcx, rbx); // ps
			a.call_aligned(has_commando_perk);
			a.test(al, al);
			a.jnz(has_commando); // Jump if not zero
			a.popad64();

			a.jmp(0x2DBA60_b); // just do normal code if we dont have commando since we dont cover the 12 bytes after

			a.bind(has_commando);
			a.popad64();
			a.test(dword_ptr(rbx, 0x54), 0x10000);
			a.jnz(charging_3_jump);
			a.jmp(0x2DBA69_b); // back to game

			a.bind(charging_3_jump);
			a.jmp(0x2DBAD3_b);
		}
#ifdef DEBUG
		float distance_2d(float* a, float* b)
		{
			return sqrt((a[0] - b[0]) * (a[0] - b[0]) + (a[1] - b[1]) * (a[1] - b[1]));
		}

		float distance_3d(float* a, float* b)
		{
			return sqrt((a[0] - b[0]) * (a[0] - b[0]) + (a[1] - b[1]) * (a[1] - b[1]) + (a[2] - b[2]) * (a[2] - b[2]));
		}

		constexpr auto EPSILON = std::numeric_limits<float>::epsilon();

		// Calculates the cross product of two 3D vectors
		void crossProduct3D(float v1[3], float v2[3], float result[3])
		{
			result[0] = v1[1] * v2[2] - v1[2] * v2[1];
			result[1] = v1[2] * v2[0] - v1[0] * v2[2];
			result[2] = v1[0] * v2[1] - v1[1] * v2[0];
		}

		// Normalizes a 3D vector
		void normalize3D(float v[3])
		{
			float length = sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
			v[0] /= length;
			v[1] /= length;
			v[2] /= length;
		}

		// Calculates the dot product of two 3D vectors
		float dotProduct3D(float v1[3], float v2[3])
		{
			return v1[0] * v2[0] + v1[1] * v2[1] + v1[2] * v2[2];
		}

		// Calculates the distance from a point to a plane defined by a point on the plane and the plane's normal vector
		float distancePointToPlane(float planePoint[3], float normal[3], float point[3])
		{
			float dist = 0.0f;
			for (int i = 0; i < 3; i++) {
				dist += (point[i] - planePoint[i]) * normal[i];
			}
			return dist;
		}

		// Calculates the barycentric coordinates of a point in a triangle defined by three 3D points
		void calculateBarycentricCoordinates(float p[3][3], float point[3], float& alpha, float& beta, float& gamma)
		{
			float v0[3], v1[3], v2[3];
			for (int i = 0; i < 3; i++) {
				v0[i] = p[2][i] - p[0][i];
				v1[i] = p[1][i] - p[0][i];
				v2[i] = point[i] - p[0][i];
			}
			float dot00 = dotProduct3D(v0, v0);
			float dot01 = dotProduct3D(v0, v1);
			float dot02 = dotProduct3D(v0, v2);
			float dot11 = dotProduct3D(v1, v1);
			float dot12 = dotProduct3D(v1, v2);
			float invDenom = 1.0f / (dot00 * dot11 - dot01 * dot01);
			beta = (dot11 * dot02 - dot01 * dot12) * invDenom;
			gamma = (dot00 * dot12 - dot01 * dot02) * invDenom;
			alpha = 1.0f - beta - gamma;
		}

		// Calculates the normal vector of a triangle defined by three 3D points
		void calculateTriangleNormal(float p[3][3], float normal[3])
		{
			float v1[3], v2[3];
			for (int i = 0; i < 3; i++) {
				v1[i] = p[1][i] - p[0][i];
				v2[i] = p[2][i] - p[0][i];
			}
			crossProduct3D(v1, v2, normal);
			normalize3D(normal);
		}

		bool lineTriangleIntersection(float p[3][3], float linePoint[3], float lineDir[3])
		{
			// Calculate the normal vector of the triangle
			float normal[3];
			calculateTriangleNormal(p, normal);

			// Calculate the dot product of the normal vector and the line direction vector
			float dotProduct = dotProduct3D(normal, lineDir);

			// If the dot product is close to zero, the line is parallel to the triangle and does not intersect
			if (fabs(dotProduct) < EPSILON) {
				return false;
			}

			// Calculate the distance from the line point to the plane of the triangle
			float distance = distancePointToPlane(p[0], normal, linePoint);

			// If the distance is zero or the sign of the distance is different than the sign of the dot product, the line does not intersect the triangle
			if (fabs(distance) < EPSILON || (distance > 0 && dotProduct > 0) || (distance < 0 && dotProduct < 0)) {
				return false;
			}

			// Calculate the intersection point of the line and the plane of the triangle
			float t = -distance / dotProduct;
			float intersection[3];
			intersection[0] = linePoint[0] + t * lineDir[0];
			intersection[1] = linePoint[1] + t * lineDir[1];
			intersection[2] = linePoint[2] + t * lineDir[2];

			// Calculate the barycentric coordinates of the intersection point
			float alpha, beta, gamma;
			calculateBarycentricCoordinates(p, intersection, alpha, beta, gamma);

			// If the barycentric coordinates are all greater than or equal to zero, the intersection point is inside the triangle and the line intersects the triangle
			if (alpha >= 0.0f && beta >= 0.0f && gamma >= 0.0f) {
				return true;
			}

			// Otherwise, the line does not intersect the triangle
			return false;
		}

		void getCenterPoint(float p[3][3], float center[3]) {
			for (int i = 0; i < 3; i++) {
				center[i] = (p[0][i] + p[1][i] + p[2][i]) / 3.0f;
			}
		}

		int frames_passed = 0; // 20 then resets to 0
		std::optional<std::string> current_material;

		void render_draw_material()
		{
			static const auto* sv_running = game::Dvar_FindVar("sv_running");
			if (!sv_running || !sv_running->current.enabled)
			{
				return;
			}

			static const auto* cg_draw2d = game::Dvar_FindVar("cg_draw2D");
			if (cg_draw2d && !cg_draw2d->current.enabled)
			{
				return;
			}

			if (!cg_draw_material || !cg_draw_material->current.integer)
			{
				return;
			}

			static const auto* gfx_map = *reinterpret_cast<game::GfxWorld**>(0xE973AE0_b);
			if (gfx_map == nullptr)
			{
				return;
			}

			const auto placement = game::ScrPlace_GetViewPlacement();
			static const float text_color[4] = { 1.0f, 1.0f, 1.0f, 1.0f };

			game::rectDef_s rect{};
			rect.x = 0.f;
			rect.y = 0.f;
			rect.w = 500.f;
			rect.horzAlign = 1;
			rect.vertAlign = 0;

			game::rectDef_s text_rect{};

			static const auto font = game::R_RegisterFont("fonts/fira_mono_regular.ttf", 22);
			if (!font)
			{
				return;
			}

			/*
			if (frames_passed > 5)
			{
				frames_passed = 0;
				current_material = {};
			}
			else
			{
				frames_passed = frames_passed + 1;
			}

			if (current_material.has_value())
			{
				game::UI_DrawWrappedText(placement, current_material.value().data(), &rect, font,
					8.0, 240.0f, 0.2f, text_color, 0, 0, &text_rect, 0);
				return;
			}
			*/

			game::vec3_t origin{};
			const auto client = game::g_entities[0].client;
			const auto angles = client->ps.viewangles;
			utils::hook::invoke<void>(0x4057F0_b, client, origin); // G_GetPlayerViewOrigin

			game::vec3_t forward{};
			utils::hook::invoke<void>(0x59C600_b, angles, forward, nullptr, nullptr); // AngleVectors

			float min_distance = -1.f;
			game::vec3_t target_center{};
			game::vec3_t target_triangle[3]{};
			game::GfxSurface* target_surface = nullptr;

			for (auto i = 0u; i < gfx_map->surfaceCount; i++)
			{
				const auto surface = &gfx_map->dpvs.surfaces[i];
				const auto indices = &gfx_map->draw.indices[surface->tris.baseIndex];
				auto too_far = false;
				for (auto o = 0; o < surface->tris.triCount; o++)
				{
					game::vec3_t triangle[3]{};
					for (auto j = 0; j < 3; j++)
					{
						const auto index = indices[o * 3 + j] + surface->tris.firstVertex;
						const auto vertex = &gfx_map->draw.vd.vertices[index];
						if (distance_2d(vertex->xyz, origin) > 1000.f)
						{
							too_far = true;
							break;
						}
						triangle[j][0] = vertex->xyz[0];
						triangle[j][1] = vertex->xyz[1];
						triangle[j][2] = vertex->xyz[2];
					}

					if (too_far)
					{
						break;
					}

					if (lineTriangleIntersection(triangle, origin, forward))
					{
						game::vec3_t center{};
						getCenterPoint(triangle, center);
						const auto dist = distance_3d(center, origin);
						if (dist < min_distance || min_distance == -1.f)
						{
							min_distance = dist;
							target_surface = surface;
							std::memcpy(&target_triangle, &triangle, sizeof(game::vec3_t[3]));
							std::memcpy(&target_center, &center, sizeof(game::vec3_t));
						}
					}
				}
			}

			if (min_distance == -1.f)
			{
				return;
			}

			auto* material = target_surface->material;
			if (!material || !material->name)
			{
				return;
			}
			auto material_name = material->name;

			auto techniqueset_name = "^1null^7";
			if (material->techniqueSet && material->techniqueSet->name)
			{
				techniqueset_name = utils::string::va("^3%s^7", material->techniqueSet->name);
			}

			auto text = utils::string::va("%s (%s)",
				material_name,
				techniqueset_name);
			current_material = text;

			game::UI_DrawWrappedText(placement, text, &rect, font,
				8.0, 240.0f, 0.2f, text_color, 0, 0, &text_rect, 0);
		}

		utils::hook::detour cg_draw2d_hook;
		void cg_draw2d_stub(int local_client_num)
		{
			cg_draw2d_hook.invoke<void>(local_client_num);

			if (game::CL_IsCgameInitialized() && !game::VirtualLobby_Loaded())
			{
				render_draw_material();
			}
		}
#endif
	}

	class component final : public component_interface
	{
	public:
		void post_unpack() override
		{
			// use "wetmodel" for wet environments in costumemodeltable.csv
			bg_customization_get_model_name_hook.create(0x62890_b, bg_customization_get_model_name_stub);

			// fix static model's lighting going black sometimes
			//dvars::override::register_int("r_smodelInstancedThreshold", 0, 0, 128, 0x0);

			// change minimum cap to -2000 instead of -1000 (culling issue)
			dvars::override::register_float("r_lodBiasRigid", 0, -2000, 0, game::DVAR_FLAG_SAVED);
		
			dvars::override::register_float("r_lodBiasSkinned", 0, -2000, 0, game::DVAR_FLAG_REPLICATED);
			dvars::override::register_int("legacySpawningEnabled", 0, 0, 0, game::DVAR_FLAG_READ);

			// stop game from outputting once per frame warnings
			//utils::hook::call(0x6BBB81_b, empty_func);
			utils::hook::copy(0x6BBB81_b, "\xC2\x00\x00", 3); // equivalent

			utils::hook::nop(0x26D99F_b, 5); // stop unk call (probably for server host?)
			utils::hook::copy_string(0x8E31D8_b, "h2_loading_animation"); // swap loading icon
			utils::hook::set<uint8_t>(0xC393F_b, 11); // increment persistent player data clcState check

			utils::hook::set<float>(0x8FBA04_b, 350.f); // modify position of loading information
			utils::hook::set<uint8_t>(0x53C9FA_b, 0xEB); // Patoke @todo: what is this?

			dvars::override::register_bool("dynent_active", false, game::DVAR_FLAG_READ);
			dvars::override::register_float("dynEnt_playerWakeUpRadius", 0, 0, 0, game::DVAR_FLAG_REPLICATED);
		
			utils::hook::set<uint8_t>(0xF8339_b, 4); // change max overhead rank text size 
			
			// Hacky way of opening lui menu_mapvote
			sv_open_menu_mapvote = dvars::register_bool("sv_open_menu_mapvote", false, game::DVAR_FLAG_EXTERNAL, "Open mapvote lui");
			scheduler::loop(open_lui_mapvote, scheduler::pipeline::lui, 100ms);

			/*
				voice experiments
			*/
			hmw_voice_chat::setup_hooks();

			// add predator missile boosting
			missile_trajectory_controlled_hook.create(0x42AAE0_b, missile_trajectory_controlled_stub);

			/* Ninja Perk */
			//CG_GetFootstepVolumeScale_Detour.create(0x3E5440_b, CG_GetFootstepVolumeScale_Stub);
			dvars::register_float("perk_footstepVolumeQuietPlayer", 0.25f, 0.0f, 3.4f, game::DVAR_FLAG_CHEAT, "Volume of player footstep sounds with 'quiet move' perk");
			dvars::register_float("perk_footstepVolumeQuietNPC", 0.25f, 0.0f, 3.4f, game::DVAR_FLAG_CHEAT, "Volume of NPC footstep sounds with 'quiet move' perk");

			//utils::hook::jump(0x2DBA53_b, utils::hook::assemble(commando_lunge_stub), true);
#ifdef DEBUG
			cg_draw_material = dvars::register_bool("cg_drawMaterial", false, game::DVAR_FLAG_NONE, "Draws material name on screen");
			cg_draw2d_hook.create(0xF57D0_b, cg_draw2d_stub);
#endif
		}
	};
}

REGISTER_COMPONENT(experimental::component)
