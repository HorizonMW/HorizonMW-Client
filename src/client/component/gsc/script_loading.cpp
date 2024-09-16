#include <std_include.hpp>
#include "loader/component_loader.hpp"

#include "component/console.hpp"
#include "component/fastfiles.hpp"
#include "component/filesystem.hpp"
#include "component/logfile.hpp"
#include "component/scripting.hpp"
#include "component/memory.hpp"

#include "game/dvars.hpp"

#include "game/scripting/array.hpp"
#include "game/scripting/execution.hpp"
#include "game/scripting/function.hpp"

#include "script_extension.hpp"
#include "script_loading.hpp"

#include <utils/compression.hpp>
#include <utils/hook.hpp>
#include <utils/io.hpp>
#include <utils/string.hpp>

namespace gsc
{
	std::unique_ptr<xsk::gsc::h1::context> gsc_ctx = std::make_unique<xsk::gsc::h1::context>();

	struct loaded_script_t
	{
		game::ScriptFile* ptr;
		std::map<std::uint32_t, col_line_t> devmap;
	};

	std::unordered_map<std::string, loaded_script_t> loaded_scripts;

	namespace
	{
		utils::hook::detour scr_begin_load_scripts_hook;
		utils::hook::detour scr_end_load_scripts_hook;

		std::unordered_map<std::string, std::uint32_t> main_handles;
		std::unordered_map<std::string, std::uint32_t> init_handles;

		utils::memory::allocator scriptfile_allocator;

		struct
		{
			char* buf = nullptr;
			char* pos = nullptr;
			const unsigned int size = memory::custom_script_mem_size;
		} script_memory;

		char* allocate_buffer(size_t size)
		{
			if (script_memory.buf == nullptr)
			{
				script_memory.buf = game::PMem_AllocFromSource_NoDebug(script_memory.size, 4, 1, game::PMEM_SOURCE_SCRIPT);
				script_memory.pos = script_memory.buf;
			}

			if (script_memory.pos + size > script_memory.buf + script_memory.size)
			{
				game::Com_Error(game::ERR_FATAL, "Out of script memory");
			}

			const auto pos = script_memory.pos;
			script_memory.pos += size;
			return pos;
		}

		void free_script_memory()
		{
			game::PMem_PopFromSource_NoDebug(script_memory.buf, script_memory.size, 4, 1, game::PMEM_SOURCE_SCRIPT);
			script_memory.buf = nullptr;
			script_memory.pos = nullptr;
		}

		void clear()
		{
			main_handles.clear();
			init_handles.clear();
			loaded_scripts.clear();
			scriptfile_allocator.clear();
			free_script_memory();
		}

		bool read_raw_script_file(const std::string& name, std::string* data)
		{
			if (filesystem::read_file(name, data))
			{
				return true;
			}

			const auto* name_str = name.data();
			if (game::DB_XAssetExists(game::ASSET_TYPE_RAWFILE, name_str) &&
				!game::DB_IsXAssetDefault(game::ASSET_TYPE_RAWFILE, name_str))
			{
				const auto asset = game::DB_FindXAssetHeader(game::ASSET_TYPE_RAWFILE, name_str, false);
				const auto len = game::DB_GetRawFileLen(asset.rawfile);
				data->resize(len);
				game::DB_GetRawBuffer(asset.rawfile, data->data(), len);
				if (len > 0)
				{
					data->pop_back();
				}

				return true;
			}

			return false;
		}

		std::map<std::uint32_t, col_line_t> parse_devmap(const xsk::gsc::buffer& devmap)
		{
			auto devmap_ptr = devmap.data;

			const auto read_32 = [&]()
			{
				const auto val = *reinterpret_cast<const std::uint32_t*>(devmap_ptr);
				devmap_ptr += sizeof(std::uint32_t);
				return val;
			};

			const auto read_16 = [&]()
			{
				const auto val = *reinterpret_cast<const std::uint16_t*>(devmap_ptr);
				devmap_ptr += sizeof(std::uint16_t);
				return val;
			};

			std::map<std::uint32_t, col_line_t> pos_map;

			const auto devmap_count = read_32();
			for (auto i = 0u; i < devmap_count; i++)
			{
				const auto script_pos = read_32() - 1;
				const auto line = read_16();
				const auto col = read_16();

				pos_map[script_pos] = {line, col};
			}

			return pos_map;
		}

		bool force_load = false;

