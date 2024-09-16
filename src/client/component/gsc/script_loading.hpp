#pragma once
#include <xsk/gsc/engine/h1.hpp>

namespace gsc
{
	struct col_line_t
	{
		std::uint16_t line;
		std::uint16_t column;
	};

	extern std::unique_ptr<xsk::gsc::h1::context> gsc_ctx;

	void load_main_handles();
	void load_init_handles();

	game::ScriptFile* find_script(game::XAssetType type, const char* name, int allow_create_default);

	std::optional<std::map<std::uint32_t, col_line_t>*> get_script_devmap(const std::string& name);
}
