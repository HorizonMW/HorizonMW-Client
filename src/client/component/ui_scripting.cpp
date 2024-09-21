#include <std_include.hpp>
#include "loader/component_loader.hpp"

#include "game/game.hpp"
#include "game/dvars.hpp"

#include "scheduler.hpp"
#include "command.hpp"

#include "localized_strings.hpp"
#include "console.hpp"
#include "discord.hpp"
#include "download.hpp"
#include "game_module.hpp"
#include "fps.hpp"
#include "server_list.hpp"
#include "filesystem.hpp"
#include "motd.hpp"
#include "fastfiles.hpp"
#include "scripting.hpp"
#include "party.hpp"

#include "game/ui_scripting/execution.hpp"
#include "game/scripting/execution.hpp"

#include "ui_scripting.hpp"

#include <utils/string.hpp>
#include <utils/hook.hpp>
#include <utils/io.hpp>
#include <utils/binary_resource.hpp>

#include "steam/steam.hpp"

#include <discord_rpc.h>
#include "stats.hpp"
#include "voice/voice_chat_globals.hpp"

#include <chrono>
#include <iomanip>
#include <sstream>

namespace ui_scripting
{
	namespace
	{
		std::unordered_map<game::hks::cclosure*, std::function<arguments(const function_arguments& args)>> converted_functions;

		utils::hook::detour hks_start_hook;
		utils::hook::detour hks_shutdown_hook;
		utils::hook::detour hks_package_require_hook;

		utils::hook::detour hks_load_hook;

		const auto lui_common = utils::nt::load_resource(LUI_COMMON);

		struct globals_t
		{
			std::string in_require_script;
			std::unordered_map<std::string, std::string> loaded_scripts;
			bool load_raw_script{};
			bool is_raw_in_zone{};
			std::string raw_script_name{};
		};

		globals_t globals{};

		bool is_loaded_script(const std::string& name)
		{
			return globals.loaded_scripts.contains(name);
		}

		std::string get_root_script(const std::string& name)
		{
			const auto itr = globals.loaded_scripts.find(name);
			return itr == globals.loaded_scripts.end() ? std::string() : itr->second;
		}

		void print_error(const std::string& error)
		{
#ifdef DEBUG
			console::error("************** LUI script execution error **************\n");
			console::error("%s\n", error.data());
			console::error("********************************************************\n");
#endif
		}

		[[maybe_unused]] void print_loading_script(const std::string& name)
		{
			console::debug("Loading LUI script '%s'\n", name.data());
		}

		std::string get_current_script()
		{
			const auto state = *game::hks::lua_state;
			game::hks::lua_Debug info{};
			game::hks::hksi_lua_getstack(state, 1, &info);
			game::hks::hksi_lua_getinfo(state, "nSl", &info);
			return info.short_src;
		}

		int load_buffer(const std::string& name, const std::string& data)
		{
			const auto state = *game::hks::lua_state;
			const auto sharing_mode = state->m_global->m_bytecodeSharingMode;
			state->m_global->m_bytecodeSharingMode = game::hks::HKS_BYTECODE_SHARING_ON;
			const auto _0 = gsl::finally([&]()
			{
				state->m_global->m_bytecodeSharingMode = sharing_mode;
			});

			game::hks::HksCompilerSettings compiler_settings{};
			return game::hks::hksi_hksL_loadbuffer(state, &compiler_settings, data.data(), data.size(), name.data());
		}

		void load_script(const std::string& name, const std::string& data)
		{			
			globals.loaded_scripts[name] = name;

			const auto lua = get_globals();
			const auto load_results = lua["loadstring"](data, name);

			if (load_results[0].is<function>())
			{
				const auto results = lua["pcall"](load_results);
				if (!results[0].as<bool>())
				{
					print_error(results[1].as<std::string>());
				}
			}
			else if (load_results[1].is<std::string>())
			{
				print_error(load_results[1].as<std::string>());
			}
		}

		void load_scripts(const std::string& script_dir)
		{
			if (!utils::io::directory_exists(script_dir))
			{
				return;
			}

			const auto scripts = utils::io::list_files(script_dir);

			for (const auto& script : scripts)
			{
				std::string data{};
				if (std::filesystem::is_directory(script) && utils::io::read_file(script + "/__init__.lua", &data))
				{
#ifdef DEBUG
					print_loading_script(script);
#endif
					load_script(script + "/__init__.lua", data);
				}
			}
		}