		game::ScriptFile* load_custom_script(const char* file_name, const std::string& real_name)
		{
			if (const auto itr = loaded_scripts.find(file_name); itr != loaded_scripts.end())
			{
				return itr->second.ptr;
			}

			if (game::VirtualLobby_Loaded() && !force_load)
			{
				return nullptr;
			}

			std::string source_buffer{};
			if (!read_raw_script_file(real_name + ".gsc", &source_buffer) || source_buffer.empty())
			{
				return nullptr;
			}

			// filter out "GSC rawfiles" that were used for development usage and are not meant for us.
			// each "GSC rawfile" has a ScriptFile counterpart to be used instead
			if (game::DB_XAssetExists(game::ASSET_TYPE_SCRIPTFILE, file_name) &&
				!game::DB_IsXAssetDefault(game::ASSET_TYPE_SCRIPTFILE, file_name))
			{
				if ((real_name.starts_with("maps/createfx") || real_name.starts_with("maps/createart") || real_name.starts_with("maps/mp"))
					&& (real_name.ends_with("_fx") || real_name.ends_with("_fog") || real_name.ends_with("_hdr")))
				{
#ifdef DEBUG
					console::debug("Refusing to compile rawfile '%s'\n", real_name.data());
#endif
					return game::DB_FindXAssetHeader(game::ASSET_TYPE_SCRIPTFILE, file_name, false).scriptfile;
				}
			}

			try
			{
				auto& compiler = gsc_ctx->compiler();
				auto& assembler = gsc_ctx->assembler();

				std::vector<std::uint8_t> data;
				data.assign(source_buffer.begin(), source_buffer.end());

				const auto assembly_ptr = compiler.compile(real_name, data);
				[[maybe_unused]] const auto& [bytecode, stack] = assembler.assemble(*assembly_ptr);

				const auto script_file_ptr = static_cast<game::ScriptFile*>(scriptfile_allocator.allocate(sizeof(game::ScriptFile)));
				script_file_ptr->name = file_name;

				script_file_ptr->len = static_cast<int>(stack.size);
				script_file_ptr->bytecodeLen = static_cast<int>(bytecode.size);

				const auto stack_size = static_cast<std::uint32_t>(stack.size + 1);
				const auto byte_code_size = static_cast<std::uint32_t>(bytecode.size + 1);

				script_file_ptr->buffer = static_cast<char*>(scriptfile_allocator.allocate(stack_size));
				std::memcpy(const_cast<char*>(script_file_ptr->buffer), stack.data, stack.size);

				script_file_ptr->bytecode = allocate_buffer(byte_code_size);
				std::memcpy(script_file_ptr->bytecode, bytecode.data, bytecode.size);

				script_file_ptr->compressedLen = 0;

				loaded_script_t loaded_script{};
				loaded_script.ptr = script_file_ptr;
				loaded_scripts.insert(std::make_pair(file_name, loaded_script));

				if (game::environment::is_dedi() || (developer_script && developer_script->current.enabled))
				{
					console::info("Loaded custom gsc '%s.gsc'\n", real_name.data());
				}

				return script_file_ptr;
			}
			catch ([[maybe_unused]] const std::exception& e)
			{
#ifdef DEBUG
				console::error("*********** script compile error *************\n");
				console::error("failed to compile '%s':\n%s", real_name.data(), e.what());
				console::error("**********************************************\n");
#else
				console::error("script '%s' failed to load!\nerror: %s\n", real_name.data(), e.what());
#endif
				return nullptr;
			}
		}

		std::string get_raw_script_file_name(const std::string& name)
		{
			if (name.ends_with(".gsh"))
			{
				return name;
			}

			return name + ".gsc";
		}

		std::string get_script_file_name(const std::string& name)
		{
			const auto id = gsc_ctx->token_id(name);
			if (!id)
			{
				return name;
			}

			return std::to_string(id);
		}

		std::pair<xsk::gsc::buffer, std::vector<std::uint8_t>> read_compiled_script_file(const std::string& name, const std::string& real_name)
		{
			const auto* script_file = game::DB_FindXAssetHeader(game::ASSET_TYPE_SCRIPTFILE, name.data(), false).scriptfile;
			if (script_file == nullptr)
			{
				throw std::runtime_error(std::format("Could not load scriptfile '{}'", real_name));
			}

#ifdef DEBUG
			console::debug("Decompiling scriptfile '%s'\n", real_name.data());
#endif

			const auto len = script_file->compressedLen;
			const std::string stack{script_file->buffer, static_cast<std::uint32_t>(len)};

			const auto decompressed_stack = utils::compression::zlib::decompress(stack);

			std::vector<std::uint8_t> stack_data;
			stack_data.assign(decompressed_stack.begin(), decompressed_stack.end());

			return {{reinterpret_cast<std::uint8_t*>(script_file->bytecode), static_cast<std::uint32_t>(script_file->bytecodeLen)}, stack_data};
		}

