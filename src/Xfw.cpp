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

#include "Xfw.hpp"

#include <libxfce4ui/libxfce4ui.h>

#include <unordered_set>

#ifdef ENABLE_X11
#include <gdk/gdkx.h>
#include <libxfce4windowing/xfw-x11.h>
#endif

namespace Xfw
{
	XfwScreen* mXfwScreen;
	XfwWorkspaceGroup* mXfwWorkspaceGroup = nullptr;
	Store::KeyStore<XfwWindow*, std::shared_ptr<GroupWindow>> mGroupWindows;
	// clang-format off
	std::unordered_set<std::string> mInvalidClassIds = {
		// https://github.com/teejee2008/asmi-taskbar-plugin/issues/75
		"wine",
		// https://github.com/teejee2008/asmi-taskbar-plugin/issues/21
  // In theory, the "crx_*" fix below for #118 should cover all these cases,
  // but it doesn't hurt to keep this as well, just in case…
		"brave-browser", "chromium", "google-chrome", "vivaldi-stable", "microsoft-edge",
	};
	// clang-format on
	XfwWindow* mPreviousActiveWindow = nullptr;

	namespace // private:
	{
		std::string getGroupNameSys(XfwWindow* xfwWindow)
		{
			// Xfw method const char *
			const gchar* const* class_ids = xfw_window_get_class_ids(xfwWindow);
			if (!xfce_str_is_empty(class_ids[0]))
			{
				// Chromium browsers use "crx_<appid>" as the instance-id for installed web
				// apps/PWAs, regardless of the browser (fork) name, so prefer it directly.
				// https://github.com/teejee2008/asmi-taskbar-plugin/issues/118
				if (!xfce_str_is_empty(class_ids[1]) && g_str_has_prefix(class_ids[1], "crx_"))
					return class_ids[1];

				// On X11, the class-id is generally the right app-id, but there is sometimes
				// an inversion with the instance-id. If there is only one 'class_ids' (wayland),
				// it's considered valid anyway
				if (!xfce_str_is_empty(class_ids[1])
					&& mInvalidClassIds.find(Help::String::toLowercase(class_ids[0])) != mInvalidClassIds.end())
					return class_ids[1];
				else
					return class_ids[0];
			}

			// proc/{pid}/cmdline method
			XfwApplicationInstance* instance = xfw_application_get_instance(xfw_window_get_application(xfwWindow), xfwWindow);
			char buffer[512];
			std::string path = "/proc/" + std::to_string(xfw_application_instance_get_pid(instance)) + "/cmdline";
			int fd = open(path.c_str(), O_RDONLY);

			if (fd >= 0)
			{
				int nbr = read(fd, buffer, 512);
				::close(fd);

				std::string exe_out = Help::String::pathBasename(buffer);
				if (exe_out != "python") // ADDIT graphical interpreters here
					return exe_out;

				char* it = buffer;
				while (*it++)
					;

				if (it < buffer + nbr)
				{
					return Help::String::pathBasename(it);
				}
			}

			// fallback : return window's name
			return xfw_window_get_name(xfwWindow);
		}
	} // namespace

	// public:

