#include <std_include.hpp>
#include "loader/component_loader.hpp"

#include "command.hpp"
#include "console.hpp"
#include "fastfiles.hpp"

#include "game/game.hpp"
#include "game/dvars.hpp"

#include <utils/hook.hpp>
#include <utils/memory.hpp>

namespace weapon
{
	namespace
	{
		utils::hook::detour g_setup_level_weapon_def_hook;
		void g_setup_level_weapon_def_stub()
		{
			// precache level weapons first
			g_setup_level_weapon_def_hook.invoke<void>();

			std::vector<game::WeaponDef*> weapons;

			// find all weapons in asset pools
			fastfiles::enum_assets(game::ASSET_TYPE_WEAPON, [&weapons](game::XAssetHeader header)
			{
				weapons.push_back(header.weapon);
			}, false);

			// sort weapons
			std::sort(weapons.begin(), weapons.end(), [](game::WeaponDef* weapon1, game::WeaponDef* weapon2)
			{
				return std::string_view(weapon1->name) <
					std::string_view(weapon2->name);
			});

			// precache items
			for (std::size_t i = 0; i < weapons.size(); i++)
			{
				//console::debug("precaching weapon \"%s\"\n", weapons[i]->name);
				game::G_GetWeaponForName(weapons[i]->name);
			}
		}

		utils::hook::detour xmodel_get_bone_index_hook;
		int xmodel_get_bone_index_stub(game::XModel* model, game::scr_string_t name, unsigned int offset, char* index)
		{
			auto result = xmodel_get_bone_index_hook.invoke<int>(model, name, offset, index);
			if (result)
			{
				return result;
			}

			const auto original_index = *index;
			const auto original_result = result;

			if (name == game::SL_FindString("tag_weapon_right") ||
				name == game::SL_FindString("tag_knife_attach"))
			{
				const auto tag_weapon = game::SL_FindString("tag_weapon");
				result = xmodel_get_bone_index_hook.invoke<int>(model, tag_weapon, offset, index);
				if (result)
				{
					console::debug("using tag_weapon instead of %s (%s, %d, %d)\n", game::SL_ConvertToString(name), model->name, offset, *index);
					return result;
				}
			}

			*index = original_index;
			result = original_result;

			return result;
		}

		void cw_mismatch_error_stub(int, const char* msg, ...)
		{
			char buffer[0x100];

			va_list ap;
			va_start(ap, msg);

			vsnprintf_s(buffer, sizeof(buffer), _TRUNCATE, msg, ap);

			va_end(ap);

			console::error(buffer);
		}

		int g_find_config_string_index_stub(const char* string, int start, int max, int create, const char* errormsg)
		{
			create = 1;
			return utils::hook::invoke<int>(0x8B530_b, string, start, max, create, errormsg); // G_FindConfigstringIndex
		}

		template <typename T>
		void set_weapon_field(const std::string& weapon_name, unsigned int field, T value)
		{
			auto weapon = game::DB_FindXAssetHeader(game::ASSET_TYPE_WEAPON, weapon_name.data(), false).data;
			if (weapon)
			{
				if (field && field < (0xE20 + sizeof(T)))
				{
					*reinterpret_cast<T*>(reinterpret_cast<std::uintptr_t>(weapon) + field) = value;
				}
				else
				{
					console::warn("weapon field: %d is higher than the size of weapon struct!\n", field);
				}
			}
			else
			{
				console::warn("weapon %s not found!\n", weapon_name.data());
			}
		}

		void set_weapon_field_float(const std::string& weapon_name, unsigned int field, float value)
		{
			set_weapon_field<float>(weapon_name, field, value);
		}

		void set_weapon_field_int(const std::string& weapon_name, unsigned int field, int value)
		{
			set_weapon_field<int>(weapon_name, field, value);
		}

		void set_weapon_field_bool(const std::string& weapon_name, unsigned int field, bool value)
		{
			set_weapon_field<bool>(weapon_name, field, value);
		}

		int compare_hash(const void* a, const void* b)
		{
			const auto hash_a = reinterpret_cast<game::DDLHash*>(
				reinterpret_cast<size_t>(a))->hash;
			const auto hash_b = reinterpret_cast<game::DDLHash*>(
				reinterpret_cast<size_t>(b))->hash;

			if (hash_a < hash_b)
			{
				return -1;
			}
			else if (hash_a > hash_b)
			{
				return 1;
			}

			return 0;
		}

