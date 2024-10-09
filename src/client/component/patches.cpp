#include <std_include.hpp>
#include "loader/component_loader.hpp"

#include "dvars.hpp"
#include "fastfiles.hpp"
#include "version.h"
#include "command.hpp"
#include "console.hpp"
#include "network.hpp"
#include "scheduler.hpp"
#include "filesystem.hpp"
#include "menus.hpp"

#include "game/game.hpp"
#include "game/dvars.hpp"

#include <utils/io.hpp>
#include <utils/hook.hpp>
#include <utils/string.hpp>
#include <utils/flags.hpp>

namespace patches
{
	namespace
	{
		const char* live_get_local_client_name()
		{
			return game::Dvar_FindVar("name")->current.string;
		}

		utils::hook::detour sv_kick_client_num_hook;

		void sv_kick_client_num(const int client_num, const char* reason)
		{
			// Don't kick bot to equalize team balance.
			if (reason == "EXE_PLAYERKICKED_BOT_BALANCE"s)
			{
				return;
			}
			return sv_kick_client_num_hook.invoke<void>(client_num, reason);
		}

		std::string get_login_username()
		{
			char username[UNLEN + 1];
			DWORD username_len = UNLEN + 1;
			if (!GetUserNameA(username, &username_len))
			{
				return "Unknown Soldier";
			}

			return std::string{username, username_len - 1};
		}

		utils::hook::detour com_register_dvars_hook;

		void com_register_dvars_stub()
		{
			if (game::environment::is_mp())
			{
				// Make name save
				dvars::register_string("name", get_login_username().data(), game::DVAR_FLAG_SAVED, "Player name.");
			}

			return com_register_dvars_hook.invoke<void>();
		}

		utils::hook::detour cg_set_client_dvar_from_server_hook;

		void cg_set_client_dvar_from_server_stub(void* clientNum, void* cgameGlob, const char* dvar_hash, const char* value)
		{
			const auto hash = std::atoi(dvar_hash);
			auto* dvar = game::Dvar_FindMalleableVar(hash);

			if (hash == game::generateHashValue("cg_fov") || 
				hash == game::generateHashValue("cg_fovMin") || 
				hash == game::generateHashValue("cg_fovScale"))
			{
				return;
			}

			if (hash == game::generateHashValue("g_scriptMainMenu"))
			{
				menus::set_script_main_menu(value);
			}

			// register new dvar
			if (!dvar)
			{
				game::Dvar_RegisterString(hash, "", value, game::DVAR_FLAG_EXTERNAL);
				return;
			}

			// only set if dvar has no flags or has cheat flag or has external flag
			if (dvar->flags == game::DVAR_FLAG_NONE || 
				(dvar->flags & game::DVAR_FLAG_CHEAT) != 0 || 
				(dvar->flags & game::DVAR_FLAG_EXTERNAL) != 0)
			{
				game::Dvar_SetFromStringFromSource(dvar, value, game::DvarSetSource::DVAR_SOURCE_EXTERNAL);
			}

			// original code
			int index = 0;
			auto result = utils::hook::invoke<bool>(0x4745E0_b, dvar, &index); // NetConstStrings_SV_GetNetworkDvarIndex
			if (result)
			{
				std::string index_str = std::to_string(index);
				return cg_set_client_dvar_from_server_hook.invoke<void>(clientNum, cgameGlob, index_str.data(), value);
			}
		}

		game::dvar_t* get_client_dvar(const char* name)
		{
			game::dvar_t* dvar = game::Dvar_FindVar(name);
			if (!dvar)
			{
				static game::dvar_t dummy{0};
				dummy.hash = game::generateHashValue(name);
				return &dummy;
			}
			return dvar;
		}

		bool get_client_dvar_hash(game::dvar_t* dvar, int* hash)
		{
			*hash = dvar->hash;
			return true;
		}

		const char* db_read_raw_file_stub(const char* filename, char* buf, const int size)
		{
			std::string file_name = filename;
			if (file_name.find(".cfg") == std::string::npos)
			{
				file_name.append(".cfg");
			}

			std::string buffer{};
			if (filesystem::read_file(file_name, &buffer))
			{
				snprintf(buf, size, "%s\n", buffer.data());
				return buf;
			}

			// DB_ReadRawFile
			return utils::hook::invoke<const char*>(0x3994B0_b, filename, buf, size);
		}