	void init()
	{
		xfw_set_client_type(XFW_CLIENT_TYPE_PAGER);
		mXfwScreen = xfw_screen_get_default();

		g_signal_connect(G_OBJECT(mXfwScreen), "window-opened",
			G_CALLBACK(+[](XfwScreen* screen, XfwWindow* xfwWindow) {
				std::shared_ptr<GroupWindow> newWindow = std::make_shared<GroupWindow>(xfwWindow);
				mGroupWindows.pushSecond(xfwWindow, newWindow);
				newWindow->mGroup->updateStyle();

				if (Settings::showPreviews && newWindow->mGroup->mGroupMenu.mVisible)
					newWindow->mGroupMenuItem->mPreviewTimeout.start();
			}),
			nullptr);

		g_signal_connect(G_OBJECT(mXfwScreen), "window-closed",
			G_CALLBACK(+[](XfwScreen* screen, XfwWindow* xfwWindow) {
				mGroupWindows.pop(xfwWindow);
				if (xfwWindow == mPreviousActiveWindow)
					mPreviousActiveWindow = nullptr;
			}),
			nullptr);

		g_signal_connect(G_OBJECT(mXfwScreen), "active-window-changed",
			G_CALLBACK(+[](XfwScreen* screen, XfwWindow* previousActiveWindow) {
				XfwWindow* activeXfwWindow = getActiveWindow();
				if (activeXfwWindow != nullptr)
				{
					std::shared_ptr<GroupWindow> activeWindow = mGroupWindows.get(activeXfwWindow);
					Help::Gtk::cssClassAdd(GTK_WIDGET(activeWindow->mGroupMenuItem->mItem), "active_menu_item");
					gtk_widget_queue_draw(activeWindow->mGroup->mButton);
				}
				if (previousActiveWindow != nullptr)
				{
					std::shared_ptr<GroupWindow> prevWindow = mGroupWindows.get(previousActiveWindow);
					if (prevWindow)
					{
						Help::Gtk::cssClassRemove(GTK_WIDGET(prevWindow->mGroupMenuItem->mItem), "active_menu_item");
						gtk_widget_queue_draw(prevWindow->mGroup->mButton);
						mPreviousActiveWindow = previousActiveWindow;
					}
				}
				setActiveWindow(previousActiveWindow);
			}),
			nullptr);

		// window<->workspace association only works on X11, where there is only one workspace group,
		// but it can be destroyed on wayland, so let's manage this in a minimalist way
		XfwWorkspaceManager* wpManager = xfw_screen_get_workspace_manager(mXfwScreen);
		auto workspaceGroupCreated = +[](XfwWorkspaceManager* manager, XfwWorkspaceGroup* group, gpointer data) {
			if (mXfwWorkspaceGroup == nullptr)
			{
				mXfwWorkspaceGroup = XFW_WORKSPACE_GROUP(xfw_workspace_manager_list_workspace_groups(manager)->data);
				g_signal_connect(G_OBJECT(mXfwWorkspaceGroup), "active-workspace-changed",
					G_CALLBACK(+[](XfwScreen* screen, XfwWindow* xfwWindow) {
						setVisibleGroups();
					}),
					nullptr);
			}
		};
		auto workspaceGroupDestroyed = +[](XfwWorkspaceManager* manager, XfwWorkspaceGroup* group, gpointer data) {
			if (group == mXfwWorkspaceGroup)
			{
				GList* groups = xfw_workspace_manager_list_workspace_groups(manager);
				if (groups == nullptr)
				{
					mXfwWorkspaceGroup = nullptr;
				}
				else
				{
					mXfwWorkspaceGroup = XFW_WORKSPACE_GROUP(groups->data);
					g_signal_connect(G_OBJECT(mXfwWorkspaceGroup), "active-workspace-changed",
						G_CALLBACK(+[](XfwScreen* screen, XfwWindow* xfwWindow) {
							setVisibleGroups();
						}),
						nullptr);
				}
			}
		};
		g_signal_connect(wpManager, "workspace-group-created", G_CALLBACK(workspaceGroupCreated), nullptr);
		g_signal_connect(wpManager, "workspace-group-destroyed", G_CALLBACK(workspaceGroupDestroyed), nullptr);
		workspaceGroupDestroyed(wpManager, nullptr, nullptr);
	}

	void finalize()
	{
		mGroupWindows.clear();
		g_signal_handlers_disconnect_by_data(xfw_screen_get_workspace_manager(mXfwScreen), nullptr);
		g_signal_handlers_disconnect_by_data(mXfwWorkspaceGroup, nullptr);
		g_signal_handlers_disconnect_by_data(mXfwScreen, nullptr);
		g_object_unref(mXfwScreen);
	}

	XfwWindow* getActiveWindow()
	{
		return xfw_screen_get_active_window(mXfwScreen);
	}

	std::string getGroupName(GroupWindow* groupWindow)
	{
		return Help::String::toLowercase(getGroupNameSys(groupWindow->mXfwWindow));
	}

