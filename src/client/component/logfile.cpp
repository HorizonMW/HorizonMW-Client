#include <std_include.hpp>
#include "loader/component_loader.hpp"

#include "component/logfile.hpp"
#include "component/scripting.hpp"
#include "component/scheduler.hpp"
#include "component/gsc/script_extension.hpp"

#include "game/dvars.hpp"

#include <utils/hook.hpp>
#include <utils/io.hpp>

#include <voice/voice_chat_globals.hpp>

namespace logfile
{
	bool hook_enabled = true;

	namespace
	{
		struct gsc_hook_t
		{
			bool is_lua_hook{};
			const char* target_pos{};
		};

		std::unordered_map<const char*, gsc_hook_t> vm_execute_hooks;
		utils::hook::detour scr_player_killed_hook;
		utils::hook::detour scr_player_damage_hook;

		utils::hook::detour client_command_hook;

		utils::hook::detour g_log_printf_hook;

		std::vector<scripting::function> say_callbacks;

		game::dvar_t* logfile;
		game::dvar_t* g_log;

		utils::hook::detour vm_execute_hook;
		char empty_function[2] = {0x32, 0x34}; // CHECK_CLEAR_PARAMS, END
		const char* target_function = nullptr;

		std::string get_weapon_name(unsigned int weapon, bool isAlternate)
		{
			char output[1024] = {0};
			game::BG_GetWeaponNameComplete(weapon, isAlternate, output, 1024);
			return output;
		}
		std::string convert_mod(const int meansOfDeath)
		{
			const auto value = reinterpret_cast<game::scr_string_t**>(0x10B5290_b)[meansOfDeath];
			const auto string = game::SL_ConvertToString(*value);
			return string;
		}

		void scr_player_killed_stub(game::gentity_s* self, const game::gentity_s* inflictor, 
			game::gentity_s* attacker, int damage, const int meansOfDeath, const unsigned int weapon, 
			const bool isAlternate, const float* vDir, const unsigned int hitLoc, int psTimeOffset, int deathAnimDuration)
		{
			if (game::environment::is_dedi())
			{
				if (self && (inflictor || attacker)) {
					if ((attacker->client || inflictor->client) && self->client) {
						if (attacker->client) {
							voice_chat_globals::last_killed_by[self->client] = attacker->client;
							voice_chat_globals::last_player_killed[attacker->client] = self->client;
						}
						else {
							voice_chat_globals::last_killed_by[self->client] = inflictor->client;
							voice_chat_globals::last_player_killed[inflictor->client] = self->client;
						}
					}
				}
				else if (self->client) {
					voice_chat_globals::last_killed_by[self->client] = nullptr;
				}
			}


			if (damage == 0) return;
			
			scr_player_killed_hook.invoke<void>(self, inflictor, attacker, damage, meansOfDeath, 
				weapon, isAlternate, vDir, hitLoc, psTimeOffset, deathAnimDuration);
		}

		void scr_player_damage_stub(game::gentity_s* self, const game::gentity_s* inflictor, 
			game::gentity_s* attacker, int damage, int dflags, const int meansOfDeath, 
			const unsigned int weapon, const bool isAlternate, const float* vPoint, 
			const float* vDir, const unsigned int hitLoc, const int timeOffset)
		{
			if (damage == 0) return;

			scr_player_damage_hook.invoke<void>(self, inflictor, attacker, damage, dflags, 
				meansOfDeath, weapon, isAlternate, vPoint, vDir, hitLoc, timeOffset);
		}

		unsigned int local_id_to_entity(unsigned int local_id)
		{
			const auto variable = game::scr_VarGlob->objectVariableValue[local_id];
			return variable.u.f.next;
		}

		bool execute_vm_hook(const char* pos)
		{
			if (vm_execute_hooks.find(pos) == vm_execute_hooks.end())
			{
				hook_enabled = true;
				return false;
			}

			if (!hook_enabled && pos > reinterpret_cast<char*>(vm_execute_hooks.size()))
			{
				hook_enabled = true;
				return false;
			}

			const auto& hook = vm_execute_hooks[pos];
			if (!hook.is_lua_hook)
			{
				target_function = hook.target_pos;
			}

			return true;
		}

