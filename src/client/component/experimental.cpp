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

	namespace 
	{
		bool load_zone_files(const std::vector<std::string>& zone_files) 
		{
			game::XZoneInfo zone_allocs[7] = {};
			int files_to_load = 0;

			for (const auto& zone_file : zone_files)
			{
				if (fastfiles::exists(zone_file)) 
				{
					auto& zone = zone_allocs[files_to_load];

					zone.name = zone_file.data();
					zone.allocFlags = game::DB_ZONE_COMMON | game::DB_ZONE_CUSTOM;
					zone.freeFlags = 0;

					files_to_load++;
				}
				else
				{
					console::print(1, "Couldn't find zone %s\n", zone_file.data());
				}
			}

			if (files_to_load == 0)
				return false;
			
			game::DB_LoadXAssets(zone_allocs, files_to_load, game::DBSyncMode::DB_LOAD_ASYNC_NO_SYNC_THREADS);

			return true;
		}

		void load_h2m_zones()
		{
			std::vector<std::string> new_zone_files = {};

			new_zone_files.emplace_back("h2m_killstreak");
			new_zone_files.emplace_back("h2m_attachments");
			new_zone_files.emplace_back("h2m_ar1");
			new_zone_files.emplace_back("h2m_smg");
			new_zone_files.emplace_back("h2m_shotgun");
			new_zone_files.emplace_back("h2m_launcher");
			new_zone_files.emplace_back("h2m_rangers");

			load_zone_files(new_zone_files);
		}

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

			// Marketing_Init stub
			utils::hook::call(0x15CDB2_b, load_h2m_zones);

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
		}
	};
}

REGISTER_COMPONENT(experimental::component)