	cairo_surface_t* getWindowPreview(XfwWindow* window, int width, int height, int scaleFactor, bool proportional)
	{
#ifdef ENABLE_X11
		// This needs work to survive porting to GTK4 and/or Wayland.
		// GDK doesn't expose an API to get a foreign window on X11 anymore (so X11 code).
		// Wayland may never have a public protocol for obtaining a preview of a foreign window,
		// which goes against its security principles (so private protocol).
		if (!GDK_IS_X11_DISPLAY(Plugin::mDisplay))
			return nullptr;

		GdkWindow* gdkWindow = gdk_x11_window_foreign_new_for_display(Plugin::mDisplay,
			xfw_window_x11_get_xid(window));

		if (gdkWindow == nullptr)
			return nullptr;

		if (width <= 0)
			width = 1;
		if (height <= 0)
			height = 1;

		// we're probably not doing anything wrong at our level, but things can go wrong in cairo when
		// multiple windows are closed and thumbnails are shown (#71), so let's catch X11 errors for it
		GdkDisplay* display = gdk_display_get_default();
		gdk_x11_display_error_trap_push(display);
		GdkPixbuf* pixbuf = gdk_pixbuf_get_from_window(gdkWindow, 0, 0, gdk_window_get_width(gdkWindow), gdk_window_get_height(gdkWindow));
		gdk_x11_display_error_trap_pop_ignored(display);
		g_object_unref(gdkWindow);

		if (pixbuf == nullptr)
			return nullptr;

		gint previewWidth = width * scaleFactor;
		gint previewHeight = height * scaleFactor;

		gint pixbufWidth = gdk_pixbuf_get_width(pixbuf);
		gint pixbufHeight = gdk_pixbuf_get_height(pixbuf);

		if (proportional)
		{
			// Fit the thumbnail inside the preview box while preserving the
			// window's aspect ratio (no letterboxing, no upscaling distortion).
			gdouble scale = MIN((gdouble)previewWidth / (gdouble)pixbufWidth,
			                    (gdouble)previewHeight / (gdouble)pixbufHeight);
			pixbufWidth = MAX(1, (gint)(pixbufWidth * scale));
			pixbufHeight = MAX(1, (gint)(pixbufHeight * scale));

			GdkPixbuf* thumbnail = gdk_pixbuf_scale_simple(pixbuf, pixbufWidth, pixbufHeight, GDK_INTERP_BILINEAR);

			if (thumbnail == nullptr)
			{
				g_object_unref(pixbuf);
				return nullptr;
			}

			cairo_surface_t* surface = gdk_cairo_surface_create_from_pixbuf(thumbnail, scaleFactor, nullptr);

			g_object_unref(thumbnail);
			g_object_unref(pixbuf);

			return surface;
		}

		/* calculate the new dimensions (fill the box, letterbox the rest) */
		gdouble wRatio = (gdouble)pixbufWidth / (gdouble)previewWidth;
		gdouble hRatio = (gdouble)pixbufHeight / (gdouble)previewHeight;

		if (hRatio > wRatio)
		{
			pixbufWidth = MAX(1, pixbufWidth / hRatio);
			pixbufHeight = MIN(pixbufHeight, previewHeight);
		}
		else
		{
			pixbufWidth = MIN(pixbufWidth, previewWidth);
			pixbufHeight = MAX(1, pixbufHeight / wRatio);
		}

		GdkPixbuf* thumbnail = gdk_pixbuf_scale_simple(pixbuf, pixbufWidth, pixbufHeight, GDK_INTERP_BILINEAR);

		GdkPixbuf* sized = gdk_pixbuf_new(GDK_COLORSPACE_RGB, true, 8, previewWidth, previewHeight);
		gint thumbWidth = gdk_pixbuf_get_width(thumbnail);
		gint thumbHeight = gdk_pixbuf_get_height(thumbnail);
		gint xOffset = (previewWidth - thumbWidth) / 2;
		gint yOffset = (previewHeight - thumbHeight) / 2;
		gdk_pixbuf_composite(thumbnail, sized, xOffset, yOffset, thumbWidth, thumbHeight, xOffset, yOffset, 1, 1, GDK_INTERP_BILINEAR, 255);

		cairo_surface_t* surface = gdk_cairo_surface_create_from_pixbuf(sized, scaleFactor, nullptr);

		g_object_unref(sized);
		g_object_unref(thumbnail);
		g_object_unref(pixbuf);

		return surface;
#else
		(void)window;
		(void)width;
		(void)height;
		(void)scaleFactor;
		(void)proportional;
		return nullptr;
#endif
	}

	void activate(GroupWindow* groupWindow, guint32 timestamp)
	{
		XfwWorkspace* workspace = xfw_window_get_workspace(groupWindow->mXfwWindow);
		if (workspace != nullptr)
			xfw_workspace_activate(workspace, nullptr);

		xfw_window_activate(groupWindow->mXfwWindow, nullptr,
			timestamp != GDK_CURRENT_TIME ? timestamp : g_get_monotonic_time() / 1000,
			nullptr);
	}

	void close(GroupWindow* groupWindow, guint32 timestamp)
	{
		xfw_window_close(groupWindow->mXfwWindow,
			timestamp != GDK_CURRENT_TIME ? timestamp : g_get_monotonic_time() / 1000,
			nullptr);
	}

	void setActiveWindow(XfwWindow* previousActiveWindow)
	{
		XfwWindow* activeWindow = getActiveWindow();
		if (previousActiveWindow != nullptr)
		{
			std::shared_ptr<GroupWindow> groupWindow = mGroupWindows.get(previousActiveWindow);
			if (groupWindow)
				groupWindow->onUnactivate();
		}
		if (activeWindow != nullptr)
			mGroupWindows.get(activeWindow)->onActivate();
	}

	void setVisibleGroups()
	{
		for (GList* window_l = xfw_screen_get_windows(mXfwScreen);
			 window_l != nullptr;
			 window_l = window_l->next)
		{
			XfwWindow* xfwWindow = XFW_WINDOW(window_l->data);
			std::shared_ptr<GroupWindow> groupWindow = mGroupWindows.get(xfwWindow);

			groupWindow->leaveGroup();
			groupWindow->updateState();
		}
	}

	void switchToLastWindow()
	{
		if (mPreviousActiveWindow != nullptr)
		{
			std::shared_ptr<GroupWindow> prevWindow = mGroupWindows.get(mPreviousActiveWindow);
			if (prevWindow)
				prevWindow->activate(GDK_CURRENT_TIME);
		}
	}
} // namespace Xfw
