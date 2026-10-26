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

#include "Dock.hpp"
#include "GroupMenuItem.hpp"
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
// The overview lets the title ellipsize to the preview width instead of
// keeping the fixed width it has in the dock menu, so long titles never widen
// the item.
static const int OVERVIEW_LABEL_MAX_CHARS = 1;

namespace Overview
{
	namespace
	{
		GtkWidget* mWindow = nullptr;
		GtkWidget* mFlowBox = nullptr;
		GtkWidget* mContent = nullptr;
		Help::Gtk::Timeout mPollTimeout;
		gulong mWindowOpenedId = 0;
		gulong mWindowClosedId = 0;
		gulong mActiveWindowChangedId = 0;
		bool mVisible = false;
		gint64 mCornerSince = -1;
		// Window closed from the overview. Kept for a short grace period so the
		// window-closed and active-window-changed signals that follow the close
		// do not dismiss the overview.
		XfwWindow* mKeepOpenWindow = nullptr;
		Help::Gtk::Timeout mCloseGraceTimeout;

		GdkMonitor* pointerMonitor(gint pointerX, gint pointerY);

		GtkWidget* createWindow()
		{
			GtkWidget* window = gtk_window_new(GTK_WINDOW_POPUP);
			gtk_window_set_default_size(GTK_WINDOW(window), 1, 1);

			return window;
		}

		// Reuse the dock item widgets (with their close button and preview) by
		// reparenting them into the overview.
		void addWindowItem(GroupWindow* window)
		{
			GroupMenuItem* menuItem = window->mGroupMenuItem;
			bool alternativeStyle = Settings::alternativeOverviewStyle && Settings::showPreviews;

			menuItem->mInOverview = true;

			// Apply the layout first: this also sets the alternative flag, so the
			// preview below is rendered with the proportional path when needed.
			menuItem->setOverviewAlternative(alternativeStyle);
			menuItem->updateLabel();

			if (Settings::showPreviews)
			{
				menuItem->updatePreview();
				gtk_label_set_ellipsize(menuItem->mLabel, PANGO_ELLIPSIZE_END);

				// In the default overview the title is reduced to the preview
				// width; the alternative style sizes it to the preview instead.
				if (!alternativeStyle)
					gtk_label_set_max_width_chars(menuItem->mLabel, OVERVIEW_LABEL_MAX_CHARS);
			}

			GtkWidget* item = GTK_WIDGET(menuItem->mItem);

			GtkWidget* parent = gtk_widget_get_parent(item);
			if (parent != nullptr)
				gtk_container_remove(GTK_CONTAINER(parent), item);

			// Keep each preview at its natural (Preview size) dimensions
			// instead of letting the flow box stretch it to fill the cell.
			gtk_widget_set_halign(item, GTK_ALIGN_CENTER);
			gtk_widget_set_valign(item, GTK_ALIGN_CENTER);

			gtk_flow_box_insert(GTK_FLOW_BOX(mFlowBox), item, -1);
			gtk_widget_show(item);
		}

		bool populate()
		{
			bool added = false;

			Dock::forEachGroup([&added](Group* group) -> void {
				group->mWindows.forEach([&added](GroupWindow* window) -> void {
					addWindowItem(window);
					added = true;
				});
			});

			return added;
		}

		// Empty the flow box. The items themselves are kept alive by their
		// GroupMenuItem reference, so detaching them first prevents destroying a
		// live item when the wrapper is destroyed.
		void clearFlowBox()
		{
			GList* children = gtk_container_get_children(GTK_CONTAINER(mFlowBox));
			for (GList* it = children; it != nullptr; it = it->next)
			{
				GtkWidget* wrapper = GTK_WIDGET(it->data);
				GList* inner = gtk_container_get_children(GTK_CONTAINER(wrapper));
				for (GList* j = inner; j != nullptr; j = j->next)
					gtk_container_remove(GTK_CONTAINER(wrapper), GTK_WIDGET(j->data));
				g_list_free(inner);
				gtk_widget_destroy(wrapper);
			}
			g_list_free(children);
		}

		// Give the items back to their group menus. updateLayout() detaches each
		// item from its current parent (the overview) before packing it again.
		void restoreItems()
		{
			Dock::forEachGroup([](Group* group) -> void {
				group->mWindows.forEach([](GroupWindow* window) -> void {
					GtkWidget* item = GTK_WIDGET(window->mGroupMenuItem->mItem);
					window->mGroupMenuItem->mInOverview = false;
					window->mGroupMenuItem->setOverviewAlternative(false);
					gtk_widget_set_halign(item, GTK_ALIGN_FILL);
					gtk_widget_set_valign(item, GTK_ALIGN_FILL);
					gtk_label_set_max_width_chars(window->mGroupMenuItem->mLabel, -1);
				});
				group->mGroupMenu.updateLayout();
			});

			// updateLayout() only detaches the items from the flow box child
			// wrappers; the empty wrappers would otherwise pile up and push the
			// grid down on every opening.
			clearFlowBox();
		}

