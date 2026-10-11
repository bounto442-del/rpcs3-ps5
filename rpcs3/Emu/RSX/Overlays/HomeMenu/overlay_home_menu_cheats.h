#pragma once

#include "Emu/RSX/Overlays/HomeMenu/overlay_home_menu_page.h"

namespace rsx
{
	namespace overlays
	{
		// One toggleable row: the patch description on the left, a switch on the right.
		struct home_menu_cheat_row : public horizontal_layout
		{
		public:
			home_menu_cheat_row(std::string_view text, bool enabled, u16 width);

			void set_enabled(bool enabled);
			bool is_enabled() const { return m_enabled; }

		private:
			switchbox* m_toggle = nullptr;
			bool m_enabled = false;
		};

		// Lists the patches loaded for the running title, so they can be toggled from the pad.
		//
		// RPCS3's own cheat manager is Qt (rpcs3qt/cheat_manager.cpp) and rpcs3qt is not built for
		// PS5, so without this page the only way to reach a cheat is to hand-edit patch_config.yml.
		// The rows are the title's patches as the patch engine loaded them, which means anything
		// dropped in patches/<TITLEID>_patch.yml shows up here with no further wiring.
		//
		// A toggle is written to patch_config.yml at once, but cannot affect the running session:
		// PPU patches are applied once, when the executable is loaded. The page says so, and the
		// Home Menu's own Restart entry is one press away.
		struct home_menu_cheats : public home_menu_page
		{
		public:
			home_menu_cheats(s16 x, s16 y, u16 width, u16 height, bool use_separators, home_menu_page* parent);

		private:
			// The coordinates a patch is enabled at. Mirrors the lookup in
			// patch_engine::apply(), Utilities/bin_patch.cpp:1500-1523.
			struct entry
			{
				std::string hash;
				std::string description;
				std::string title;
				std::string serial;
				std::string app_version;
				bool enabled = false;
			};

			void collect_entries();
			void add_row(usz index, u16 width);
			void persist();

			std::vector<entry> m_cheats;
		};
	}
}