		script_value json_to_lua(const nlohmann::json& json)
		{
			if (json.is_object())
			{
				table object;
				for (const auto& [key, value] : json.items())
				{
					object[key] = json_to_lua(value);
				}
				return object;
			}

			if (json.is_array())
			{
				table array;
				auto index = 1;
				for (const auto& value : json.array())
				{
					array[index++] = json_to_lua(value);
				}
				return array;
			}

			if (json.is_boolean())
			{
				return json.get<bool>();
			}

			if (json.is_number_integer())
			{
				return json.get<int>();
			}

			if (json.is_number_float())
			{
				return json.get<float>();
			}

			if (json.is_string())
			{
				return json.get<std::string>();
			}

			return {};
		}

		void setup_functions()
		{
			const auto lua = get_globals();

			lua["io"] = {};
			lua["os"] = {};
			lua["debug"] = {};

			using game = table;
			auto game_type = game();
			lua["game"] = game_type;

			game_type["getfps"] = [](const game&)
			{
				return fps::get_fps();
			};

			if (::game::environment::is_mp())
			{
				game_type["getping"] = [](const game&)
				{
					if ((*::game::client_state) == nullptr)
					{
						return 0;
					}

					return (*::game::client_state)->ping;
				};
			}

			game_type["ismultiplayer"] = [](const game&)
			{
				return ::game::environment::is_mp();
			};

			game_type["addlocalizedstring"] = [](const game&, const std::string& string,
				const std::string& value)
			{
				localized_strings::override(string, value);
			};
			
			game_type["sharedset"] = [](const game&, const std::string& key, const std::string& value)
			{
				scripting::shared_table.access([key, value](scripting::shared_table_t& table)
				{
					table[key] = value;
				});
			};

			game_type["sharedget"] = [](const game&, const std::string& key)
			{
				std::string result;
				scripting::shared_table.access([key, &result](scripting::shared_table_t& table)
				{
					result = table[key];
				});
				return result;
			};

			game_type["sharedclear"] = [](const game&)
			{
				scripting::shared_table.access([](scripting::shared_table_t& table)
				{
					table.clear();
				});
			};

			game_type["assetlist"] = [](const game&, const std::string& type_string)
			{
				auto table_ = table();
				auto index = 1;
				auto type_index = -1;

				for (auto i = 0; i < ::game::XAssetType::ASSET_TYPE_COUNT; i++)
				{
					if (type_string == ::game::g_assetNames[i])
					{
						type_index = i;
					}
				}

				if (type_index == -1)
				{
					throw std::runtime_error("Asset type does not exist");
				}

				const auto type = static_cast<::game::XAssetType>(type_index);
				fastfiles::enum_assets(type, [type, &table_, &index](const ::game::XAssetHeader header)
				{
					const auto asset = ::game::XAsset{type, header};
					const std::string asset_name = ::game::DB_GetXAssetName(&asset);
					table_[index++] = asset_name;
				}, true);

				return table_;
			};

			game_type["getweapondisplayname"] = [](const game&, const std::string& name)
			{
				const auto alternate = name.starts_with("alt_");
				const auto weapon = ::game::G_GetWeaponForName(name.data());

				char buffer[0x400] = {0};
				::game::CG_GetWeaponDisplayName(weapon, alternate, buffer, 0x400);

				return std::string(buffer);
			};

			game_type["getattachmentdisplayname"] = [](const game&, const std::string& name)
			{				
				auto attachment = ::game::DB_FindXAssetHeader(::game::XAssetType::ASSET_TYPE_ATTACHMENT, name.data(), 1).attachment;

				if (!attachment) return std::string("");

				return std::string(attachment->szDisplayName);
			};

			game_type["getloadedmod"] = [](const game&)
			{
				/*
				const auto& path = mods::get_mod();
				return path.value_or("");
				*/
				return "";
			};

			game_type["sendStats"] = [](const game&)
			{
				stats::send_stats();
			};

			game_type["getkillcamweaponinfo"] = [](const game&, const int weapon_id, bool is_alt_weapon)
			{
				std::vector<std::string> weapon_info{};

				auto weaponHudMaterial = ::game::BG_KillIcon(weapon_id, is_alt_weapon);

				if (weaponHudMaterial)
					weapon_info.push_back(weaponHudMaterial->info.name);
				else
					weapon_info.push_back("");

				auto weaponHudIsFlipped = ::game::BG_FlipKillIcon(weapon_id, is_alt_weapon);

				weapon_info.push_back(std::to_string(!!weaponHudIsFlipped));

				return weapon_info;
			};

			game_type["virtuallobbypresentable"] = [](const game&)
			{
				::game::Dvar_SetFromStringByNameFromSource("virtualLobbyPresentable", "1", ::game::DVAR_SOURCE_INTERNAL);
			};

			game_type["getcurrentgamelanguage"] = [](const game&)
			{
				return steam::SteamApps()->GetCurrentGameLanguage();
			};

			game_type["isdefaultmaterial"] = [](const game&, const std::string& material)
			{
				return static_cast<bool>(::game::DB_IsXAssetDefault(::game::ASSET_TYPE_MATERIAL,
					material.data()));
			};

			game_type["getcommandbind"] = [](const game&, const std::string& cmd)
			{
				const auto binding = ::game::Key_GetBindingForCmd(cmd.data());
				auto key = -1;
				for (auto i = 0; i < 256; i++)
				{
					if (::game::playerKeys[0].keys[i].binding == binding)
					{
						key = i;
					}
				}

				if (key == -1)
				{
					return ::game::UI_SafeTranslateString("KEY_UNBOUND");
				}
				else
				{
					const auto loc_string = ::game::Key_KeynumToString(key, 1, 0);
					return ::game::UI_SafeTranslateString(loc_string);
				}
			};

			game_type["openlink"] = [](const game&, const std::string& name)
			{
				const auto links = motd::get_links();
				const auto link = links.find(name);
				if (link == links.end())
				{
					return;
				}

				ShellExecuteA(nullptr, "open", link->second.data(), nullptr, nullptr, SW_SHOWNORMAL);
			};

			game_type["islink"] = [](const game&, const std::string& name)
			{
				const auto links = motd::get_links();
				const auto link = links.find(name);
				return link != links.end();
			};

			auto hud_extras = table();
			lua["hudextras"] = hud_extras;
			hud_extras["showvoicemessage"] = [](const game&) {
				if (voice_chat_globals::is_voice_message_shown()) {
					return;
				}

				notify("showvoicemessage", {});
				voice_chat_globals::set_voice_message_shown_state(true);
				scheduler::once([=]()
				{
					notify("hidevoicemessage", {});
				}, scheduler::pipeline::main, 5s);
			};

			// In lua hudextras:gettime("%H:%M:%S %p") // Example: 02:30:15 PM
			hud_extras["gettime"] = [](const game&, std::string format) { // Format can be %H:%M:%S %p
				auto now = std::chrono::system_clock::now();
				auto time_t_now = std::chrono::system_clock::to_time_t(now);

				std::tm local_time;
				localtime_s(&local_time, &time_t_now);

				std::ostringstream oss;
				oss << std::put_time(&local_time, format.c_str());

				return oss.str();
			};

			auto server_list_table = table();
			lua["serverlist"] = server_list_table;

			server_list_table["getplayercount"] = server_list::get_player_count;
			server_list_table["getservercount"] = server_list::get_server_count;
			server_list_table["gettotalpagecount"] = server_list::tcp::get_total_pages;
			server_list_table["getserverlimitperpage"] = server_list::tcp::get_server_limit_per_page;
			server_list_table["getcurrentpage"] = server_list::tcp::get_current_page;
			server_list_table["getnotificationmessage"] = server_list::tcp::get_notification_message;
			server_list_table["geterrormessage"] = server_list::tcp::get_error_message;
			server_list_table["geterrorheader"] = server_list::tcp::get_error_header;
			server_list_table["getpagenumber"] = [](int server_index) {
				return server_list::tcp::get_page_number(server_index);
			};

			server_list_table["loadpage"] = [](const game&, int page_index) {
				server_list::tcp::load_page(page_index);
			};

			server_list_table["nextpage"] = [](const game&) {
				server_list::tcp::next_page();
			};

			server_list_table["previouspage"] = [](const game&) {
				server_list::tcp::previous_page();
			};
			server_list_table["addfavourite"] = [](const game&, int index)
			{
				server_list::add_favourite(index);
			};
			server_list_table["deletefavourite"] = [](const game&, int index)
			{
				server_list::delete_favourite(index);
			};
			server_list_table["sortservers"] = [](const game&, int sort_type)
			{
				//server_list::sort_serverlist(sort_type);
				  server_list::tcp::sort_current_page(sort_type);
			};
			server_list_table["isgettingserverlist"] = [](const game&)
			{
					return server_list::tcp::is_getting_server_list();
			};
			server_list_table["isgettingfavourites"] = [](const game&)
			{
				return server_list::tcp::is_getting_favourites();
			};
			server_list_table["isloadingpage"] = [](const game&)
			{
				return server_list::tcp::is_loading_a_page();
			};

			auto download_table = table();
			lua["download"] = download_table;

			download_table["abort"] = download::stop_download;

			download_table["userdownloadresponse"] = party::user_download_response;
			download_table["getwwwurl"] = []
			{
				const auto state = party::get_server_connection_state();
				return state.base_url;
			};

			auto discord_table = table();
			lua["discord"] = discord_table;

			discord_table["respond"] = discord::respond;

			discord_table["getavatarmaterial"] = [](const std::string& id)
				-> script_value
			{
				const auto material = discord::get_avatar_material(id);
				if (material == nullptr)
				{
					return {};
				}

				return lightuserdata(material);
			};

			discord_table["reply"] = table();
			discord_table["reply"]["yes"] = DISCORD_REPLY_YES;
			discord_table["reply"]["ignore"] = DISCORD_REPLY_IGNORE;
			discord_table["reply"]["no"] = DISCORD_REPLY_NO;

			auto motd_table = table();
			lua["motd"] = motd_table;

			motd_table["getnumfeaturedtabs"] = motd::get_num_featured_tabs;

			motd_table["getmotd"] = []()
			{
				return json_to_lua(motd::get_motd());
			};

			motd_table["getfeaturedtab"] = [](const int index)
			{
				return json_to_lua(motd::get_featured_tab(index));
			};

			motd_table["hasmotd"] = motd::has_motd;
		}