		// Choose a balanced number of columns for the alternative style so the
		// previews use both rows and columns instead of forming a single row.
		void updateGridLayout()
		{
			if (!(Settings::alternativeOverviewStyle && Settings::showPreviews))
			{
				// Keep the default overview's natural flow layout.
				gtk_flow_box_set_min_children_per_line(GTK_FLOW_BOX(mFlowBox), 1);
				gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(mFlowBox), G_MAXUINT);
				return;
			}

			int count = 0;
			Dock::forEachGroup([&count](Group* group) -> void {
				count += group->mWindows.size();
			});

			if (count <= 0)
				return;

			// 4 windows -> 2x2, 6 -> 3x2, 9 -> 3x3, and so on.
			int columns = 1;
			while (columns * columns < count)
				++columns;

			// Do not ask for more columns than fit across the monitor, or the
			// flow box would force them into a clipped row.
			GdkMonitor* monitor = gdk_display_get_primary_monitor(Plugin::mDisplay);
			if (Plugin::mPointer != nullptr)
			{
				gint pointerX, pointerY;
				gdk_device_get_position(Plugin::mPointer, nullptr, &pointerX, &pointerY);
				GdkMonitor* pointerMon = pointerMonitor(pointerX, pointerY);
				if (pointerMon != nullptr)
					monitor = pointerMon;
			}

			if (monitor != nullptr)
			{
				GdkRectangle geometry;
				gdk_monitor_get_geometry(monitor, &geometry);

				const int spacing = 6;
				int available = geometry.width - 2 * OVERVIEW_PADDING;
				int maxColumns = (available + spacing) / (Settings::previewWidth + spacing);
				columns = MIN(columns, MAX(1, maxColumns));
			}