		void bsp_sys_error_stub(const char* error, const char* arg1)
		{
			if (game::environment::is_dedi())
			{
				game::Sys_Error(error, arg1);
			}
			else
			{
				scheduler::once([]()
				{
					command::execute("reconnect");
				}, scheduler::pipeline::main, 1s);
				game::Com_Error(game::ERR_DROP, error, arg1);
			}
		}

		utils::hook::detour cmd_lui_notify_server_hook;
		void cmd_lui_notify_server_stub(game::gentity_s* ent)
		{
			const auto svs_clients = *game::svs_clients;
			if (svs_clients == nullptr)
			{
				return;
			}

			command::params_sv params{};
			const auto menu_id = atoi(params.get(1));
			const auto client = &svs_clients[ent->s.number];

			// 13 => change class
			if (menu_id == 13 && ent->client->team == game::TEAM_SPECTATOR)
			{
				return;
			}

			// 32 => "end_game"
			if (menu_id == 32 && client->header.remoteAddress.type != game::NA_LOOPBACK)
			{
				game::SV_DropClient_Internal(client, "PLATFORM_STEAM_KICK_CHEAT", true);
				return;
			}

			cmd_lui_notify_server_hook.invoke<void>(ent);
		}

		void sv_execute_client_message_stub(game::client_t* client, game::msg_t* msg)
		{
			if ((client->reliableSequence - client->reliableAcknowledge) < 0)
			{
				client->reliableAcknowledge = client->reliableSequence;
				console::info("Negative reliableAcknowledge from %s - cl->reliableSequence is %i, reliableAcknowledge is %i\n",
					client->name, client->reliableSequence, client->reliableAcknowledge);
				network::send(client->header.remoteAddress, "error", "EXE_LOSTRELIABLECOMMANDS", '\n');
				return;
			}

			utils::hook::invoke<void>(0x54EC50_b, client, msg);
		}

		void aim_assist_add_to_target_list(void* aaGlob, void* screenTarget)
		{
			if (!dvars::aimassist_enabled->current.enabled)
			{
				return;
			}

			game::AimAssist_AddToTargetList(aaGlob, screenTarget);
		}

		void missing_content_error_stub(int, const char*)
		{
			game::Com_Error(game::ERR_DROP, utils::string::va("MISSING FILE\n%s.ff",
				fastfiles::get_current_fastfile().data()));
		}

		utils::hook::detour init_network_dvars_hook;
		void init_network_dvars_stub(game::dvar_t* dvar)
		{
			constexpr auto hash = dvars::generate_hash("r_tonemapHighlightRange");
			if (dvar->hash == hash)
			{
				init_network_dvars_hook.invoke<void>(dvar);
			}
		}

		int ui_draw_crosshair()
		{
			return 1;
		}

		utils::hook::detour cl_gamepad_scrolling_buttons_hook;
		void cl_gamepad_scrolling_buttons_stub(int local_client_num, int a2)
		{
			if (local_client_num <= 3)
			{
				cl_gamepad_scrolling_buttons_hook.invoke<void>(local_client_num, a2);
			}
		}

#define GET_FORMATTED_BUFFER() \
		char buffer[2048]; \
		{ \
			va_list ap; \
			va_start(ap, fmt); \
			vsnprintf_s(buffer, sizeof(buffer), _TRUNCATE, fmt, ap); \
			va_end(ap); \
		}

		void create_2d_texture_stub_1(const char* fmt, ...)
		{
			fmt = "Create2DTexture( %s, %i, %i, %i, %i ) failed\n\n"
				"Disable shader caching, lower graphic settings, free up RAM, or update your GPU drivers.";

			GET_FORMATTED_BUFFER()

			game::Sys_Error("%s", buffer);
		}

		void create_2d_texture_stub_2(game::errorParm code, const char* fmt, ...)
		{
			fmt = "Create2DTexture( %s, %i, %i, %i, %i ) failed\n\n"
				"Disable shader caching, lower graphic settings, free up RAM, or update your GPU drivers.";

			GET_FORMATTED_BUFFER()

			game::Com_Error(code, "%s", buffer);
		}