		void start()
		{
			globals = {};

			const auto lua = get_globals();
			lua["EnableGlobals"]();

			setup_functions();

			lua["print"] = [](const variadic_args& va)
			{
				std::string buffer{};
				const auto to_string = get_globals()["tostring"];

				for (auto i = 0; i < va.size(); i++)
				{
					const auto& arg = va[i];
					const auto str = to_string(arg)[0].as<std::string>();
					buffer.append(str);

					if (i < va.size() - 1)
					{
						buffer.append("\t");
					}
				}

				console::info("%s\n", buffer.data());
			};

			lua["table"]["unpack"] = lua["unpack"];
			lua["luiglobals"] = lua;

			load_script("lui_common", lui_common);

#ifdef DEBUG
			for (const auto& path : filesystem::get_search_paths_rev())
			{
				load_scripts(path + "/ui_scripts/");
				load_scripts(path + "/ui_scripts/mp/");
			}
#endif // DEBUG

			std::vector<std::pair<std::string, game::XAssetHeader>> loaded_scripts_queue;

			auto insert_alphabetically = [](std::vector<std::pair<std::string, game::XAssetHeader>>&vec, const std::string & key, const game::XAssetHeader & value)
			{
				auto it = std::lower_bound(vec.begin(), vec.end(), key,
					[](const auto& elem, const auto& val)
					{ 
						return elem.first < val; 
					});

				vec.insert(it, std::make_pair(key, value));
			};

			// check zones for LUI rawfiles to load
			fastfiles::enum_assets(game::ASSET_TYPE_RAWFILE, [&](game::XAssetHeader header)
			{
				const auto* rawfile = header.rawfile;
				if (rawfile)
				{
					std::string rawfile_name = rawfile->name;
					if (!rawfile_name.ends_with("/__init__.lua"))
					{
						return;
					}

					if (rawfile_name.ends_with(".lua"))
					{
						const auto base_name = rawfile_name.substr(0, rawfile_name.size() - 4);
						insert_alphabetically(loaded_scripts_queue, rawfile_name, header);
					}
				}
			}, false);

			for (const auto& entry : loaded_scripts_queue)
			{
				const auto* rawfile = entry.second.rawfile;
				if (!rawfile)
				{
					continue;
				}

				auto rawfile_name = entry.first;

#ifdef DEBUG
				print_loading_script(rawfile_name);
#endif

				std::string buffer;
				const auto len = game::DB_GetRawFileLen(rawfile);

				buffer.resize(len);
				game::DB_GetRawBuffer(rawfile, buffer.data(), len);
				if (len > 0)
				{
					buffer.pop_back();
				}

				load_script(rawfile_name, buffer);
			}
		}

