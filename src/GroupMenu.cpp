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

#ifdef ENABLE_WAYLAND
#include <gtk-layer-shell.h>
#endif

#include "Group.hpp"
#include "GroupMenu.hpp"
#include "GroupMenuItem.hpp"
#include "Plugin.hpp"

// Vertical space kept free above and below the preview popup.
static const int PREVIEW_MARGIN = 8;

static GtkWidget*
create_window()
{
	GtkWidget* window = gtk_window_new(GTK_WINDOW_POPUP);
	gtk_widget_add_events(window, GDK_SCROLL_MASK);
	gtk_window_set_default_size(GTK_WINDOW(window), 1, 1);

	gtk_widget_set_app_paintable(window, true);
	GdkScreen* screen = gtk_widget_get_screen(window);
	GdkVisual* visual = gdk_screen_get_rgba_visual(screen);
	if (visual != nullptr)
	{
		gtk_widget_set_visual(window, visual);
	}

#ifdef ENABLE_WAYLAND
	if (gtk_layer_is_supported())
	{
		gtk_layer_init_for_window(GTK_WINDOW(window));
		gtk_layer_set_exclusive_zone(GTK_WINDOW(window), -1);
		gtk_layer_set_anchor(GTK_WINDOW(window), GTK_LAYER_SHELL_EDGE_TOP, true);
		gtk_layer_set_anchor(GTK_WINDOW(window), GTK_LAYER_SHELL_EDGE_LEFT, true);
	}
#endif

	return window;
}

GroupMenu::GroupMenu(Group* dockButton)
{
	mGroup = dockButton;
	mVisible = false;
	mMouseHover = false;
	mWindow = create_window();
	mBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

	Help::Gtk::cssClassAdd(mWindow, "xfce-docklike-window");
	Help::Gtk::cssClassAdd(mBox, "menu");

	gtk_container_add(GTK_CONTAINER(mWindow), mBox);
	gtk_widget_show(mBox);

	mPopupIdle.setup([this]() {
		popup();
		return false;
	});

	//--------------------------------------------------

	g_signal_connect(G_OBJECT(mWindow), "enter-notify-event",
		G_CALLBACK(+[](GtkWidget* widget, GdkEvent* event, GroupMenu* me) {
			me->mMouseHover = true;

			if (Settings::showPreviews)
				me->mGroup->mWindows.forEach([](GroupWindow* w) -> void {
					w->mGroupMenuItem->updatePreview();
				});

			return true;
		}),
		this);

	g_signal_connect(G_OBJECT(mWindow), "leave-notify-event",
		G_CALLBACK(+[](GtkWidget* widget, GdkEventCrossing* event, GroupMenu* me) {
			if (event->detail == GDK_NOTIFY_INFERIOR)
			{
				return false;
			}
			me->mGroup->setMouseLeaveTimeout();
			me->mMouseHover = false;

			return true;
		}),
		this);

	g_signal_connect(G_OBJECT(mWindow), "scroll-event",
		G_CALLBACK(+[](GtkWidget* widget, GdkEventScroll* event, GroupMenu* me) {
			((Group*)me->mGroup)->scrollWindows(event->time, event->direction);
			return true;
		}),
		this);

	g_signal_connect(G_OBJECT(Plugin::mXfPlugin), "notify::scale-factor",
		G_CALLBACK(+[](GtkWidget* widget, GParamSpec* pspec, GroupMenu* me) {
			g_object_ref(me->mBox);
			gtk_container_remove(GTK_CONTAINER(me->mWindow), me->mBox);
			gtk_widget_destroy(me->mWindow);
			me->mWindow = create_window();
			gtk_container_add(GTK_CONTAINER(me->mWindow), me->mBox);
			g_object_unref(me->mBox);
		}),
		this);
}

GroupMenu::~GroupMenu()
{
	mPopupIdle.stop();
	gtk_widget_destroy(mWindow);
}

void GroupMenu::add(GroupMenuItem* menuItem)
{
	mItems.push_back(GTK_WIDGET(menuItem->mItem));
	updateLayout();

	if (mVisible)
		mPopupIdle.start();
}

void GroupMenu::remove(GroupMenuItem* menuItem)
{
	GtkWidget* item = GTK_WIDGET(menuItem->mItem);

	GtkWidget* parent = gtk_widget_get_parent(item);
	if (parent != nullptr)
		gtk_container_remove(GTK_CONTAINER(parent), item);

	mItems.erase(std::remove(mItems.begin(), mItems.end(), item), mItems.end());
	updateLayout();

	gtk_window_resize(GTK_WINDOW(mWindow), 1, 1);

	if (mGroup->mWindowsCount < (Settings::noWindowsListIfSingle ? 2 : 1))
		gtk_widget_hide(mWindow);

	if (mVisible)
		mPopupIdle.start();
}