		void swap_chain_stub(game::errorParm code, const char* fmt, ...)
		{
			fmt = "IDXGISwapChain::Present failed: %s\n\n"
				"Disable shader caching, lower graphic settings, free up RAM, or update your GPU drivers.";

			GET_FORMATTED_BUFFER()

			game::Com_Error(code, "%s", buffer);
		}

		void dvar_set_bool(game::dvar_t* dvar, bool value)
		{
			game::dvar_value dvar_value{};
			dvar_value.enabled = value;
			game::Dvar_SetVariant(dvar, &dvar_value, game::DVAR_SOURCE_INTERNAL);
		}

		void sub_157FA0_stub()
		{
			const auto dvar_706663C2 = *reinterpret_cast<game::dvar_t**>(0x3426B90_b);
			const auto dvar_617FB3B4 = *reinterpret_cast<game::dvar_t**>(0x3426BA0_b);

			if (!dvar_706663C2->current.enabled || utils::hook::invoke<bool>(0x15B2F0_b))
			{
				utils::hook::invoke<void>(0x17D8D0_b, 0, 0);
				dvar_set_bool(dvar_706663C2, true);
				dvar_set_bool(dvar_617FB3B4, true);
			}

			if (utils::hook::invoke<bool>(0x5B7AB0_b))
			{
				utils::hook::invoke<void>(0x17D8D0_b, 0, 0);
				dvar_set_bool(dvar_617FB3B4, true);
			}
		}

		game::dvar_t* r_warn_once_per_frame = nullptr;
		void warn_once_per_frame_stub(__int64 a1, int a2, int a3, __int64 a4)
		{
#ifdef DEBUG
			if (r_warn_once_per_frame && r_warn_once_per_frame->current.enabled)
			{
				utils::hook::invoke<void>(0x5AF170_b, a1, a2, a3, a4);
			}
#endif
		}

		template<size_t N>
		bool try_load_all_zones(std::array<std::string, N> zones)
		{
			auto* info = utils::memory::get_allocator()->allocate_array<game::XZoneInfo>(zones.size());
			const auto _ = gsl::finally([&]() 
			{
				utils::memory::get_allocator()->free(info);
			});
			
			int counter = 0;
			for (auto& zone : zones)
			{
				if (fastfiles::exists(zone))
				{
					info[counter].name = zone.data();
					info[counter].allocFlags = game::DB_ZONE_COMMON | game::DB_ZONE_CUSTOM;
					info[counter].freeFlags = 0;
					counter++;
				}
				else
				{
					console::error("Couldn't find zone %s\n", zone.data());
				}
			}

			if (counter <= 0)
				return false;

			game::DB_LoadXAssets(info, counter, game::DBSyncMode::DB_LOAD_ASYNC_NO_SYNC_THREADS);
			return true;
		}
		
		void* ui_init_stub()
		{
			std::array<std::string, 7> zones
			{
				"h2m_killstreak",
				"h2m_attachments",
				"h2m_ar1",
				"h2m_smg",
				"h2m_shotgun",
				"h2m_launcher",
				"h2m_rangers"
			};

			try_load_all_zones(zones);

			return utils::hook::invoke<void*>(0x2A5540_b);
		}
	}

	class component final : public component_interface
	{
	public:
		void post_start() override
		{
			// replace virtualLobbyMap dvar with trainer
			if (utils::io::file_exists("zone/trainer.ff") || utils::io::file_exists("trainer.ff"))
			{
				dvars::override::register_string("virtualLobbyMap", "trainer", game::DVAR_FLAG_READ);
			}
		}