			gtk_flow_box_set_min_children_per_line(GTK_FLOW_BOX(mFlowBox), columns);
			gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(mFlowBox), columns);
		}

		// Rebuild the overview after a window was closed from it. The remaining
		// items may have been pulled back into their (hidden) group menus by
		// GroupMenu::remove(), so repopulate the flow box from the live groups.
		void refresh()
		{
			clearFlowBox();

			if (!populate())
			{
				hide();
				return;
			}

			updateGridLayout();
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
		Help::Gtk::cssClassAdd(mWindow, "asmi-taskbar-window");
		Help::Gtk::cssClassAdd(mWindow, "overview");

		GtkWidget* scrolled = gtk_scrolled_window_new(nullptr, nullptr);
		gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
		Help::Gtk::cssClassAdd(scrolled, "overview-scrolled");
		gtk_widget_set_margin_start(scrolled, OVERVIEW_PADDING);
		gtk_widget_set_margin_end(scrolled, OVERVIEW_PADDING);
		gtk_widget_set_margin_top(scrolled, OVERVIEW_PADDING);
		gtk_widget_set_margin_bottom(scrolled, OVERVIEW_PADDING);
		gtk_container_add(GTK_CONTAINER(mWindow), scrolled);

		// Center the grid in the viewport. The flow box itself only takes its
		// natural size in the alternative style, so it can sit in the middle of
		// the screen instead of hugging the top-left corner.
		mContent = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
		gtk_widget_set_hexpand(mContent, TRUE);
		gtk_widget_set_vexpand(mContent, TRUE);
		gtk_container_add(GTK_CONTAINER(scrolled), mContent);

		mFlowBox = gtk_flow_box_new();
		// Keep the flow box alive while it is moved between the packed layout
		// and the centered one.
		g_object_ref_sink(mFlowBox);
		gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(mFlowBox), GTK_SELECTION_NONE);
		// Equal-sized cells keep the grid regular even though previews and
		// windows without a preview have different natural sizes.
		gtk_flow_box_set_homogeneous(GTK_FLOW_BOX(mFlowBox), TRUE);
		gtk_flow_box_set_column_spacing(GTK_FLOW_BOX(mFlowBox), 6);
		gtk_flow_box_set_row_spacing(GTK_FLOW_BOX(mFlowBox), 6);
		Help::Gtk::cssClassAdd(mFlowBox, "menu");
		gtk_box_pack_start(GTK_BOX(mContent), mFlowBox, TRUE, TRUE, 0);
		gtk_widget_show(scrolled);
		gtk_widget_show(mContent);
		gtk_widget_show(mFlowBox);

		mPollTimeout.setup(POLL_INTERVAL, []() {
			return poll();
		});

		// Forget the window that was closed from the overview once the signals
		// caused by its destruction have been processed.
		mCloseGraceTimeout.setup(1000, []() {
			mKeepOpenWindow = nullptr;
			return false;
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

		// dismiss the overview whenever the set of open windows changes, so the
		// items never refer to a destroyed window, and when a window is activated
		// from the overview
		mWindowOpenedId = g_signal_connect(G_OBJECT(Xfw::mXfwScreen), "window-opened",
			G_CALLBACK(+[](XfwScreen* screen, XfwWindow* xfwWindow) { hide(); }),
			nullptr);
		mWindowClosedId = g_signal_connect(G_OBJECT(Xfw::mXfwScreen), "window-closed",
			G_CALLBACK(+[](XfwScreen* screen, XfwWindow* xfwWindow) {
				// A window closed from the overview must not dismiss it: rebuild
				// the grid with the remaining windows instead.
				if (xfwWindow == mKeepOpenWindow)
				{
					if (mVisible)
						refresh();
					return;
				}

				hide();
			}),
			nullptr);
		mActiveWindowChangedId = g_signal_connect(G_OBJECT(Xfw::mXfwScreen), "active-window-changed",
			G_CALLBACK(+[](XfwScreen* screen, XfwWindow* previousActiveWindow) {
				// Closing a window moves the focus to another one; that focus
				// change belongs to the close and must not dismiss the overview.
				// The previous window may be reported as NULL on some backends.
				if (mKeepOpenWindow != nullptr
					&& (previousActiveWindow == nullptr || previousActiveWindow == mKeepOpenWindow))
					return;

				hide();
			}),
			nullptr);

		updateSettings();
	}

	void finalize()
	{
		mPollTimeout.stop();
		mCloseGraceTimeout.stop();
		mKeepOpenWindow = nullptr;
		ungrabKeyboard();

		if (mWindowOpenedId != 0)
			g_signal_handler_disconnect(Xfw::mXfwScreen, mWindowOpenedId);
		if (mWindowClosedId != 0)
			g_signal_handler_disconnect(Xfw::mXfwScreen, mWindowClosedId);
		if (mActiveWindowChangedId != 0)
			g_signal_handler_disconnect(Xfw::mXfwScreen, mActiveWindowChangedId);

		mWindowOpenedId = 0;
		mWindowClosedId = 0;
		mActiveWindowChangedId = 0;

		if (mWindow != nullptr)
		{
			gtk_widget_destroy(mWindow);
			mWindow = nullptr;
			mContent = nullptr;
		}

		if (mFlowBox != nullptr)
		{
			g_object_unref(mFlowBox);
			mFlowBox = nullptr;
		}
	}

	void show()
	{
		if (mVisible)
			return;

		bool alternativeStyle = Settings::alternativeOverviewStyle && Settings::showPreviews;
		gtk_flow_box_set_homogeneous(GTK_FLOW_BOX(mFlowBox), !alternativeStyle);

		if (alternativeStyle)
		{
			// Let the grid take its natural size and center it in the viewport
			// instead of leaving it in the top-left corner.
			gtk_widget_set_halign(mFlowBox, GTK_ALIGN_CENTER);
			gtk_widget_set_valign(mFlowBox, GTK_ALIGN_CENTER);
			gtk_widget_set_hexpand(mFlowBox, FALSE);
			gtk_widget_set_vexpand(mFlowBox, FALSE);

			if (gtk_box_get_center_widget(GTK_BOX(mContent)) != mFlowBox)
			{
				GtkWidget* parent = gtk_widget_get_parent(mFlowBox);
				if (parent != nullptr)
					gtk_container_remove(GTK_CONTAINER(parent), mFlowBox);
				gtk_box_set_center_widget(GTK_BOX(mContent), mFlowBox);
			}
		}
		else
		{
			gtk_widget_set_halign(mFlowBox, GTK_ALIGN_FILL);
			gtk_widget_set_valign(mFlowBox, GTK_ALIGN_FILL);
			gtk_widget_set_hexpand(mFlowBox, TRUE);
			gtk_widget_set_vexpand(mFlowBox, TRUE);

			if (gtk_box_get_center_widget(GTK_BOX(mContent)) == mFlowBox)
			{
				gtk_container_remove(GTK_CONTAINER(mContent), mFlowBox);
				gtk_box_pack_start(GTK_BOX(mContent), mFlowBox, TRUE, TRUE, 0);
			}
		}

		if (!populate())
			return;

		updateGridLayout();
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
		mKeepOpenWindow = nullptr;
		mCloseGraceTimeout.stop();
		ungrabKeyboard();
		gtk_widget_hide(mWindow);
		restoreItems();
	}

	void closeFromOverview(GroupWindow* window)
	{
		if (!mVisible)
		{
			Xfw::close(window, 0);
			return;
		}

		// Remember which window is being closed so the signals emitted while it
		// disappears keep the overview open and trigger a refresh.
		mKeepOpenWindow = window->mXfwWindow;
		mCloseGraceTimeout.start();
		Xfw::close(window, 0);
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