		utils::memory::allocator ddl_allocator;
		std::unordered_set<void*> modified_enums;

		std::vector<const char*> get_stringtable_entries(const std::string& name)
		{
			std::vector<const char*> entries;

			const auto string_table = game::DB_FindXAssetHeader(
				game::ASSET_TYPE_STRINGTABLE, name.data(), false).stringTable;

			if (string_table == nullptr)
			{
				return entries;
			}

			for (auto row = 0; row < string_table->rowCount; row++)
			{
				if (string_table->columnCount <= 0)
				{
					continue;
				}

				const auto index = (row * string_table->columnCount);
				const auto weapon = string_table->values[index].string;
				entries.push_back(ddl_allocator.duplicate_string(weapon));
			}

			return entries;
		}

		void add_entries_to_enum(game::DDLEnum* enum_, const std::vector<const char*> entries)
		{
			if (entries.size() <= 0)
			{
				return;
			}

			const auto new_size = enum_->memberCount + entries.size();
			const auto members = ddl_allocator.allocate_array<const char*>(new_size);
			const auto hash_list = ddl_allocator.allocate_array<game::DDLHash>(new_size);

			std::memcpy(members, enum_->members, 8 * enum_->memberCount);
			std::memcpy(hash_list, enum_->hashTable.list, 8 * enum_->hashTable.count);

			for (auto i = 0; i < entries.size(); i++)
			{
				const auto hash = utils::hook::invoke<unsigned int>(0x794FB0_b, entries[i], 0);
				const auto index = enum_->memberCount + i;
				hash_list[index].index = index;
				hash_list[index].hash = hash;
				members[index] = entries[i];
			}

			std::qsort(hash_list, new_size, sizeof(game::DDLHash), compare_hash);

			enum_->members = members;
			enum_->hashTable.list = hash_list;
			enum_->memberCount = static_cast<int>(new_size);
			enum_->hashTable.count = static_cast<int>(new_size);
		}

		void load_ddl_asset_stub(game::DDLRoot** asset)
		{
			const auto root = *asset;
			if (!root->ddlDef)
			{
				return utils::hook::invoke<void>(0x39BE20_b, root);
			}

			auto ddl_def = root->ddlDef;
			while (ddl_def)
			{
				for (auto i = 0; i < ddl_def->enumCount; i++)
				{
					const auto enum_ = &ddl_def->enumList[i];
					if (modified_enums.contains(enum_))
					{
						continue;
					}

					if ((enum_->name == "WeaponStats"s || enum_->name == "Weapon"s))
					{
						const auto weapons = get_stringtable_entries("mp/customweapons.csv");
						add_entries_to_enum(enum_, weapons);
						modified_enums.insert(enum_);
					}

					if (enum_->name == "AttachmentBase"s)
					{
						const auto attachments = get_stringtable_entries("mp/customattachments.csv");
						add_entries_to_enum(enum_, attachments);
						modified_enums.insert(enum_);
					}
				}

				ddl_def = ddl_def->next;
			}

			utils::hook::invoke<void>(0x39BE20_b, asset);
		}

		void draw_clip_ammo_type_one(__int64 cgameGlob, game::rectDef_s* rect, unsigned int weapIdx, float* color, int alignment, game::PlayerHandIndex hand, game::DrawClipAmmoParams* drawParams)
		{
			auto ammoInClip = game::BG_GetAmmoInClip(cgameGlob, weapIdx, 0, hand);
			game::AmmoColor((void*)cgameGlob, color, weapIdx, 0, hand);
			auto clipSize = game::BG_GetClipSize(cgameGlob, weapIdx, 0);

			auto activePlacement = game::ScrPlace_GetActivePlacement();
			auto width = game::ScrPlace_ApplyXWithoutSplitScreenScaling(activePlacement, 6.6666665f, rect->horzAlign);
			auto height = game::ScrPlace_ApplyYWithoutSplitScreenScaling(activePlacement, 13.333333f, rect->vertAlign);

			auto adjustedValue = game::ScrPlace_ApplyXWithoutSplitScreenScaling(activePlacement, 1.7777778f, rect->horzAlign);
			game::ScrPlace_ApplyYWithoutSplitScreenScaling(activePlacement, 5.3333335f, rect->vertAlign);

			auto bulletX = rect->x - width;
			auto bulletY = rect->y;

			for (auto clipIdx = 0; clipIdx < clipSize; ++clipIdx)
			{
				if (clipIdx == ammoInClip)
				{
					*color = 0.30000001f;
					color[1] = 0.30000001f;
					color[2] = 0.30000001f;
				}

				game::R_AddCmdDrawStretchPic(bulletX, bulletY, width, height, 0.0f, 0.0f, 1.0f, 1.0f, color, drawParams->image);
				bulletX = bulletX - adjustedValue;
			}
		}

