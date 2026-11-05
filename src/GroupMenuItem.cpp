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

#include "GroupMenuItem.hpp"

static GtkTargetEntry entries[1] = {{(gchar*)"any", 0, 0}};

GroupMenuItem::GroupMenuItem(GroupWindow* groupWindow)
{
	mGroupWindow = groupWindow;
	mDragging = false;
	mDragStartX = 0;
	mDragStartY = 0;

	// This needs work to survive porting to GTK4.
	// GtkEventBox is removed, all events are supported by all widgets.

	mItem = GTK_EVENT_BOX(gtk_event_box_new());
	gtk_widget_add_events(GTK_WIDGET(mItem), GDK_POINTER_MOTION_MASK);
	gtk_drag_dest_set(GTK_WIDGET(mItem), GTK_DEST_DEFAULT_DROP, entries, 1, GDK_ACTION_MOVE);
	Help::Gtk::cssClassAdd(GTK_WIDGET(mItem), "menu_item");
	gtk_widget_show(GTK_WIDGET(mItem));
	g_object_ref(mItem);

	mGrid = GTK_GRID(gtk_grid_new());
	gtk_grid_set_column_spacing(mGrid, 6);
	gtk_widget_show(GTK_WIDGET(mGrid));
	gtk_container_add(GTK_CONTAINER(mItem), GTK_WIDGET(mGrid));

	mIcon = GTK_IMAGE(gtk_image_new());
	Help::Gtk::cssClassAdd(GTK_WIDGET(mIcon), "icon");
	gtk_widget_set_halign(GTK_WIDGET(mIcon), GTK_ALIGN_START);
	gtk_widget_set_hexpand(GTK_WIDGET(mIcon), false);
	gtk_widget_show(GTK_WIDGET(mIcon));
	gtk_grid_attach(mGrid, GTK_WIDGET(mIcon), 0, 0, 1, 1);

	mLabel = GTK_LABEL(gtk_label_new(""));
	Help::Gtk::cssClassAdd(GTK_WIDGET(mLabel), "title");
	gtk_label_set_xalign(mLabel, 0);
	gtk_label_set_ellipsize(mLabel, PANGO_ELLIPSIZE_END);
	gtk_label_set_width_chars(mLabel, 26);
	gtk_widget_set_hexpand(GTK_WIDGET(mLabel), true);
	gtk_widget_show(GTK_WIDGET(mLabel));
	gtk_grid_attach(mGrid, GTK_WIDGET(mLabel), 1, 0, 1, 1);

	mCloseButton = GTK_BUTTON(gtk_button_new_from_icon_name("window-close", GTK_ICON_SIZE_MENU));
	gtk_button_set_relief(mCloseButton, GTK_RELIEF_NONE);
	gtk_widget_set_halign(GTK_WIDGET(mCloseButton), GTK_ALIGN_END);
	gtk_widget_set_hexpand(GTK_WIDGET(mCloseButton), false);
	gtk_widget_show(GTK_WIDGET(mCloseButton));
	gtk_grid_attach(mGrid, GTK_WIDGET(mCloseButton), 2, 0, 1, 1);

	mPreview = GTK_IMAGE(gtk_image_new());
	Help::Gtk::cssClassAdd(GTK_WIDGET(mPreview), "preview");
	gtk_grid_attach(mGrid, GTK_WIDGET(mPreview), 0, 1, 3, 1);
	gtk_widget_set_visible(GTK_WIDGET(mPreview), Settings::showPreviews);

	if (Xfw::getActiveWindow() == mGroupWindow->mXfwWindow)
		Help::Gtk::cssClassAdd(GTK_WIDGET(mItem), "active_menu_item");

	int sleepMS = 1000;
	if (Settings::previewSleep)
		sleepMS = Settings::previewSleep;

	mPreviewTimeout.setup(sleepMS, [this]() {
		updatePreview();
		return true;
	});

	//--------------------------------------------------

	g_signal_connect(G_OBJECT(mItem), "button-press-event",
		G_CALLBACK(+[](GtkWidget* widget, GdkEventButton* event, GroupMenuItem* me) {
			if (event->button == GDK_BUTTON_PRIMARY)
			{
				// Delay activation to the release so a press-and-drag can reorder
				// the previews instead of switching to the window.
				me->mDragging = false;
				me->mDragStartX = event->x;
				me->mDragStartY = event->y;
			}
			else if (event->button == GDK_BUTTON_MIDDLE && Settings::middleButtonBehavior == BEHAVIOR_CLOSE_ALL)
				Xfw::close(me->mGroupWindow, event->time);
			else if (event->button == GDK_BUTTON_SECONDARY)
			{
				GtkWidget* menu = GTK_WIDGET(g_object_ref_sink(xfw_window_action_menu_new(me->mGroupWindow->mXfwWindow)));
				xfce_panel_plugin_register_menu(Plugin::mXfPlugin, GTK_MENU(menu));
				g_signal_connect(G_OBJECT(menu), "deactivate",
					G_CALLBACK(+[](GtkMenuShell* _menu, GroupMenuItem* _me) {
						g_object_unref(_menu);
						_me->mGroupWindow->mGroup->mWindowMenuShown = false;
						_me->mGroupWindow->mGroup->setMouseLeaveTimeout();
					}),
					me);
				gtk_menu_popup_at_pointer(GTK_MENU(menu), (GdkEvent*)event);
				me->mGroupWindow->mGroup->mWindowMenuShown = true;
			}
			return true;
		}),
		this);

	g_signal_connect(G_OBJECT(mItem), "motion-notify-event",
		G_CALLBACK(+[](GtkWidget* widget, GdkEventMotion* event, GroupMenuItem* me) {
			if (me->mDragging)
			{
				gint px;
				gint py;
				gdk_device_get_position(Plugin::mPointer, nullptr, &px, &py);

				GroupWindow* window = me->mGroupWindow;
				window->mGroup->mGroupMenu.setDropTarget(
					window->mGroup->mGroupMenu.itemIndexAtPointer(px, py));

				return true;
			}

			if (!(event->state & GDK_BUTTON1_MASK))
				return false;

			if (gtk_drag_check_threshold(widget, me->mDragStartX, me->mDragStartY, event->x, event->y))
			{
				me->mDragging = true;
				Help::Gtk::cssClassAdd(widget, "dragging_menu_item");
				me->mGroupWindow->mGroup->mGroupMenu.clearDropTarget();
				me->mGroupWindow->mGroup->mGroupMenu.setDragCursor(true);
			}

			return true;
		}),
		this);

	g_signal_connect(G_OBJECT(mItem), "button-release-event",
		G_CALLBACK(+[](GtkWidget* widget, GdkEventButton* event, GroupMenuItem* me) {
			if (event->button != GDK_BUTTON_PRIMARY)
				return false;

			Help::Gtk::cssClassRemove(widget, "dragging_menu_item");

			if (me->mDragging)
			{
				me->mDragging = false;

				GroupWindow* window = me->mGroupWindow;
				GroupMenu* menu = &window->mGroup->mGroupMenu;
				int index = menu->mDropIndex;
				menu->clearDropTarget();
				menu->setDragCursor(false);

				if (index >= 0 && window->mGroup->mWindows.getIndex(window) != (uint)index)
					window->mGroup->moveWindow(window, index);

				return true;
			}

			me->mGroupWindow->activate(event->time);
			return true;
		}),
		this);

	g_signal_connect(G_OBJECT(mItem), "drag-motion",
		G_CALLBACK(+[](GtkWidget* widget, GdkDragContext* context, gint x, gint y, guint time, GroupMenuItem* me) {
			me->mGroupWindow->activate(time);
			gdk_drag_status(context, GDK_ACTION_MOVE, time);
			return true;
		}),
		this);

	g_signal_connect(G_OBJECT(mItem), "drag-leave",
		G_CALLBACK(+[](GtkWidget* widget, GdkDragContext* context, guint time, GroupMenuItem* me) {
			g_idle_add(+[](gpointer data) -> gboolean {
				GroupMenuItem* me_check = static_cast<GroupMenuItem*>(data);
				GroupMenu* menu = &me_check->mGroupWindow->mGroup->mGroupMenu;

				if (menu->mVisible && menu->getPointerDistance() > 0)
					menu->hide();

				return G_SOURCE_REMOVE; }, me);
		}),
		this);

	g_signal_connect(G_OBJECT(mItem), "enter-notify-event",
		G_CALLBACK(+[](GtkWidget* widget, GdkEventCrossing* event, GroupMenuItem* me) {
			if (!me->mDragging && (event->state & GDK_BUTTON1_MASK))
				me->mGroupWindow->activate(event->time);
			Help::Gtk::cssClassAdd(widget, "hover_menu_item");
			gtk_widget_queue_draw(widget);
			return true;
		}),
		this);

	g_signal_connect(G_OBJECT(mItem), "leave-notify-event",
		G_CALLBACK(+[](GtkWidget* widget, GdkEvent* event, GroupMenuItem* me) {
			Help::Gtk::cssClassRemove(widget, "hover_menu_item");
			gtk_widget_queue_draw(widget);
			gtk_widget_queue_draw(me->mGroupWindow->mGroup->mButton);
			return true;
		}),
		this);

	g_signal_connect(G_OBJECT(mCloseButton), "clicked",
		G_CALLBACK(+[](GtkButton* button, GroupMenuItem* me) {
			Xfw::close(me->mGroupWindow, 0);
		}),
		this);
}