		void load_script(const std::string& name)
		{
			if (!game::Scr_LoadScript(name.data()))
			{
				return;
			}

			if (game::VirtualLobby_Loaded() && !game::CL_IsCgameInitialized())
			{
				const auto vl_init_handle = game::Scr_GetFunctionHandle(name.data(), gsc_ctx->token_id("vl_init"));
				const auto vl_main_handle = game::Scr_GetFunctionHandle(name.data(), gsc_ctx->token_id("vl_main"));
				if (vl_main_handle)
				{
#ifdef DEBUG
					console::info("Loaded '%s::vl_main'\n", name.data());
#endif
					main_handles[name] = vl_main_handle;
				}

				if (vl_init_handle)
				{
#ifdef DEBUG
					console::info("Loaded '%s::vl_init'\n", name.data());
#endif
					init_handles[name] = vl_init_handle;
				}
			}
			
			const auto main_handle = game::Scr_GetFunctionHandle(name.data(), gsc_ctx->token_id("main"));
			const auto init_handle = game::Scr_GetFunctionHandle(name.data(), gsc_ctx->token_id("init"));

			if (main_handle)
			{
#ifdef DEBUG
				console::info("Loaded '%s::main'\n", name.data());
#endif
				main_handles[name] = main_handle;
			}

			if (init_handle)
			{
#ifdef DEBUG
				console::info("Loaded '%s::init'\n", name.data());
#endif
				init_handles[name] = init_handle;
			}
		}

		void load_scripts(const std::filesystem::path& root_dir, const std::filesystem::path& subfolder)
		{
			std::filesystem::path script_dir = root_dir / subfolder;

			if (root_dir.generic_string() == "zone"s)
			{
				fastfiles::enum_assets(game::ASSET_TYPE_RAWFILE, [&subfolder](game::XAssetHeader header)
					{
						const auto* rawfile = header.rawfile;
						if (rawfile)
						{
							std::string rawfile_name = rawfile->name;

							const auto subfolder_name = subfolder.generic_string();
							if (!rawfile_name.starts_with(subfolder_name) || !rawfile_name.ends_with(".gsc"))
							{
								return;
							}

							const auto base_name = rawfile_name.substr(0, rawfile_name.size() - 4);
							load_script(base_name);
						}
					}, false);
			}

#ifndef DEBUG
			if (game::environment::is_dedi())
			{
#endif
				if (utils::io::directory_exists(script_dir.generic_string()))
				{
					const auto scripts = utils::io::list_files(script_dir.generic_string());
					for (const auto& script : scripts)
					{
						if (!script.ends_with(".gsc"))
						{
							continue;
						}

						std::filesystem::path path(script);
						const auto relative = path.lexically_relative(root_dir).generic_string();
						const auto base_name = relative.substr(0, relative.size() - 4);

						load_script(base_name);
					}
				}
#ifndef DEBUG
			}
#endif
		}

		int db_is_x_asset_default(game::XAssetType type, const char* name)
		{
			if (loaded_scripts.contains(name))
			{
				return 0;
			}

			return game::DB_IsXAssetDefault(type, name);
		}

		void load_gametype_script_stub(void* a1, void* a2)
		{
			utils::hook::invoke<void>(0x18BC00_b, a1, a2);

			force_load = true;
			const auto _0 = gsl::finally([&]
			{
				force_load = false;
			});

			const auto load_scripts_wrapper = [](const std::string& path)
			{
				load_scripts(path, "scripts/mp_patches/"); // ran in game & in vlobby
				if (game::environment::is_dedi())
				{
					load_scripts(path, "user_scripts/mp_patches/");
				}

				static auto* vlobby_active_dvar = game::Dvar_FindVar("virtuallobbyactive");
				if (game::VirtualLobby_Loaded() && vlobby_active_dvar && vlobby_active_dvar->current.enabled)
				{
					load_scripts(path, "scripts/vlobby_patches/");
				}
				else
				{
					load_scripts(path, "scripts/mp/");
					if (game::environment::is_dedi())
					{
						load_scripts(path, "user_scripts/mp/");
					}
				}
			};

			// find scripts from disk
			for (const auto& path : filesystem::get_search_paths())
			{
				load_scripts_wrapper(path);
			}

			// find scripts from zone
			load_scripts_wrapper("zone");
		}