		void post_unpack() override
		{
			// Register dvars
			com_register_dvars_hook.create(0x15BB60_b, &com_register_dvars_stub);

			// Unlock fps in main menu
			utils::hook::set<BYTE>(0x34396B_b, 0xEB);

			if (!game::environment::is_dedi())
			{
				// Fix mouse lag
				utils::hook::nop(0x5BFF89_b, 6);
				scheduler::loop([]()
				{
					SetThreadExecutionState(ES_DISPLAY_REQUIRED);
				}, scheduler::pipeline::main);
			}

			// Set compassSize dvar minimum to 0.1
			dvars::override::register_float("compassSize", 1.0f, 0.1f, 50.0f, game::DVAR_FLAG_SAVED);

			// Make cg_fov and cg_fovscale saved dvars
			dvars::override::register_float("cg_fov", 65.f, 40.f, 200.f, game::DvarFlags::DVAR_FLAG_SAVED);
			dvars::override::register_float("cg_fovScale", 1.f, 0.1f, 2.f, game::DvarFlags::DVAR_FLAG_SAVED);
			dvars::override::register_float("cg_fovMin", 1.f, 1.0f, 90.f, game::DvarFlags::DVAR_FLAG_SAVED);

			// Enable Marketing Comms
			dvars::override::register_int("marketing_active", 1, 1, 1, game::DVAR_FLAG_WRITE);

			// Makes com_maxfps saved dvar
			if (game::environment::is_dedi())
			{
				dvars::override::register_int("com_maxfps", 85, 0, 100, game::DVAR_FLAG_NONE);
			}
			else
			{
				dvars::override::register_int("com_maxfps", 0, 0, 1000, game::DVAR_FLAG_SAVED);
			}

			// Makes mis_cheat saved dvar
			dvars::override::register_bool("mis_cheat", 0, game::DVAR_FLAG_SAVED);

			// Fix speaker config bug
			dvars::override::register_int("snd_detectedSpeakerConfig", 0, 0, 100, 0);

#ifdef DEBUG
			// Allow kbam input when gamepad is enabled (dev only)
			utils::hook::nop(0x135EFB_b, 2);
			utils::hook::nop(0x13388F_b, 6);
#endif

			// Show missing fastfiles
			utils::hook::call(0x39A78E_b, missing_content_error_stub);

			// Allow executing custom cfg files with the "exec" command
			utils::hook::call(0x156D41_b, db_read_raw_file_stub);

			// Remove useless information from errors + add additional help to common errors
			utils::hook::call(0x681A69_b, create_2d_texture_stub_1); 	// Sys_Error for "Create2DTexture( %s, %i, %i, %i, %i ) failed"
			utils::hook::call(0x681C1B_b, create_2d_texture_stub_2); 	// Com_Error for ^
			utils::hook::call(0x6CB1BC_b, swap_chain_stub); 			// Com_Error for "IDXGISwapChain::Present failed: %s"

			// Uncheat protect gamepad-related dvars
			dvars::override::register_float("gpad_button_deadzone", 0.13f, 0, 1, game::DVAR_FLAG_SAVED);
			dvars::override::register_float("gpad_stick_deadzone_min", 0.2f, 0, 1, game::DVAR_FLAG_SAVED);
			dvars::override::register_float("gpad_stick_deadzone_max", 0.01f, 0, 1, game::DVAR_FLAG_SAVED);
			dvars::override::register_float("gpad_stick_pressed", 0.4f, 0, 1, game::DVAR_FLAG_SAVED);
			dvars::override::register_float("gpad_stick_pressed_hysteresis", 0.1f, 0, 1, game::DVAR_FLAG_SAVED);

			// Disable r_preloadShaders
			dvars::override::register_bool("r_preloadShaders", false, game::DVAR_FLAG_READ);

			// Disable r_preloadShadersFrontendAllow
			dvars::override::register_bool("r_preloadShadersFrontendAllow", false, game::DVAR_FLAG_READ);

			patch_mp();
		}