		void try_start()
		{
			try
			{
				start();
			}
			catch (const std::exception& e)
			{
				console::error("Failed to load LUI scripts: %s\n", e.what());
			}
		}

		void* hks_start_stub(char a1)
		{
			const auto _0 = gsl::finally(&try_start);
			return hks_start_hook.invoke<void*>(a1);
		}

		void hks_shutdown_stub()
		{
			converted_functions.clear();
			globals = {};
			return hks_shutdown_hook.invoke<void>();
		}

		void* hks_package_require_stub(game::hks::lua_State* state)
		{
			const auto script = get_current_script();
			const auto root = get_root_script(script);
			globals.in_require_script = root;
			return hks_package_require_hook.invoke<void*>(state);
		}

		game::XAssetHeader db_find_x_asset_header_stub(game::XAssetType type, const char* name, int allow_create_default)
		{
			game::XAssetHeader header{.luaFile = nullptr};

			if (!is_loaded_script(globals.in_require_script))
			{
				return game::DB_FindXAssetHeader(type, name, allow_create_default);
			}

			const auto folder = globals.in_require_script.substr(0, globals.in_require_script.find_last_of("/\\"));
			const std::string name_ = name;
			const std::string target_script = folder + "/" + name_ + ".lua";

			if (utils::io::file_exists(target_script))
			{
				globals.load_raw_script = true;
				globals.raw_script_name = target_script;
				header.luaFile = reinterpret_cast<game::LuaFile*>(1);
			}
			else if (game::DB_XAssetExists(game::ASSET_TYPE_RAWFILE, target_script.data()) &&
				!game::DB_IsXAssetDefault(game::ASSET_TYPE_RAWFILE, target_script.data()))
			{
				globals.load_raw_script = true;
				globals.is_raw_in_zone = true;
				globals.raw_script_name = target_script;
				header.luaFile = reinterpret_cast<game::LuaFile*>(1);
			}
			else if (name_.starts_with("ui/LUI/"))
			{
				return game::DB_FindXAssetHeader(type, name, allow_create_default);
			}

			return header;
		}