		void db_get_raw_buffer_stub(const game::RawFile* rawfile, char* buf, const int size)
		{
			if (rawfile->len > 0 && rawfile->compressedLen == 0)
			{
				std::memset(buf, 0, size);
				std::memcpy(buf, rawfile->buffer, std::min(rawfile->len, size));
				return;
			}

			game::DB_GetRawBuffer(rawfile, buf, size);
		}

		void scr_begin_load_scripts_stub()
		{
			const bool dev_script = developer_script ? developer_script->current.enabled : false;
			const auto comp_mode = dev_script ?
				xsk::gsc::build::dev :
				xsk::gsc::build::prod;

			gsc_ctx->init(comp_mode, [](const std::string& include_name)
				-> std::pair<xsk::gsc::buffer, std::vector<std::uint8_t>>
				{
					const auto real_name = get_raw_script_file_name(include_name);

					std::string file_buffer;
					if (!read_raw_script_file(real_name, &file_buffer) || file_buffer.empty())
					{
						const auto name = get_script_file_name(include_name);
						if (game::DB_XAssetExists(game::ASSET_TYPE_SCRIPTFILE, name.data()))
						{
							return read_compiled_script_file(name, real_name);
						}

						throw std::runtime_error(std::format("Could not load gsc file '{}'", real_name));
					}

					std::vector<std::uint8_t> script_data;
					script_data.assign(file_buffer.begin(), file_buffer.end());

					return { {}, script_data };
				});

			scr_begin_load_scripts_hook.invoke<void>();
		}


		void scr_end_load_scripts_stub()
		{
			// cleanup the compiler
			gsc_ctx->cleanup();

			scr_end_load_scripts_hook.invoke<void>();
		}
	}

	void load_main_handles()
	{
		for (auto& function_handle : main_handles)
		{
#ifdef DEBUG
			console::info("Executing '%s::main'\n", function_handle.first.data());
#endif
			game::RemoveRefToObject(game::Scr_ExecThread(function_handle.second, 0));
		}
	}

	void load_init_handles()
	{
		for (auto& function_handle : init_handles)
		{
#ifdef DEBUG
			console::info("Executing '%s::init'\n", function_handle.first.data());
#endif
			game::RemoveRefToObject(game::Scr_ExecThread(function_handle.second, 0));
		}
	}

	game::ScriptFile* find_script(game::XAssetType type, const char* name, int allow_create_default)
	{
		std::string real_name = name;
		const auto id = static_cast<std::uint16_t>(std::atoi(name));
		if (id)
		{
			real_name = gsc_ctx->token_name(id);
		}

		auto* script = load_custom_script(name, real_name);
		if (script)
		{
			return script;
		}

		return game::DB_FindXAssetHeader(type, name, allow_create_default).scriptfile;
	}

	std::optional<std::map<std::uint32_t, col_line_t>*> get_script_devmap(const std::string& name)
	{
		const auto iter = loaded_scripts.find(name);
		if (iter == loaded_scripts.end())
		{
			return {};
		}

		return { &iter->second.devmap };
	}

	class loading final : public component_interface
	{
	public:
		void post_unpack() override
		{
			// Load our scripts with an uncompressed stack
			utils::hook::call(0x50E3C0_b, db_get_raw_buffer_stub);

			scr_begin_load_scripts_hook.create(0x504BC0_b, scr_begin_load_scripts_stub);
			scr_end_load_scripts_hook.create(0x504CF0_b, scr_end_load_scripts_stub);

			// ProcessScript: hook xasset functions to return our own custom scripts
			utils::hook::call(0x50E357_b, find_script);
			utils::hook::call(0x50E367_b, db_is_x_asset_default);

			// GScr_LoadScripts: initial loading of scripts
			utils::hook::call(0x18C325_b, load_gametype_script_stub);

			// main is called from scripting.cpp
			// init is called from scripting.cpp

			scripting::on_shutdown([](bool free_scripts, bool post_shutdown)
			{
				logfile::clear_callbacks();
				if (free_scripts && post_shutdown)
				{
					clear();
				}
			});
		}

		void pre_destroy() override
		{
			scr_begin_load_scripts_hook.clear();
			scr_end_load_scripts_hook.clear();
		}
	};
}

REGISTER_COMPONENT(gsc::loading)