GroupMenuItem::~GroupMenuItem()
{
	mPreviewTimeout.stop();
	g_object_unref(mItem);
}

void GroupMenuItem::updateLabel()
{
	const char* winName = xfw_window_get_name(mGroupWindow->mXfwWindow);

	if (Xfw::getActiveWindow() == mGroupWindow->mXfwWindow)
	{
		gchar* escaped = g_markup_escape_text(winName, -1);
		gchar* markup = g_strdup_printf("<b>%s</b>", escaped);
		gtk_label_set_markup(mLabel, markup);
		g_free(markup);
		g_free(escaped);
	}
	else if (mGroupWindow->getState(XFW_WINDOW_STATE_MINIMIZED))
	{
		gchar* escaped = g_markup_escape_text(winName, -1);
		gchar* markup = g_strdup_printf("<i>%s</i>", escaped);
		gtk_label_set_markup(mLabel, markup);
		g_free(markup);
		g_free(escaped);
	}
	else
		gtk_label_set_text(mLabel, winName);
}

void GroupMenuItem::updateIcon()
{
	gint scale_factor = gtk_widget_get_scale_factor(GTK_WIDGET(mIcon));
	GdkPixbuf* iconPixbuf = xfw_window_get_icon(mGroupWindow->mXfwWindow, 16, scale_factor);

	if (iconPixbuf != nullptr)
	{
		cairo_surface_t* surface = gdk_cairo_surface_create_from_pixbuf(iconPixbuf, scale_factor, nullptr);
		gtk_image_set_from_surface(mIcon, surface);
		cairo_surface_destroy(surface);
	}
}

