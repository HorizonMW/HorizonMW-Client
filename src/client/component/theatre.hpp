#pragma once

namespace theatre
{
	struct record
	{
		std::string name;
		std::string mapname;
		std::string gametype;
		std::string filename;
	};

	record* recording{};
}