void GroupMenu::popup()
{
	if (mGroup->mWindowsCount >= (Settings::noWindowsListIfSingle ? 2 : 1))
	{
		gint wx, wy;
		mVisible = true;

		// Update the previews before measuring and showing the window
		if (Settings::showPreviews)
		{
			mGroup->mWindows.forEach([](GroupWindow* w) -> void {
				w->mGroupMenuItem->updatePreview();
			});
		}

		updateLayout();
		gtk_window_resize(GTK_WINDOW(mWindow), 1, 1);

		xfce_panel_plugin_position_widget(Plugin::mXfPlugin, mWindow, mGroup->mButton, &wx, &wy);
		updatePosition(wx, wy);
		gtk_widget_show(mWindow);
	}
}

void GroupMenu::updateOrientation()
{
	XfcePanelPluginMode panelMode = xfce_panel_plugin_get_mode(Plugin::mXfPlugin);
	bool previewsHorizontal = Settings::showPreviews && panelMode == XFCE_PANEL_PLUGIN_MODE_HORIZONTAL;

	// On horizontal panels the previews form rows that stack vertically; on vertical
	// panels they form columns that sit side by side. The window list is a single column.
	if (Settings::showPreviews && !previewsHorizontal)
		gtk_orientable_set_orientation(GTK_ORIENTABLE(mBox), GTK_ORIENTATION_HORIZONTAL);
	else
		gtk_orientable_set_orientation(GTK_ORIENTABLE(mBox), GTK_ORIENTATION_VERTICAL);

	Help::Gtk::cssClassRemove(mBox, "docklike-horizontal");
	Help::Gtk::cssClassRemove(mBox, "docklike-vertical");
	Help::Gtk::cssClassRemove(mBox, "docklike-previews");
	Help::Gtk::cssClassAdd(mBox, previewsHorizontal ? "docklike-horizontal" : "docklike-vertical");

	if (Settings::showPreviews)
		Help::Gtk::cssClassAdd(mBox, "docklike-previews");
}

int GroupMenu::lineCapacity(bool horizontal)
{
	GdkDisplay* display = gdk_display_get_default();
	GdkWindow* window = mGroup->mButton != nullptr
		? gtk_widget_get_window(mGroup->mButton) : nullptr;
	GdkMonitor* monitor = window != nullptr
		? gdk_display_get_monitor_at_window(display, window)
		: gdk_display_get_primary_monitor(display);

	if (monitor == nullptr)
		return 1;

	GdkRectangle geometry;
	gdk_monitor_get_geometry(monitor, &geometry);

	gint available = (horizontal ? geometry.width : geometry.height) - 2 * PREVIEW_MARGIN;
	if (available <= 0)
		return 1;

	gint itemSize = 0;
	for (GtkWidget* item : mItems)
	{
		gint minimum, natural;
		if (horizontal)
			gtk_widget_get_preferred_width(item, &minimum, &natural);
		else
			gtk_widget_get_preferred_height(item, &minimum, &natural);
		itemSize = std::max(itemSize, natural);
	}

	if (itemSize <= 0)
		itemSize = horizontal ? Settings::previewWidth : Settings::previewHeight;

	return std::max(1, available / itemSize);
}

void GroupMenu::updateLayout()
{
	updateOrientation();

	bool previews = Settings::showPreviews;
	bool horizontal = previews
		&& xfce_panel_plugin_get_mode(Plugin::mXfPlugin) == XFCE_PANEL_PLUGIN_MODE_HORIZONTAL;

	// Measure the items before detaching them; only previews wrap.
	int capacity = previews ? lineCapacity(horizontal) : (int)mItems.size();
	if (capacity < 1)
		capacity = 1;

	// Detach the items; their GroupMenuItem keeps a reference to each widget.
	for (GtkWidget* item : mItems)
	{
		GtkWidget* parent = gtk_widget_get_parent(item);
		if (parent != nullptr)
			gtk_container_remove(GTK_CONTAINER(parent), item);
	}

	// Drop the previous lines; the items were detached above.
	GList* children = gtk_container_get_children(GTK_CONTAINER(mBox));
	for (GList* n = children; n != nullptr; n = n->next)
		gtk_widget_destroy(GTK_WIDGET(n->data));
	g_list_free(children);

	// Keep the first line closest to the panel; later lines grow away from it.
	XfceScreenPosition position = xfce_panel_plugin_get_screen_position(Plugin::mXfPlugin);
	bool fromEnd = horizontal ? xfce_screen_position_is_bottom(position)
							  : xfce_screen_position_is_right(position);
	GtkOrientation lineOrientation = horizontal ? GTK_ORIENTATION_HORIZONTAL : GTK_ORIENTATION_VERTICAL;

	GtkWidget* line = nullptr;
	for (size_t i = 0; i < mItems.size(); i++)
	{
		if (i % capacity == 0)
		{
			line = gtk_box_new(lineOrientation, 0);
			if (fromEnd)
				gtk_box_pack_end(GTK_BOX(mBox), line, false, true, 0);
			else
				gtk_box_pack_start(GTK_BOX(mBox), line, false, true, 0);
			gtk_widget_show(line);
		}
		gtk_box_pack_start(GTK_BOX(line), mItems[i], false, true, 0);
	}

	// Keep a line so an empty menu still has a valid size.
	if (line == nullptr)
	{
		line = gtk_box_new(lineOrientation, 0);
		gtk_box_pack_start(GTK_BOX(mBox), line, false, true, 0);
		gtk_widget_show(line);
	}
}

