/*
 * Copyright (c) 2019-2020 Nicolas Szabo <nszabo@vivaldi.net>
 * Copyright (c) 2020-2021 David Keogh <davidtkeogh@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef THEME_HPP
#define THEME_HPP

#define DEFAULT_THEME ".asmi-taskbar-window.overview { background-color: @menu_bgcolor; margin: 0px; padding: 0px; border: 0px; border-radius: 0px; }\n" \
					  ".asmi-taskbar-window.overview .overview-scrolled { background-color: transparent; border: 0px; }\n"                        \
					  ".asmi-taskbar-window .menu { margin: 0px; padding: 0px; border: 0px; background-color: @menu_bgcolor; }\n"                        \
					  ".asmi-taskbar-window .menu_item grid { margin: 0px; padding:4px; }\n"                                                 \
					  ".asmi-taskbar-window .menu_item .preview { margin:2px 0px }\n"                                                        \
					  ".asmi-taskbar-window .hover_menu_item { background-color: alpha(@menu_item_color_hover, 0.2); }\n"                    \
					  ".asmi-taskbar-window .dragging_menu_item { background-color: alpha(@menu_item_color_hover, 0.35); }\n"                  \
					  ".asmi-taskbar-window .drop_target_menu_item { background-color: alpha(@menu_item_color_hover, 0.5); }\n"                \
					  ".asmi-taskbar-window .menu.asmi-taskbar-previews.asmi-taskbar-vertical .menu_item:not(:first-child) { border-top: 1px solid alpha(@menu_item_color, 0.25); }\n"     \
					  ".asmi-taskbar-window .menu.asmi-taskbar-previews.asmi-taskbar-horizontal .menu_item:not(:first-child) { border-left: 1px solid alpha(@menu_item_color, 0.25); }\n";

#define LAUNCHER_COUNT_THEME "#asmi-taskbar-plugin .launcher_count { "         \
							 "color: @launcher_count_fgcolor; "            \
							 "background-color: @launcher_count_bgcolor; " \
							 "border-radius: 999px; "                      \
							 "min-width: 12px; "                           \
							 "font-weight: bold; "                         \
							 "padding: 1px 4px; "                          \
							 "margin: 1px; }\n";

#define OVERVIEW_ALT_THEME ".asmi-taskbar-window .menu_item.overview_alt { background-color: transparent; }\n"                                                       \
						   ".asmi-taskbar-window .menu_item.overview_alt grid { padding: 0px; }\n"                                                       \
						   ".asmi-taskbar-window .menu_item.overview_alt .preview { margin: 0px; }\n"                                                    \
						   ".asmi-taskbar-window .menu_item.overview_alt .title { color: @menu_item_color; background-color: alpha(@menu_item_color, 0.15); " \
						   "border: 1px solid alpha(@menu_item_color, 0.20); border-radius: 999px; padding: 2px 10px; }\n"                               \
						   ".asmi-taskbar-window .menu_item.overview_alt .overview_close { color: #ffffff; "                                             \
						   "background-color: rgba(0, 0, 0, 0.55); border: 1px solid rgba(255, 255, 255, 0.15); "                                       \
						   "border-radius: 999px; min-width: 24px; min-height: 24px; padding: 2px; }\n"                                                 \
						   ".asmi-taskbar-window .menu_item.overview_alt .overview_close:hover { background-color: rgba(0, 0, 0, 0.75); }\n";

#include "Dock.hpp"

#include <gtk/gtk.h>

#include <iostream>
#include <string>

namespace Theme
{
	void init();
	void load();
	std::string get_theme_colors();
} // namespace Theme

#endif // THEME_HPP
