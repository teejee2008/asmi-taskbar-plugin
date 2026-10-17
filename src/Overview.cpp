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

#include "Overview.hpp"

#include "GroupWindow.hpp"
#include "Helpers.hpp"
#include "Plugin.hpp"
#include "Settings.hpp"
#include "Xfw.hpp"

#include <cstdlib>

// Mouse distance (in pixels) from the screen corner that triggers the overview.
static const int TRIGGER_MARGIN = 4;
// Pointer poll interval, in milliseconds.
static const int POLL_INTERVAL = 50;
// Space kept free around the preview grid.
static const int OVERVIEW_PADDING = 12;

namespace Overview
{
	namespace
	{
		GtkWidget* mWindow = nullptr;
		GtkWidget* mFlowBox = nullptr;
		Help::Gtk::Timeout mPollTimeout;
		gulong mWindowOpenedId = 0;
		gulong mWindowClosedId = 0;
		bool mVisible = false;
		gint64 mCornerSince = -1;

		GtkWidget* createWindow()
		{
			GtkWidget* window = gtk_window_new(GTK_WINDOW_POPUP);
			gtk_window_set_default_size(GTK_WINDOW(window), 1, 1);

			return window;
		}

		void clearItems()
		{
			GList* children = gtk_container_get_children(GTK_CONTAINER(mFlowBox));
			for (GList* it = children; it != nullptr; it = it->next)
				gtk_widget_destroy(GTK_WIDGET(it->data));
			g_list_free(children);
		}

		GtkWidget* createItem(GroupWindow* window)
		{
			GtkWidget* item = gtk_event_box_new();
			Help::Gtk::cssClassAdd(item, "menu_item");
			Help::Gtk::cssClassAdd(item, "overview_item");

			GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
			gtk_container_add(GTK_CONTAINER(item), box);

			GtkWidget* preview = gtk_image_new();
			Help::Gtk::cssClassAdd(preview, "preview");
			gtk_box_pack_start(GTK_BOX(box), preview, false, false, 0);

			gint scaleFactor = gtk_widget_get_scale_factor(preview);
			cairo_surface_t* surface = Xfw::getWindowPreview(window->mXfwWindow,
				Settings::previewWidth, Settings::previewHeight, scaleFactor);
			if (surface != nullptr)
			{
				gtk_image_set_from_surface(GTK_IMAGE(preview), surface);
				cairo_surface_destroy(surface);
			}

			GtkWidget* label = gtk_label_new(xfw_window_get_name(window->mXfwWindow));
			Help::Gtk::cssClassAdd(label, "title");
			gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_END);
			gtk_label_set_max_width_chars(GTK_LABEL(label), 30);
			gtk_box_pack_start(GTK_BOX(box), label, false, false, 0);

			if (Xfw::getActiveWindow() == window->mXfwWindow)
				Help::Gtk::cssClassAdd(item, "active_menu_item");

			g_signal_connect(G_OBJECT(item), "button-press-event",
				G_CALLBACK(+[](GtkWidget* widget, GdkEventButton* event, GroupWindow* w) {
					if (event->button == GDK_BUTTON_PRIMARY)
					{
						w->activate(event->time);
						hide();
					}
					else if (event->button == GDK_BUTTON_MIDDLE)
						Xfw::close(w, event->time);
					else if (event->button == GDK_BUTTON_SECONDARY)
					{
						GtkWidget* menu = GTK_WIDGET(g_object_ref_sink(xfw_window_action_menu_new(w->mXfwWindow)));
						xfce_panel_plugin_register_menu(Plugin::mXfPlugin, GTK_MENU(menu));
						g_signal_connect(G_OBJECT(menu), "deactivate",
							G_CALLBACK(+[](GtkMenuShell* _menu, gpointer _data) {
								g_object_unref(_menu);
							}),
							nullptr);
						gtk_menu_popup_at_pointer(GTK_MENU(menu), (GdkEvent*)event);
					}
					return true;
				}),
				window);

			g_signal_connect(G_OBJECT(item), "enter-notify-event",
				G_CALLBACK(+[](GtkWidget* widget, GdkEventCrossing* event, gpointer _data) {
					Help::Gtk::cssClassAdd(widget, "hover_menu_item");
					gtk_widget_queue_draw(widget);
					return true;
				}),
				nullptr);

			g_signal_connect(G_OBJECT(item), "leave-notify-event",
				G_CALLBACK(+[](GtkWidget* widget, GdkEvent* event, gpointer _data) {
					Help::Gtk::cssClassRemove(widget, "hover_menu_item");
					gtk_widget_queue_draw(widget);
					return true;
				}),
				nullptr);