		utils::hook::detour draw_clip_ammo_hook;
		void draw_clip_ammo_stub(__int64 cgame_glob, game::rectDef_s* rect, unsigned int weapon_id, __int64 weapon_def, float* color, int alignment, game::PlayerHandIndex hand)
		{
			if (hand != game::PlayerHandIndex::WEAPON_HAND_LEFT)
			{
				draw_clip_ammo_hook.invoke<void>(cgame_glob, rect, weapon_id, weapon_def, color, alignment, hand);
				return;
			}
			
			const auto ammo_type = *(DWORD*)(weapon_def + 0x628);
			if (ammo_type == 1)
			{
				game::DrawClipAmmoParams params{};
				params.image = game::Material_RegisterHandle("h1_hud_weapwidget_ammopip_medium");
				draw_clip_ammo_type_one(cgame_glob, rect, weapon_id, color, alignment, hand, &params);
				return;
			}

			draw_clip_ammo_hook.invoke<void>(cgame_glob, rect, weapon_id, weapon_def, color, alignment, hand);
		}
	}

	void clear_modifed_enums()
	{
		modified_enums.clear();
	}

	class component final : public component_interface
	{
	public:
		void post_unpack() override
		{
			// precache all weapons that are loaded in zones
			g_setup_level_weapon_def_hook.create(0x462630_b, g_setup_level_weapon_def_stub);

			// use tag_weapon if tag_weapon_right or tag_knife_attach are not found on model
			xmodel_get_bone_index_hook.create(0x5C82B0_b, xmodel_get_bone_index_stub);
			// make custom weapon index mismatch not drop in CG_SetupCustomWeapon
			utils::hook::call(0x11B9AF_b, cw_mismatch_error_stub);
			// make weapon index mismatch not drop in CG_SetupWeapon
			utils::hook::call(0xF2195_b, cw_mismatch_error_stub);
			utils::hook::call(0x11BA1F_b, cw_mismatch_error_stub);

			// patch attachment configstring so it will create if not found
			utils::hook::call(0x41C595_b, g_find_config_string_index_stub);

			utils::hook::call(0x36B4D4_b, load_ddl_asset_stub);

			// fix akimbo pistol ownerdraw not working on left hand
			draw_clip_ammo_hook.create(0x2F15A0_b, draw_clip_ammo_stub);

			dvars::register_bool("sv_disableCustomClasses", false, game::DVAR_FLAG_REPLICATED, "Disable custom classes on server");

#ifdef DEBUG
			command::add("setWeaponFieldFloat", [](const command::params& params)
			{
				if (params.size() <= 3)
				{
					console::info("usage: setWeaponFieldInt <weapon> <field> <value>\n");
					return;
				}
				set_weapon_field_float(params.get(1), atoi(params.get(2)), static_cast<float>(atof(params.get(3))));
			});

			command::add("setWeaponFieldInt", [](const command::params& params)
			{
				if (params.size() <= 3)
				{
					console::info("usage: setWeaponFieldInt <weapon> <field> <value>\n");
					return;
				}
				set_weapon_field_int(params.get(1), atoi(params.get(2)), static_cast<int>(atoi(params.get(3))));
			});

			command::add("setWeaponFieldBool", [](const command::params& params)
			{
				if (params.size() <= 3)
				{
					console::info("usage: setWeaponFieldBool <weapon> <field> <value>\n");
					return;
				}
				set_weapon_field_bool(params.get(1), atoi(params.get(2)), static_cast<bool>(atoi(params.get(3))));
			});
#endif
		}
	};
}

REGISTER_COMPONENT(weapon::component)
