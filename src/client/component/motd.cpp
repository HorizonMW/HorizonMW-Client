#include <std_include.hpp>
#include "loader/component_loader.hpp"

#include "console.hpp"
#include "materials.hpp"
#include "motd.hpp"
#include "images.hpp"
#include "command.hpp"
#include "scheduler.hpp"

#include "game/game.hpp"

#include <utils/string.hpp>
#include <utils/concurrency.hpp>
#include <utils/http.hpp>

namespace motd
{
	namespace
	{
		constexpr auto max_featured_tabs = 8;

		utils::concurrency::container<links_map_t> links;
		utils::concurrency::container<nlohmann::json, std::recursive_mutex> marketing;

		std::atomic_bool killed;

		std::optional<std::string> download_image(const std::string& url)
		{
			if (killed)
			{
				return {};
			}

			const auto res = utils::http::get_data_motd(url);
			return res;
		}

		void download_motd_image(nlohmann::json& data)
		{
			if (!data["motd"].is_object() || !data["motd"]["image_url"].is_string())
			{
				return;
			}

			const auto url = data["motd"]["image_url"].get<std::string>();
			const auto image_data = download_image(url);
			if (image_data.has_value())
			{
				const auto& image = image_data.value();
				images::override_texture("motd_image", image);
			}
		}

		void download_featured_tabs_images(nlohmann::json& data)
		{
			if (!data["featured"].is_array())
			{
				return;
			}

			auto index = 0;
			for (const auto& [key, tab] : data["featured"].items())
			{
				index++;
				if (index >= max_featured_tabs + 1)
				{
					return;
				}

				if (!tab.is_object() || !tab["image_url"].is_string())
				{
					continue;
				}

				const auto download_image_ = [&](const std::string& field, const std::string& image_name)
					{
						const auto url = tab[field].get<std::string>();
						const auto image_data = download_image(url);
						if (image_data.has_value())
						{
							const auto& image = image_data.value();
							images::override_texture(std::format("{}_{}", image_name, index), image);
						}
					};

				download_image_("image_url", "featured_panel");
				download_image_("thumbnail_url", "featured_panel_thumbnail");
			}
		}

		void download_images(nlohmann::json& data)
		{
			if (!data.is_object())
			{
				return;
			}

			download_motd_image(data);
			download_featured_tabs_images(data);
		}

		std::optional<std::string> get_server_file(const std::string& endpoint)
		{
			const auto try_url = [&](const std::string& base_url)
			{
				const auto url = base_url + endpoint;
				printf("[HTTP] GET file \"%s\"\n", url.data());
				const auto result = utils::http::get_data_motd(url);
				return result;
			};

			const auto result = try_url("https://price.horizonmw.org/");

			if (result.has_value())
			{
				return result;
			}

			return {};
		}

		void init_links(links_map_t& map)
		{
			map =
			{
				{"discord", "https://discord.gg/horizonmw"},
				{"website", "https://www.youtube.com/@horizonmw"},
			};
		}

		void add_links(nlohmann::json& data)
		{
			links.access([&](links_map_t& map)
				{
					init_links(map);
					if (!data.is_object() || !data["links"].is_object())
					{
						return;
					}

					for (const auto& [link, url] : data["links"].items())
					{
						if (!url.is_string())
						{
							continue;
						}

						map.insert(std::make_pair(link, url.get<std::string>()));
					}
				});
		}

		void init(bool load_images = true)
		{
			links.access([](links_map_t& map)
			{
				init_links(map);
			});

			marketing.access([&](nlohmann::json& data)
			{
				data.clear();

				const auto marketing_data = get_server_file("marketing2.json");
				if (marketing_data.has_value())
				{
					try
					{
						const auto& value = marketing_data.value();
						data = nlohmann::json::parse(value);

						add_links(data);
						if (load_images)
						{
							download_images(data);
						}
					}
					catch (const std::exception& e)
					{
						printf("Failed to load marketing.json: %s\n", e.what());
					}
				}
			});
		}
	}

	links_map_t get_links()
	{
		return links.access<links_map_t>([&](links_map_t& map)
		{
			return map;
		});
	}

	int get_num_featured_tabs()
	{
		return marketing.access<nlohmann::json>([&](nlohmann::json& data)
			-> nlohmann::json
		{
			if (!data.is_object() || !data["featured"].is_array())
			{
				return 0;
			}

			return std::min(max_featured_tabs, static_cast<int>(data["featured"].size()));
		});
	}

	nlohmann::json get_featured_tab(const int index)
	{
		return marketing.access<nlohmann::json>([&](nlohmann::json& data)
			-> nlohmann::json
			{
				if (!data.is_object() || !data["featured"].is_array())
				{
					return {};
				}

				if (index >= data["featured"].size())
				{
					return {};
				}

				return data["featured"][index];
			});
	}

	nlohmann::json get_motd()
	{
		return marketing.access<nlohmann::json>([](nlohmann::json& data)
			-> nlohmann::json
			{
				if (!data.is_object() || !data["motd"].is_object())
				{
					return {};
				}

				return data["motd"];
			});
	}

	bool has_motd()
	{
		return marketing.access<bool>([](nlohmann::json& data)
			-> nlohmann::json
			{
				return data.is_object() && data["motd"].is_object();
			});
	}

	std::thread init_thread;

	class component final : public component_interface
	{
	public:
		void post_load() override
		{
			if (game::environment::is_dedi())
			{
				return;
			}

			init_thread = std::thread([]
			{
				init();
			});
		}

		void post_unpack() override
		{
			if (game::environment::is_dedi())
			{
				return;
			}

			if (init_thread.joinable())
			{
				init_thread.join();
			}

			command::add("reloadmotd", []()
			{
				init(true);
			});

			command::add("reloadmotd_noimages", []()
			{
				init(false);
			});
		}

		void pre_destroy() override
		{
			if (game::environment::is_dedi())
			{
				return;
			}

			killed = true;
			if (init_thread.joinable())
			{
				init_thread.join();
			}
		}
	};
}

REGISTER_COMPONENT(motd::component)