			return item;
		}

		void populate()
		{
			clearItems();

			Xfw::mGroupWindows.forEach([](std::pair<XfwWindow*, std::shared_ptr<GroupWindow>> pair) -> void {
				gtk_flow_box_insert(GTK_FLOW_BOX(mFlowBox), createItem(pair.second.get()), -1);
			});

			gtk_widget_show_all(mFlowBox);
		}

		GdkMonitor* pointerMonitor(gint pointerX, gint pointerY)
		{
			GdkMonitor* monitor = gdk_display_get_monitor_at_point(Plugin::mDisplay, pointerX, pointerY);
			if (monitor == nullptr)
				monitor = gdk_display_get_primary_monitor(Plugin::mDisplay);

			return monitor;
		}

		bool getCornerPosition(gint pointerX, gint pointerY, gint* cornerX, gint* cornerY)
		{
			GdkMonitor* monitor = pointerMonitor(pointerX, pointerY);
			if (monitor == nullptr)
				return false;

			GdkRectangle geometry;
			gdk_monitor_get_geometry(monitor, &geometry);

			switch (Settings::hotCornerCorner)
			{
			case 1: // top right
				*cornerX = geometry.x + geometry.width - 1;
				*cornerY = geometry.y;
				break;
			case 2: // bottom left
				*cornerX = geometry.x;
				*cornerY = geometry.y + geometry.height - 1;
				break;
			case 3: // bottom right
				*cornerX = geometry.x + geometry.width - 1;
				*cornerY = geometry.y + geometry.height - 1;
				break;
			default: // top left
				*cornerX = geometry.x;
				*cornerY = geometry.y;
				break;
			}

			return true;
		}

		void positionWindow()
		{
			GdkMonitor* monitor = gdk_display_get_primary_monitor(Plugin::mDisplay);

			if (Plugin::mPointer != nullptr)
			{
				gint pointerX, pointerY;
				gdk_device_get_position(Plugin::mPointer, nullptr, &pointerX, &pointerY);
				GdkMonitor* pointerMon = pointerMonitor(pointerX, pointerY);
				if (pointerMon != nullptr)
					monitor = pointerMon;
			}

			if (monitor == nullptr)
				return;

			GdkRectangle geometry;
			gdk_monitor_get_geometry(monitor, &geometry);

			// fill the whole screen so scrollbars only appear once the grid
			// no longer fits
			gtk_window_resize(GTK_WINDOW(mWindow), geometry.width, geometry.height);
			gtk_window_move(GTK_WINDOW(mWindow), geometry.x, geometry.y);
		}

		bool pointerInsideWindow(gint pointerX, gint pointerY)
		{
			GdkWindow* gdkWindow = gtk_widget_get_window(mWindow);
			if (gdkWindow == nullptr)
				return false;

			gint winX, winY;
			gdk_window_get_origin(gdkWindow, &winX, &winY);
			gint winWidth = gdk_window_get_width(gdkWindow);
			gint winHeight = gdk_window_get_height(gdkWindow);

			return pointerX >= winX && pointerX < winX + winWidth
				&& pointerY >= winY && pointerY < winY + winHeight;
		}

		bool poll()
		{
			if (Plugin::mPointer == nullptr)
				return true;

			gint pointerX, pointerY;
			gdk_device_get_position(Plugin::mPointer, nullptr, &pointerX, &pointerY);

			gint cornerX, cornerY;
			if (!getCornerPosition(pointerX, pointerY, &cornerX, &cornerY))
				return true;

			bool inCorner = std::abs(pointerX - cornerX) <= TRIGGER_MARGIN
				&& std::abs(pointerY - cornerY) <= TRIGGER_MARGIN;

			if (!mVisible)
			{
				if (inCorner)
				{
					gint64 now = g_get_monotonic_time() / 1000;
					if (mCornerSince < 0)
						mCornerSince = now;

					if (now - mCornerSince >= Settings::hotCornerDelay)
						show();
				}
				else
					mCornerSince = -1;
			}
			else
			{
				mCornerSince = -1;

				if (!inCorner && !pointerInsideWindow(pointerX, pointerY))
					hide();
			}

			return true;
		}

		void grabKeyboard()
		{
			GdkSeat* seat = gdk_display_get_default_seat(Plugin::mDisplay);
			if (seat != nullptr)
				gdk_seat_grab(seat, gtk_widget_get_window(mWindow), GDK_SEAT_CAPABILITY_KEYBOARD,
					false, nullptr, nullptr, nullptr, nullptr);
		}