		void vm_execute_stub(utils::hook::assembler& a)
		{
			const auto replace = a.newLabel();
			const auto end = a.newLabel();

			a.pushad64();

			a.mov(rcx, r14);
			a.call_aligned(execute_vm_hook);

			a.cmp(al, 0);
			a.jne(replace);

			a.popad64();
			a.jmp(end);

			a.bind(end);

			a.movzx(r15d, byte_ptr(r14));
			a.inc(r14);
			a.mov(dword_ptr(rbp, 0xA4), r15d);

			a.jmp(0x5111B3_b);

			a.bind(replace);

			a.popad64();
			a.mov(rax, qword_ptr(reinterpret_cast<int64_t>(&target_function)));
			a.mov(r14, rax);
			a.jmp(end);
		}

		void g_log_printf_stub(const char* fmt, ...)
		{
			if (!logfile->current.enabled)
			{
				return;
			}

			char va_buffer[0x400] = {0};

			va_list ap;
			va_start(ap, fmt);
			vsprintf_s(va_buffer, fmt, ap);
			va_end(ap);

			const auto file = g_log->current.string;
			const auto time = *game::level_time / 1000;

			utils::io::write_file(file, utils::string::va("%3i:%i%i %s",
				time / 60,
				time % 60 / 10,
				time % 60 % 10,
				va_buffer
			), true);
		}
	}

	void clear_callbacks()
	{
		vm_execute_hooks.clear();
	}

	void enable_vm_execute_hook()
	{
		hook_enabled = true;
	}

	void disable_vm_execute_hook()
	{
		hook_enabled = false;
	}

	bool client_command_stub(const int client_num)
	{
		auto self = &game::g_entities[client_num];
		char cmd[1024] = {0};

		game::SV_Cmd_ArgvBuffer(0, cmd, 1024);

		auto hidden = false;
		if (cmd == "say"s || cmd == "say_team"s)
		{
			std::string message(game::ConcatArgs(1));
			message.erase(0, 1);

			for (const auto& callback : say_callbacks)
			{
				const auto entity_id = game::Scr_GetEntityId(client_num, 0);
				const auto result = callback(entity_id, {message, cmd == "say_team"s});

				if (result.is<int>() && !hidden)
				{
					hidden = result.as<int>() == 0;
				}
			}

			scheduler::once([cmd, message, self, hidden]()
			{
				const scripting::entity level{*game::levelEntityId};
				const scripting::entity player{game::Scr_GetEntityId(self->s.number, 0)};

				notify(level, cmd, {player, message, hidden});
				notify(player, cmd, {message, hidden});

				game::G_LogPrintf("%s;%s;%i;%s;%s\n",
					cmd,
					player.call("getguid").as<const char*>(),
					player.call("getentitynumber").as<int>(),
					player.get("name").as<const char*>(),
					message.data());
			}, scheduler::pipeline::server);

			if (hidden)
			{
				return false;
			}
		}

		return true;
	}

	void set_gsc_hook(const char* source, const char* target)
	{
		gsc_hook_t hook;
		hook.is_lua_hook = false;
		hook.target_pos = target;
		vm_execute_hooks[source] = hook;
	}

	void clear_hook(const char* pos)
	{
		vm_execute_hooks.erase(pos);
	}

	size_t get_hook_count()
	{
		return vm_execute_hooks.size();
	}

	class component final : public component_interface
	{
	public:
		void post_unpack() override
		{
			utils::hook::jump(0x5111A5_b, utils::hook::assemble(vm_execute_stub), true);

			scr_player_damage_hook.create(0x1CE780_b, scr_player_damage_stub);
			scr_player_killed_hook.create(0x1CEA60_b, scr_player_killed_stub);

			// Reimplement game log
			scheduler::once([]()
			{
				logfile = dvars::register_bool("logfile", true, game::DVAR_FLAG_NONE, "Enable game logging");
				g_log = dvars::register_string("g_log", "hmw-mod\\logs\\games_mp.log", game::DVAR_FLAG_NONE, "Log file path");
			}, scheduler::pipeline::main);
			g_log_printf_hook.create(game::G_LogPrintf, g_log_printf_stub);

			gsc::function::add("onplayersay", [](const gsc::function_args& args)
			{
				const auto function = args[0].as<scripting::function>();
				say_callbacks.push_back(function);
				return scripting::script_value{};
			});

			scripting::on_shutdown([](bool /*free_scripts*/, bool post_shutdown)
			{
				if (!post_shutdown)
				{
					say_callbacks.clear();
				}
			});
		}
	};
}

REGISTER_COMPONENT(logfile::component)
