#include "stdafx.h"
#include "overlay_home_menu_cheats.h"

#include "Emu/IdManager.h"
#include "Emu/System.h"
#include "Utilities/bin_patch.h"

namespace rsx
{
	namespace overlays
	{
		home_menu_cheat_row::home_menu_cheat_row(std::string_view text, bool enabled, u16 width)
		{
			const u16 toggle_width = menu_checkbox_size * 2;

			auto title = std::make_unique<label>(text);
			title->set_font(14);
			title->set_wrap_text(true);
			title->align_text(text_align::left);
			title->back_color.a = 0.f;
			title->set_size(width - menu_entry_margin - toggle_width - (menu_entry_margin / 2), menu_entry_height);

			auto toggle = std::make_unique<switchbox>();
			toggle->set_size(toggle_width, menu_checkbox_size);

			// horizontal_layout::add_element() keeps whatever y the item already has, so this is
			// how the switch gets vertically centered in the taller row.
			toggle->set_pos(0, (menu_entry_height - menu_checkbox_size) / 2);

			auto_resize = false;
			pack_padding = 15;
			set_size(width, menu_entry_height);

			// Packed from the left with a margin, like the settings rows do it.
			advance_pos = menu_entry_margin / 2;

			add_element(title);
			m_toggle = add_element(toggle);
			m_toggle->set_checked(enabled);

			m_enabled = enabled;
		}

		void home_menu_cheat_row::set_enabled(bool enabled)
		{
			m_enabled = enabled;

			if (m_toggle)
			{
				m_toggle->set_checked(enabled);

				// Invalidating the switch is enough: layout_container::is_compiled() walks the
				// children, so this reaches the list view and the page without any extra poke.
				m_toggle->refresh();
			}
		}

		home_menu_cheats::home_menu_cheats(s16 x, s16 y, u16 width, u16 height, bool use_separators, home_menu_page* parent)
			: home_menu_page(x, y, width, height, use_separators, parent, "Cheats")
		{
			collect_entries();

			if (m_cheats.empty())
			{
				add_item(home_menu::fa_icon::none, "No patches are loaded for this title.", [](pad_button) -> page_navigation
				{
					return page_navigation::stay;
				});
			}
			else
			{
				add_item(home_menu::fa_icon::none, "Saved as you toggle. Takes effect after Restart.", [](pad_button) -> page_navigation
				{
					return page_navigation::stay;
				});

				for (usz i = 0; i < m_cheats.size(); i++)
				{
					add_row(i, width);
				}
			}

			apply_layout();
		}

		void home_menu_cheats::collect_entries()
		{
			const std::string title_id = Emu.GetTitleID();

			if (title_id.empty())
			{
				return;
			}

			const std::string app_version = Emu.GetAppVersion();
			const patch_engine* patches = g_fxo->try_get<patch_engine>();

			if (!patches)
			{
				return;
			}

			for (const auto& [hash, container] : patches->get_loaded_patches())
			{
				for (const auto& [description, info] : container.patch_info_map)
				{
					// info.titles is keyed by game title, then by serial, then by app version.
					// "All" is the wildcard in each of the last two.
					for (const auto& [title, serials] : info.titles)
					{
						for (const auto& [serial, versions] : serials)
						{
							if (serial != "All" && serial != title_id)
							{
								continue;
							}

							for (const auto& [version, config_values] : versions)
							{
								if (version != "All" && version != app_version)
								{
									continue;
								}

								entry e;
								e.hash = hash;
								e.description = description;
								e.title = title;
								e.serial = serial;
								e.app_version = version;
								e.enabled = config_values.enabled;
								m_cheats.push_back(std::move(e));
							}
						}
					}
				}
			}

			// m_map is an unordered_map, so give the list a stable order of its own.
			std::sort(m_cheats.begin(), m_cheats.end(), [](const entry& a, const entry& b)
			{
				return a.description < b.description;
			});
		}

		void home_menu_cheats::add_row(usz index, u16 width)
		{
			auto row = std::make_unique<home_menu_cheat_row>(m_cheats[index].description, m_cheats[index].enabled, width);
			home_menu_cheat_row* raw = row.get();

			std::unique_ptr<overlay_element> element = std::move(row);

			add_item(element, [this, index, raw](pad_button btn) -> page_navigation
			{
				bool value = m_cheats[index].enabled;

				switch (btn)
				{
				case pad_button::cross:
				case pad_button::dpad_left:
				case pad_button::dpad_right:
				case pad_button::ls_left:
				case pad_button::ls_right:
					value = !value;
					break;
				case pad_button::select:
					value = false; // The default for every patch
					break;
				default:
					return page_navigation::stay;
				}

				if (value == m_cheats[index].enabled)
				{
					return page_navigation::stay;
				}

				m_cheats[index].enabled = value;
				raw->set_enabled(value);
				persist();

				rsx_log.notice("Cheats: '%s' %s", m_cheats[index].description, value ? "enabled" : "disabled");
				return page_navigation::stay;
			});
		}

		void home_menu_cheats::persist()
		{
			// Read the file back and merge, rather than writing m_map: m_map holds only the patches
			// this title and patch.yml contributed, so saving it directly would drop enable flags
			// belonging to any other title.
			patch_engine::patch_map config = patch_engine::load_config();

			for (const entry& e : m_cheats)
			{
				config[e.hash].patch_info_map[e.description].titles[e.title][e.serial][e.app_version].enabled = e.enabled;
			}

			// save_config() writes exactly the entries that are enabled, so a cheat switched off
			// here disappears from the file on the next write, which is the intended default.
			patch_engine::save_config(config);
		}
	}
}