		void ungrabKeyboard()
		{
			GdkSeat* seat = gdk_display_get_default_seat(Plugin::mDisplay);
			if (seat != nullptr)
				gdk_seat_ungrab(seat);
		}
	} // namespace

	void init()
	{
		mWindow = createWindow();
		Help::Gtk::cssClassAdd(mWindow, "xfce-docklike-window");
		Help::Gtk::cssClassAdd(mWindow, "overview");

		GtkWidget* scrolled = gtk_scrolled_window_new(nullptr, nullptr);
		gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
		Help::Gtk::cssClassAdd(scrolled, "overview-scrolled");
		gtk_widget_set_margin_start(scrolled, OVERVIEW_PADDING);
		gtk_widget_set_margin_end(scrolled, OVERVIEW_PADDING);
		gtk_widget_set_margin_top(scrolled, OVERVIEW_PADDING);
		gtk_widget_set_margin_bottom(scrolled, OVERVIEW_PADDING);
		gtk_container_add(GTK_CONTAINER(mWindow), scrolled);

		mFlowBox = gtk_flow_box_new();
		gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(mFlowBox), GTK_SELECTION_NONE);
		gtk_flow_box_set_column_spacing(GTK_FLOW_BOX(mFlowBox), 6);
		gtk_flow_box_set_row_spacing(GTK_FLOW_BOX(mFlowBox), 6);
		Help::Gtk::cssClassAdd(mFlowBox, "menu");
		gtk_container_add(GTK_CONTAINER(scrolled), mFlowBox);
		gtk_widget_show(scrolled);
		gtk_widget_show(mFlowBox);

		mPollTimeout.setup(POLL_INTERVAL, []() {
			return poll();
		});

		// dismiss the overview on any key press, except for the arrow keys and
		// enter, which are reserved for navigating the overview
		gtk_widget_add_events(mWindow, GDK_KEY_PRESS_MASK);
		g_signal_connect(G_OBJECT(mWindow), "key-press-event",
			G_CALLBACK(+[](GtkWidget* widget, GdkEventKey* event, gpointer _data) {
				switch (event->keyval)
				{
				case GDK_KEY_Up:
				case GDK_KEY_Down:
				case GDK_KEY_Left:
				case GDK_KEY_Right:
				case GDK_KEY_Return:
				case GDK_KEY_KP_Enter:
				case GDK_KEY_ISO_Enter:
					return false;
				default:
					hide();
					return true;
				}
			}),
			nullptr);

		// grab the keyboard once the window is mapped so it receives the key
		// presses even as an override-redirect popup
		g_signal_connect(G_OBJECT(mWindow), "map-event",
			G_CALLBACK(+[](GtkWidget* widget, GdkEvent* event, gpointer _data) {
				grabKeyboard();
				return true;
			}),
			nullptr);

		// hide the overview whenever the set of open windows changes, so the
		// items never refer to a destroyed window
		mWindowOpenedId = g_signal_connect(G_OBJECT(Xfw::mXfwScreen), "window-opened",
			G_CALLBACK(+[](XfwScreen* screen, XfwWindow* xfwWindow) { hide(); }),
			nullptr);
		mWindowClosedId = g_signal_connect(G_OBJECT(Xfw::mXfwScreen), "window-closed",
			G_CALLBACK(+[](XfwScreen* screen, XfwWindow* xfwWindow) { hide(); }),
			nullptr);

		updateSettings();
	}

	void finalize()
	{
		mPollTimeout.stop();
		ungrabKeyboard();

		if (mWindowOpenedId != 0)
			g_signal_handler_disconnect(Xfw::mXfwScreen, mWindowOpenedId);
		if (mWindowClosedId != 0)
			g_signal_handler_disconnect(Xfw::mXfwScreen, mWindowClosedId);

		mWindowOpenedId = 0;
		mWindowClosedId = 0;

		if (mWindow != nullptr)
		{
			gtk_widget_destroy(mWindow);
			mWindow = nullptr;
			mFlowBox = nullptr;
		}
	}

	void show()
	{
		if (mVisible || Xfw::mGroupWindows.size() == 0)
			return;

		populate();
		positionWindow();
		mVisible = true;
		gtk_widget_show(mWindow);
		gtk_widget_show(mFlowBox);
	}

	void hide()
	{
		if (!mVisible)
			return;

		mVisible = false;
		ungrabKeyboard();
		gtk_widget_hide(mWindow);
	}

	void updateSettings()
	{
		if (!Settings::hotCornerEnabled)
		{
			mPollTimeout.stop();
			hide();
			return;
		}

		if (mPollTimeout.mTimeoutId == 0)
			mPollTimeout.start();
	}

	void toggle()
	{
		if (mVisible)
			hide();
		else
			show();
	}
} // namespace Overview