		int hks_load_stub(game::hks::lua_State* state, void* compiler_options, 
			void* reader, void* reader_data, const char* chunk_name)
		{
			if (globals.load_raw_script)
			{
				globals.load_raw_script = false;

				globals.loaded_scripts[globals.raw_script_name] = globals.in_require_script;

				if (globals.is_raw_in_zone)
				{
					globals.is_raw_in_zone = false;

					const auto rawfile = game::DB_FindXAssetHeader(game::ASSET_TYPE_RAWFILE, globals.raw_script_name.data(), 0).rawfile;
					if (rawfile)
					{
						std::string buffer;
						const auto len = game::DB_GetRawFileLen(rawfile);

						buffer.resize(len);
						game::DB_GetRawBuffer(rawfile, buffer.data(), len);
						if (len > 0)
						{
							buffer.pop_back();
						}

						return load_buffer(globals.raw_script_name, buffer);
					}
				}
				else
				{
					return load_buffer(globals.raw_script_name, utils::io::read_file(globals.raw_script_name));
				}
			}

			return hks_load_hook.invoke<int>(state, compiler_options, reader,
				reader_data, chunk_name);
		}

		std::string current_error;
		int main_handler(game::hks::lua_State* state)
		{
			bool error = false;

			try
			{
				const auto value = state->m_apistack.base[-1];
				if (value.t != game::hks::TCFUNCTION)
				{
					return 0;
				}

				const auto closure = value.v.cClosure;
				if (!converted_functions.contains(closure))
				{
					return 0;
				}

				const auto& function = converted_functions[closure];

				const auto args = get_return_values();
				const auto results = function(args);

				for (const auto& result : results)
				{
					push_value(result);
				}

				return static_cast<int>(results.size());
			}
			catch (const std::exception& e)
			{
				current_error = e.what();
				error = true;
			}

			if (error)
			{
				game::hks::hksi_luaL_error(state, current_error.data());
			}

			return 0;
		}
	}

	table get_globals()
	{
		const auto state = *game::hks::lua_state;
		return state->globals.v.table;
	}

	template <typename F>
	game::hks::cclosure* convert_function(F f)
	{
		const auto state = *game::hks::lua_state;
		const auto closure = game::hks::cclosure_Create(state, main_handler, 0, 0, 0);
		converted_functions[closure] = wrap_function(f);
		return closure;
	}

	bool lui_running()
	{
		return *game::hks::lua_state != nullptr;
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

			dvars::register_bool("r_preloadShadersFrontendAllow", true, game::DVAR_FLAG_SAVED, "Allow shader popup on startup");

			utils::hook::call(0x25E809_b, db_find_x_asset_header_stub);
			utils::hook::call(0x25E6BB_b, db_find_x_asset_header_stub);

			hks_load_hook.create(0x22C180_b, hks_load_stub);

			hks_package_require_hook.create(0x214040_b, hks_package_require_stub);
			hks_start_hook.create(0x27A790_b, hks_start_stub);
			hks_shutdown_hook.create(0x2707C0_b, hks_shutdown_stub);

			command::add("lui_restart", []
			{
				utils::hook::invoke<void>(0x27BEC0_b);
			});
		}
	};
}

REGISTER_COMPONENT(ui_scripting::component)