// Draw an empty-window placeholder (outline + title bar) used when no
// thumbnail can be captured: minimized windows, Wayland, or X11 errors.
static cairo_surface_t* createPlaceholderPreview(GtkWidget* widget, int width, int height, int scaleFactor)
{
	if (width <= 0 || height <= 0)
		return nullptr;

	if (scaleFactor < 1)
		scaleFactor = 1;

	GtkStyleContext* context = gtk_widget_get_style_context(widget);

	GdkRGBA bg, fg, border;
	if (!gtk_style_context_lookup_color(context, "theme_bg_color", &bg))
		bg = {0.9, 0.9, 0.9, 1.0};
	if (!gtk_style_context_lookup_color(context, "theme_fg_color", &fg))
		fg = {0.1, 0.1, 0.1, 1.0};
	if (!gtk_style_context_lookup_color(context, "borders", &border))
		border = {fg.red, fg.green, fg.blue, 0.35};

	bg.alpha = 1.0;
	border.alpha = border.alpha > 0.0 ? border.alpha : 0.35;

	// Title bar: a subtly contrasted variant of the window background.
	GdkRGBA titleBar = {
		bg.red * 0.92 + fg.red * 0.08,
		bg.green * 0.92 + fg.green * 0.08,
		bg.blue * 0.92 + fg.blue * 0.08,
		1.0};

	cairo_surface_t* surface = cairo_image_surface_create(
		CAIRO_FORMAT_ARGB32, width * scaleFactor, height * scaleFactor);

	if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS)
	{
		cairo_surface_destroy(surface);
		return nullptr;
	}

	// Draw in logical (unscaled) coordinates.
	cairo_surface_set_device_scale(surface, scaleFactor, scaleFactor);

	cairo_t* cr = cairo_create(surface);
	double w = width;
	double h = height;
	double titleHeight = MAX(10.0, h / 10.0);
	double line = 1.0;

	// Window body.
	cairo_set_source_rgba(cr, bg.red, bg.green, bg.blue, bg.alpha);
	cairo_rectangle(cr, 0, 0, w, h);
	cairo_fill(cr);

	// Title bar.
	cairo_set_source_rgba(cr, titleBar.red, titleBar.green, titleBar.blue, titleBar.alpha);
	cairo_rectangle(cr, 0, 0, w, titleHeight);
	cairo_fill(cr);

	// Separator below the title bar.
	cairo_set_source_rgba(cr, border.red, border.green, border.blue, border.alpha);
	cairo_rectangle(cr, 0, titleHeight - line / 2.0, w, line);
	cairo_fill(cr);

	// Window outline, drawn just inside the surface so it is not clipped.
	cairo_set_line_width(cr, line);
	cairo_set_source_rgba(cr, border.red, border.green, border.blue, border.alpha);
	cairo_rectangle(cr, line / 2.0, line / 2.0, w - line, h - line);
	cairo_stroke(cr);

	cairo_destroy(cr);
	return surface;
}

void GroupMenuItem::updatePreview()
{
	gint scaleFactor = gtk_widget_get_scale_factor(GTK_WIDGET(mPreview));
	cairo_surface_t* surface = nullptr;

	// Minimized windows may not expose a usable thumbnail.
	if (!mGroupWindow->getState(XFW_WINDOW_STATE_MINIMIZED))
		surface = Xfw::getWindowPreview(mGroupWindow->mXfwWindow,
			Settings::previewWidth, Settings::previewHeight, scaleFactor);

	// Fall back to a placeholder when no thumbnail is available.
	if (surface == nullptr)
		surface = createPlaceholderPreview(GTK_WIDGET(mPreview),
			Settings::previewWidth, Settings::previewHeight, scaleFactor);

	if (surface != nullptr)
	{
		gtk_image_set_from_surface(mPreview, surface);
		cairo_surface_destroy(surface);
	}
}