		static void patch_mp()
		{
			// fix vid_restart crash
			utils::hook::set<uint8_t>(0x139680_b, 0xC3);

			utils::hook::jump(0x5BB9C0_b, &live_get_local_client_name);

			// Disable data validation error popup
			dvars::override::register_int("data_validation_allow_drop", 0, 0, 0, game::DVAR_FLAG_NONE);

			// Patch SV_KickClientNum
			sv_kick_client_num_hook.create(game::SV_KickClientNum, &sv_kick_client_num);

			// block changing name in-game
			utils::hook::set<uint8_t>(0x54CFF0_b, 0xC3);

			// client side aim assist dvar
			dvars::aimassist_enabled = dvars::register_bool("aimassist_enabled", true,
				game::DvarFlags::DVAR_FLAG_SAVED,
				"Enables aim assist for controllers");
			utils::hook::call(0xE857F_b, aim_assist_add_to_target_list);

			// patch "Couldn't find the bsp for this map." error to not be fatal in mp
			utils::hook::call(0x39465B_b, bsp_sys_error_stub);

#ifdef DEBUG
			// isProfanity
			utils::hook::set(0x361AA0_b, 0xC3C033);
#endif

			// disable elite_clan
			dvars::override::register_int("elite_clan_active", 0, 0, 0, game::DVAR_FLAG_NONE);
			utils::hook::set<uint8_t>(0x62D2F0_b, 0xC3); // don't register commands

			// disable codPointStore
			dvars::override::register_int("codPointStore_enabled", 0, 0, 0, game::DVAR_FLAG_NONE);

			// don't register every replicated dvar as a network dvar (only r_tonemapHighlightRange, fixes red dots)
			init_network_dvars_hook.create(0x4740C0_b, init_network_dvars_stub);

			// patch "Server is different version" to show the server client version
			utils::hook::inject(0x54DCE5_b, VERSION);

			// prevent servers overriding our fov
			utils::hook::nop(0x17DA96_b, 0x16);
			utils::hook::nop(0xE00BE_b, 0x17);
			utils::hook::nop(0x307F90_b, 0x5); // don't change cg_fov when toggling third person spectating

			// make setclientdvar behave like older games
			cg_set_client_dvar_from_server_hook.create(0x11AA90_b, cg_set_client_dvar_from_server_stub);
			utils::hook::call(0x407EC5_b, get_client_dvar_hash); // setclientdvar
			utils::hook::call(0x4087C1_b, get_client_dvar_hash); // setclientdvars
			utils::hook::call(0x407E8E_b, get_client_dvar); // setclientdvar
			utils::hook::call(0x40878A_b, get_client_dvar); // setclientdvars
			utils::hook::set<uint8_t>(0x407EB6_b, 0xEB); // setclientdvar
			utils::hook::set<uint8_t>(0x4087B2_b, 0xEB); // setclientdvars

			// some [data validation] anti tamper thing that kills performance
			dvars::override::register_int("dvl", 0, 0, 0, game::DVAR_FLAG_READ);

			// unlock safeArea_* (h2m-mod values)
			utils::hook::jump(0x347BC5_b, 0x347BD3_b);
			utils::hook::jump(0x347BEC_b, 0x347C17_b);

			const auto safe_area_value = 0.97f;
			dvars::override::register_float("safeArea_adjusted_horizontal", safe_area_value, 0.0f, 1.0f, game::DVAR_FLAG_SAVED);
			dvars::override::register_float("safeArea_adjusted_vertical", safe_area_value, 0.0f, 1.0f, game::DVAR_FLAG_SAVED);
			//dvars::override::register_float("safeArea_horizontal", safe_area_value, 0.0f, 1.0f, game::DVAR_FLAG_SAVED);
			//dvars::override::register_float("safeArea_vertical", safe_area_value, 0.0f, 1.0f, game::DVAR_FLAG_SAVED);

			// allow servers to check for new packages more often
			dvars::override::register_int("sv_network_fps", 1000, 20, 1000, game::DVAR_FLAG_SAVED);

			// Massively increate timeouts
			dvars::override::register_int("cl_timeout", 90, 90, 1800, game::DVAR_FLAG_NONE); // Seems unused
			dvars::override::register_int("sv_timeout", 90, 90, 1800, game::DVAR_FLAG_NONE); // 30 - 0 - 1800
			dvars::override::register_int("cl_connectTimeout", 120, 120, 1800, game::DVAR_FLAG_NONE); // Seems unused
			dvars::override::register_int("sv_connectTimeout", 120, 120, 1800, game::DVAR_FLAG_NONE); // 60 - 0 - 1800

			dvars::register_int("scr_game_spectatetype", 1, 0, 99, game::DVAR_FLAG_REPLICATED, "");

			dvars::override::register_bool("ui_drawCrosshair", true, game::DVAR_FLAG_WRITE);
			utils::hook::jump(0x1E6010_b, ui_draw_crosshair);

			// Prevent clients from ending the game as non host by sending 'end_game' lui notification
			cmd_lui_notify_server_hook.create(0x412D50_b, cmd_lui_notify_server_stub);

			// Prevent clients from sending invalid reliableAcknowledge
			utils::hook::call(0x1CBD06_b, sv_execute_client_message_stub);

			// Change default hostname and make it replicated
			dvars::override::register_string("sv_hostname", "^5H2M-Mod^7 Default Server", game::DVAR_FLAG_REPLICATED);

			// Dont free server/client memory on asset loading (fixes crashing on map rotation)
			utils::hook::nop(0x132474_b, 5);

			// Fix gamepad related crash
			cl_gamepad_scrolling_buttons_hook.create(0x133210_b, cl_gamepad_scrolling_buttons_stub);

			// Prevent the game from modifying Windows microphone volume (since voice chat isn't used)
			//utils::hook::set<uint8_t>(0x5BEEA0_b, 0xC3); // Mixer_SetWaveInRecordLevels

			// Fix 'out of memory' error
			utils::hook::call(0x15C7EE_b, sub_157FA0_stub);

			utils::hook::set<uint8_t>(0x556250_b, 0xC3); // disable host migration

			/*
			
				H2M-Mod patches below here
			
			*/
			// change names of window name + stat files for h2m
			utils::hook::copy_string(0x926210_b, "H2M-Mod");	// window name
			utils::hook::copy_string(0x929168_b, "H2M-Mod");	// mulitbyte string (window too?)
			utils::hook::copy_string(0x91F464_b, "h2mdta");		// mpdata
			utils::hook::copy_string(0x91F458_b, "h2mcdta");	// commondata

			// overrides of lighting dvars to make it script-controlled instead (and replicated to server -> client)
			dvars::override::register_bool("r_drawsun", 0, game::DVAR_FLAG_NONE | game::DVAR_FLAG_REPLICATED);
			dvars::override::register_bool("r_colorscaleusetweaks", 0, game::DVAR_FLAG_NONE | game::DVAR_FLAG_REPLICATED);
			dvars::override::register_bool("r_primarylightusetweaks", 0, game::DVAR_FLAG_NONE | game::DVAR_FLAG_REPLICATED);
			dvars::override::register_bool("r_veilusetweaks", 0, game::DVAR_FLAG_NONE | game::DVAR_FLAG_REPLICATED);
			dvars::override::register_bool("r_viewmodelprimarylightusetweaks", 0, game::DVAR_FLAG_NONE | game::DVAR_FLAG_REPLICATED);
			dvars::override::register_float("r_diffusecolorscale", 0.0f, 0.0f, 10.0f, game::DVAR_FLAG_NONE | game::DVAR_FLAG_REPLICATED);
			dvars::override::register_float("r_specularcolorscale", 0.0f, 0.0f, 25.0f, game::DVAR_FLAG_NONE | game::DVAR_FLAG_REPLICATED);
			dvars::override::register_float("r_primarylighttweakdiffusestrength", 0.0f, 0.0f, 100.0f, game::DVAR_FLAG_NONE | game::DVAR_FLAG_REPLICATED);
			dvars::override::register_float("r_primarylighttweakspecularstrength", 0.0f, 0.0f, 100.0f, game::DVAR_FLAG_NONE | game::DVAR_FLAG_REPLICATED);
			dvars::override::register_float("r_viewmodelprimarylighttweakdiffusestrength", 0.0f, 0.0f, 10.0f, game::DVAR_FLAG_NONE | game::DVAR_FLAG_REPLICATED);
			dvars::override::register_float("r_viewmodelprimarylighttweakdspecularstrength", 0.0f, 0.0f, 10.0f, game::DVAR_FLAG_NONE | game::DVAR_FLAG_REPLICATED);
			dvars::override::register_int("r_smodelinstancedthreshold", 2, 0, 128, game::DVAR_FLAG_NONE | game::DVAR_FLAG_REPLICATED);
			dvars::override::register_float("r_viewModelPrimaryLightTweakSpecularStrength", 1.0f, 0.0f, 10.0f, game::DVAR_FLAG_NONE | game::DVAR_FLAG_REPLICATED);
			dvars::override::register_bool("r_veil", 0, game::DVAR_FLAG_NONE | game::DVAR_FLAG_REPLICATED);
			dvars::override::register_float("r_veilStrength", 0.086999997f, -10.0f, 10.0f, game::DVAR_FLAG_NONE | game::DVAR_FLAG_REPLICATED);
			dvars::override::register_float("r_veilBackgroundStrength", 0.91299999f, -10.0f, 10.0f, game::DVAR_FLAG_NONE | game::DVAR_FLAG_REPLICATED);

			utils::hook::set<byte>(0x67D187_b, 0x8);
			dvars::override::register_bool("r_fog", 1, game::DVAR_FLAG_NONE | game::DVAR_FLAG_REPLICATED);
			dvars::override::register_float("r_lodBiasRigid", 0.0f, -2000.0f, 0.0f, game::DVAR_FLAG_NONE | game::DVAR_FLAG_REPLICATED);
			dvars::override::register_float("r_lodBiasSkinned", 0.0f, -2000.0f, 0.0f, game::DVAR_FLAG_NONE | game::DVAR_FLAG_REPLICATED);

			dvars::override::register_int("legacySpawningEnabled", 0, 0, 0, game::DVAR_FLAG_READ);

			// stop spam in console
			utils::hook::call(0x6BBB81_b, warn_once_per_frame_stub);
#ifdef DEBUG
			r_warn_once_per_frame = dvars::register_bool("r_warnOncePerFrame", false, game::DVAR_FLAG_SAVED, "Print warnings from R_WarnOncePerFrame");
#endif

			// stop even more spam
			utils::hook::nop(0x26D99F_b, 5);

			// change startup loading animation
			utils::hook::copy_string(0x8E31D8_b, "h2_loading_animation");

			utils::hook::set<byte>(0xC393F_b, 11); // allow setting cac in game

			utils::hook::call(0x15CDB2_b, ui_init_stub);

			// use default font for overhead names
			dvars::override::register_int("cg_overheadNamesFont", 6, 0, 6, game::DVAR_FLAG_CHEAT);
			dvars::override::register_float("cg_overheadNamesSize", 0.5f, 0.0f, 100.0f, game::DVAR_FLAG_CHEAT);

			utils::hook::set<float>(0x8FBA04_b, 350.0f); // move server loading text up to 350.0f instead of 439.0f

			utils::hook::set<byte>(0x53C9FA_b, 0xEB);

			// stop dynents sound spam (its not even used at all in IW4, so who cares)
			dvars::override::register_bool("dynent_active", false, game::DVAR_FLAG_READ);
			dvars::override::register_float("dynEnt_playerWakeUpRadius", 0.0f, 0.0f, 0.0f, game::DVAR_FLAG_READ);

			utils::hook::set<byte>(0xF8339_b, 4); // render 4 chars for overheadname rank

			// unprotect draw2D and drawGun
			dvars::override::register_bool("cg_draw2D", true, game::DVAR_FLAG_NONE);
			dvars::override::register_bool("cg_drawGun", true, game::DVAR_FLAG_NONE);

			dvars::register_int("scr_war_score_kill", 100, 1, 500, game::DVAR_FLAG_NONE, "Team Deathmatch XP Cap");
			dvars::register_int("scr_dm_score_kill", 50, 1, 500, game::DVAR_FLAG_NONE, "Free-For-All XP Cap");
			dvars::register_int("scr_dom_score_kill", 100, 1, 500, game::DVAR_FLAG_NONE, "Domination XP Cap");
			dvars::register_int("scr_sd_score_kill", 500, 1, 500, game::DVAR_FLAG_NONE, "Search and Destroy XP Cap");
			dvars::register_int("scr_ctf_score_kill", 100, 1, 500, game::DVAR_FLAG_NONE, "Capture the Flag XP Cap");
			dvars::register_int("scr_gun_score_kill", 100, 1, 500, game::DVAR_FLAG_NONE, "Gun Game XP Cap");
			dvars::register_int("scr_hp_score_kill", 100, 1, 500, game::DVAR_FLAG_NONE, "Hardpoint XP Cap");
			dvars::register_int("scr_conf_score_kill", 50, 1, 500, game::DVAR_FLAG_NONE, "Kill Confirmed XP Cap");
			dvars::register_int("scr_dd_score_kill", 100, 1, 500, game::DVAR_FLAG_NONE, "Demolition XP Cap");
			dvars::register_int("scr_sab_score_kill", 100, 1, 500, game::DVAR_FLAG_NONE, "Sabotage XP Cap");
			dvars::register_int("scr_koth_score_kill", 100, 1, 500, game::DVAR_FLAG_NONE, "Headquarters XP Cap");
		}
	};
}

REGISTER_COMPONENT(patches::component)