void GroupMenu::updatePosition(gint wx, gint wy)
{
	GdkScreen* screen;
	GdkRectangle geometry;
	GdkDisplay* display;
	GdkMonitor* monitor;

	screen = gtk_widget_get_screen(mGroup->mButton);
	display = gdk_screen_get_display(screen);
	monitor = gdk_display_get_monitor_at_window(display, gtk_widget_get_window(mGroup->mButton));
	gdk_monitor_get_geometry(monitor, &geometry);

	gint window_width, window_height;
	gtk_window_get_size(GTK_WINDOW(mWindow), &window_width, &window_height);

	gint button_width = gtk_widget_get_allocated_width(mGroup->mButton);
	gint button_height = gtk_widget_get_allocated_height(mGroup->mButton);

	XfcePanelPluginMode panelMode = xfce_panel_plugin_get_mode(Plugin::mXfPlugin);

	if (panelMode == XFCE_PANEL_PLUGIN_MODE_HORIZONTAL)
	{
		if (wx != geometry.x + geometry.width - window_width)
		{
			wx -= (window_width / 2) - (button_width / 2);
			wx = wx < geometry.x ? geometry.x : wx;
		}
	}
	else
	{
		if (wy != geometry.y + geometry.height - window_height)
		{
			wy -= (window_height / 2) - (button_height / 2);
			wy = wy < geometry.y ? geometry.y : wy;
		}
	}

	int gap = Settings::previewGap;

	if (gap > 0)
	{
		XfceScreenPosition position = xfce_panel_plugin_get_screen_position(Plugin::mXfPlugin);

		if (xfce_screen_position_is_top(position))
			wy += gap;
		else if (xfce_screen_position_is_bottom(position))
			wy -= gap;
		else if (xfce_screen_position_is_left(position))
			wx += gap;
		else if (xfce_screen_position_is_right(position))
			wx -= gap;
	}

#ifdef ENABLE_WAYLAND
	if (gtk_layer_is_supported())
	{
		gtk_layer_set_monitor(GTK_WINDOW(mWindow), monitor);
		gtk_layer_set_margin(GTK_WINDOW(mWindow), GTK_LAYER_SHELL_EDGE_LEFT, wx - geometry.x);
		gtk_layer_set_margin(GTK_WINDOW(mWindow), GTK_LAYER_SHELL_EDGE_TOP, wy - geometry.y);
	}
	else
#endif
	{
		gtk_window_move(GTK_WINDOW(mWindow), wx, wy);
	}
}

void GroupMenu::reposition()
{
	if (!mVisible)
		return;

	gint wx, wy;
	xfce_panel_plugin_position_widget(Plugin::mXfPlugin, mWindow, mGroup->mButton, &wx, &wy);
	updatePosition(wx, wy);
}

void GroupMenu::hide()
{
	mVisible = false;
	gtk_widget_hide(mWindow);
}

void GroupMenu::showPreviewsChanged()
{
	mGroup->mWindows.forEach([](GroupWindow* w) -> void {
		gtk_widget_set_visible(GTK_WIDGET(w->mGroupMenuItem->mPreview), Settings::showPreviews);
		w->mGroupMenuItem->mPreviewTimeout.stop();
	});
	gtk_window_resize(GTK_WINDOW(mWindow), 1, 1);
}

uint GroupMenu::getPointerDistance()
{
	gint wx, wy, ww, wh, px, py;
	guint dx, dy;
	dx = dy = 0;

	gtk_window_get_position(GTK_WINDOW(mWindow), &wx, &wy);
	gtk_window_get_size(GTK_WINDOW(mWindow), &ww, &wh);
	gdk_device_get_position(Plugin::mPointer, nullptr, &px, &py);

	if (px < wx)
		dx = wx - px;
	else if (px > wx + ww)
		dx = px - (wx + ww);

	if (py < wy)
		dy = wy - py;
	else if (py > wy + wh)
		dy = py - (wy + wh);

	return std::max(dx, dy);
}
