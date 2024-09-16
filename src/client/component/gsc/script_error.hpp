#pragma once

namespace gsc
{
	struct script_info_t
	{
		const char* script_start;
		std::string file;
		std::string function;
	};

	std::optional<script_info_t> find_function(const char* pos);
}
