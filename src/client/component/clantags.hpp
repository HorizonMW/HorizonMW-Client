#pragma once

namespace clantags
{
	struct clan_tag
	{
		std::string short_name;
		int width;
		int height;
	};

	static std::unordered_map<std::string, clan_tag> tags
	{
		{"H2M", {"h2", 64, 64}},
		{"SM2", {"sm", 77, 48}},
		{"VER", {"vr", 64, 64}}
	//{"HMW",	{"hm", 77, 77}}
	// Could add more tags here in the future
	};
}
