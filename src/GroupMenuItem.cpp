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
	mOverviewAlternative = false;
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

	// Root container that holds either the default grid layout or the
	// alternative overlay layout. Only one is visible at a time.
	mRootBox = GTK_BOX(gtk_box_new(GTK_ORIENTATION_VERTICAL, 0));
	gtk_widget_show(GTK_WIDGET(mRootBox));
	gtk_container_add(GTK_CONTAINER(mItem), GTK_WIDGET(mRootBox));

	//--------------------------------------------------
	// Default layout: GtkGrid with icon, label, close in row 0,
	// preview spanning row 1.
	//--------------------------------------------------

	mGrid = GTK_GRID(gtk_grid_new());
	gtk_grid_set_column_spacing(mGrid, 6);
	gtk_widget_show(GTK_WIDGET(mGrid));
	gtk_box_pack_start(GTK_BOX(mRootBox), GTK_WIDGET(mGrid), TRUE, TRUE, 0);

	mIcon = GTK_IMAGE(gtk_image_new());
	// Keep our own reference: the overview reparents this widget between
	// containers, and the container would otherwise destroy it on removal.
	g_object_ref_sink(GTK_WIDGET(mIcon));
	Help::Gtk::cssClassAdd(GTK_WIDGET(mIcon), "icon");
	gtk_widget_set_halign(GTK_WIDGET(mIcon), GTK_ALIGN_START);
	gtk_widget_set_hexpand(GTK_WIDGET(mIcon), false);
	gtk_widget_show(GTK_WIDGET(mIcon));
	gtk_grid_attach(mGrid, GTK_WIDGET(mIcon), 0, 0, 1, 1);

	mLabel = GTK_LABEL(gtk_label_new(""));
	g_object_ref_sink(GTK_WIDGET(mLabel));
	Help::Gtk::cssClassAdd(GTK_WIDGET(mLabel), "title");
	gtk_label_set_xalign(mLabel, 0);
	gtk_label_set_ellipsize(mLabel, PANGO_ELLIPSIZE_END);
	gtk_label_set_width_chars(mLabel, 26);
	gtk_widget_set_hexpand(GTK_WIDGET(mLabel), true);
	gtk_widget_show(GTK_WIDGET(mLabel));
	gtk_grid_attach(mGrid, GTK_WIDGET(mLabel), 1, 0, 1, 1);

	mCloseButton = GTK_BUTTON(gtk_button_new_from_icon_name("window-close", GTK_ICON_SIZE_MENU));
	g_object_ref_sink(GTK_WIDGET(mCloseButton));
	gtk_button_set_relief(mCloseButton, GTK_RELIEF_NONE);
	Help::Gtk::cssClassAdd(GTK_WIDGET(mCloseButton), "overview_close");
	gtk_widget_set_halign(GTK_WIDGET(mCloseButton), GTK_ALIGN_END);
	gtk_widget_set_hexpand(GTK_WIDGET(mCloseButton), false);
	gtk_widget_show(GTK_WIDGET(mCloseButton));
	gtk_grid_attach(mGrid, GTK_WIDGET(mCloseButton), 2, 0, 1, 1);

	mPreview = GTK_IMAGE(gtk_image_new());
	g_object_ref_sink(GTK_WIDGET(mPreview));
	Help::Gtk::cssClassAdd(GTK_WIDGET(mPreview), "preview");
	gtk_grid_attach(mGrid, GTK_WIDGET(mPreview), 0, 1, 3, 1);
	gtk_widget_set_visible(GTK_WIDGET(mPreview), Settings::showPreviews);

	//--------------------------------------------------
	// Alternative layout: GtkOverlay inside a vertical GtkBox.
	// Overlay contains preview (main child) with icon (bottom center)
	// and close button (top right) overlaid on top. Label below.
	//--------------------------------------------------

	mAltBox = GTK_BOX(gtk_box_new(GTK_ORIENTATION_VERTICAL, 0));
	gtk_widget_hide(GTK_WIDGET(mAltBox));
	gtk_box_pack_start(GTK_BOX(mRootBox), GTK_WIDGET(mAltBox), TRUE, TRUE, 0);

	mOverlay = GTK_OVERLAY(gtk_overlay_new());
	gtk_widget_show(GTK_WIDGET(mOverlay));
	gtk_box_pack_start(GTK_BOX(mAltBox), GTK_WIDGET(mOverlay), TRUE, TRUE, 0);

	// Label is added to mAltBox in setOverviewAlternative(true) when switching
	// to the alternative layout; it stays in mGrid for the default layout.

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
			if (me->mOverviewAlternative)
				me->setOverviewHover(true);
			gtk_widget_queue_draw(widget);
			return true;
		}),
		this);

	g_signal_connect(G_OBJECT(mItem), "leave-notify-event",
		G_CALLBACK(+[](GtkWidget* widget, GdkEventCrossing* event, GroupMenuItem* me) {
			// Entering a child (such as the close button) also produces a leave
			// event for the event box; keep the overview controls visible then.
			if (event->detail != GDK_NOTIFY_INFERIOR)
			{
				Help::Gtk::cssClassRemove(widget, "hover_menu_item");
				if (me->mOverviewAlternative)
					me->setOverviewHover(false);
				gtk_widget_queue_draw(widget);
				gtk_widget_queue_draw(me->mGroupWindow->mGroup->mButton);
			}
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

	// Drop the references taken in the constructor. If the widgets are still
	// parented, the container keeps them alive until the item is destroyed.
	g_object_unref(mPreview);
	g_object_unref(mCloseButton);
	g_object_unref(mLabel);
	g_object_unref(mIcon);

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
	gint iconSize = 16;

	if (mOverviewAlternative)
		iconSize = CLAMP(MIN(Settings::previewWidth, Settings::previewHeight) / 6, 24, 48);

	GdkPixbuf* iconPixbuf = xfw_window_get_icon(mGroupWindow->mXfwWindow, iconSize, scale_factor);

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
	cairo_surface_t* surface = Xfw::getWindowPreview(mGroupWindow->mXfwWindow,
		Settings::previewWidth, Settings::previewHeight, scaleFactor, mOverviewAlternative);

	if (surface == nullptr && mOverviewAlternative)
	{
		// Fall back to a letterboxed preview so the window is still shown if the
		// proportional rendering cannot be produced.
		surface = Xfw::getWindowPreview(mGroupWindow->mXfwWindow,
			Settings::previewWidth, Settings::previewHeight, scaleFactor, false);
	}

	// Fall back to a placeholder when no thumbnail is available.
	if (surface == nullptr)
		surface = createPlaceholderPreview(GTK_WIDGET(mPreview),
			Settings::previewWidth, Settings::previewHeight, scaleFactor);

	if (surface != nullptr)
	{
		gtk_image_set_from_surface(mPreview, surface);

		if (mOverviewAlternative)
		{
			gint minimumWidth;
			gint naturalWidth;
			gtk_widget_get_preferred_width(GTK_WIDGET(mPreview), &minimumWidth, &naturalWidth);
			int maxChars = MAX(8, (naturalWidth > 0 ? naturalWidth : Settings::previewWidth) / 8);
			gtk_label_set_max_width_chars(mLabel, maxChars);
		}

		cairo_surface_destroy(surface);
	}
}

void GroupMenuItem::setOverviewHover(bool hover)
{
	if (!mOverviewAlternative)
		return;

	gtk_widget_set_opacity(GTK_WIDGET(mLabel), hover ? 1.0 : 0.0);
	gtk_widget_set_opacity(GTK_WIDGET(mCloseButton), hover ? 1.0 : 0.0);
	gtk_widget_set_sensitive(GTK_WIDGET(mCloseButton), hover);
}

void GroupMenuItem::setOverviewAlternative(bool enabled)
{
	mOverviewAlternative = enabled;

	// Reparenting is only needed when the layout actually changes. Calling it
	// twice would try to remove the widgets from the wrong container.
	bool inOverlay = gtk_widget_get_parent(GTK_WIDGET(mPreview)) == GTK_WIDGET(mOverlay);

	if (enabled)
	{
		if (!inOverlay)
		{
			// Switch from default grid layout to alternative overlay layout.
			gtk_widget_hide(GTK_WIDGET(mGrid));
			gtk_widget_show(GTK_WIDGET(mAltBox));

			// Move preview, icon, close button and label out of the grid.
			gtk_container_remove(GTK_CONTAINER(mGrid), GTK_WIDGET(mPreview));
			gtk_container_remove(GTK_CONTAINER(mGrid), GTK_WIDGET(mIcon));
			gtk_container_remove(GTK_CONTAINER(mGrid), GTK_WIDGET(mCloseButton));
			gtk_container_remove(GTK_CONTAINER(mGrid), GTK_WIDGET(mLabel));

			gtk_container_add(GTK_CONTAINER(mOverlay), GTK_WIDGET(mPreview));
			gtk_overlay_add_overlay(mOverlay, GTK_WIDGET(mIcon));
			gtk_overlay_add_overlay(mOverlay, GTK_WIDGET(mCloseButton));
			gtk_box_pack_start(GTK_BOX(mAltBox), GTK_WIDGET(mLabel), FALSE, FALSE, 0);
		}

		Help::Gtk::cssClassAdd(GTK_WIDGET(mItem), "overview_alt");

		// Position overlay children: icon bottom center, close top right.
		gtk_widget_set_halign(GTK_WIDGET(mIcon), GTK_ALIGN_CENTER);
		gtk_widget_set_valign(GTK_WIDGET(mIcon), GTK_ALIGN_END);
		gtk_widget_set_hexpand(GTK_WIDGET(mIcon), false);
		gtk_widget_set_margin_top(GTK_WIDGET(mIcon), 0);
		gtk_widget_set_margin_bottom(GTK_WIDGET(mIcon), 4);
		gtk_widget_set_margin_start(GTK_WIDGET(mIcon), 0);
		gtk_widget_set_margin_end(GTK_WIDGET(mIcon), 0);

		gtk_widget_set_halign(GTK_WIDGET(mCloseButton), GTK_ALIGN_END);
		gtk_widget_set_valign(GTK_WIDGET(mCloseButton), GTK_ALIGN_START);
		gtk_widget_set_margin_top(GTK_WIDGET(mCloseButton), 6);
		gtk_widget_set_margin_end(GTK_WIDGET(mCloseButton), 6);
		gtk_widget_set_margin_bottom(GTK_WIDGET(mCloseButton), 0);
		gtk_widget_set_margin_start(GTK_WIDGET(mCloseButton), 0);

		gtk_widget_set_halign(GTK_WIDGET(mPreview), GTK_ALIGN_FILL);
		gtk_widget_set_valign(GTK_WIDGET(mPreview), GTK_ALIGN_FILL);
		gtk_widget_set_margin_top(GTK_WIDGET(mPreview), 0);
		gtk_widget_set_margin_bottom(GTK_WIDGET(mPreview), 0);
		gtk_widget_set_margin_start(GTK_WIDGET(mPreview), 0);
		gtk_widget_set_margin_end(GTK_WIDGET(mPreview), 0);

		// Label below the overlay: centered, ellipsized to preview width.
		gtk_widget_set_halign(GTK_WIDGET(mLabel), GTK_ALIGN_CENTER);
		gtk_widget_set_valign(GTK_WIDGET(mLabel), GTK_ALIGN_CENTER);
		gtk_widget_set_hexpand(GTK_WIDGET(mLabel), false);
		gtk_label_set_xalign(mLabel, 0.5);
		gtk_label_set_ellipsize(mLabel, PANGO_ELLIPSIZE_END);
		gtk_label_set_width_chars(mLabel, -1);
		gtk_label_set_max_width_chars(mLabel, MAX(8, Settings::previewWidth / 8));
		gtk_widget_set_margin_top(GTK_WIDGET(mLabel), 8);
		gtk_widget_set_margin_bottom(GTK_WIDGET(mLabel), 4);

		gtk_widget_show(GTK_WIDGET(mPreview));
		gtk_widget_show(GTK_WIDGET(mIcon));
		gtk_widget_show(GTK_WIDGET(mCloseButton));
		gtk_widget_show(GTK_WIDGET(mLabel));

		setOverviewHover(false);
		updateIcon();
	}
	else
	{
		if (inOverlay)
		{
			// Switch back to default grid layout.
			gtk_widget_hide(GTK_WIDGET(mAltBox));
			gtk_widget_show(GTK_WIDGET(mGrid));

			gtk_container_remove(GTK_CONTAINER(mAltBox), GTK_WIDGET(mLabel));

			// Move widgets from overlay back into the grid.
			gtk_container_remove(GTK_CONTAINER(mOverlay), GTK_WIDGET(mPreview));
			gtk_container_remove(GTK_CONTAINER(mOverlay), GTK_WIDGET(mIcon));
			gtk_container_remove(GTK_CONTAINER(mOverlay), GTK_WIDGET(mCloseButton));

			gtk_grid_attach(mGrid, GTK_WIDGET(mIcon), 0, 0, 1, 1);
			gtk_grid_attach(mGrid, GTK_WIDGET(mLabel), 1, 0, 1, 1);
			gtk_grid_attach(mGrid, GTK_WIDGET(mCloseButton), 2, 0, 1, 1);
			gtk_grid_attach(mGrid, GTK_WIDGET(mPreview), 0, 1, 3, 1);
		}

		Help::Gtk::cssClassRemove(GTK_WIDGET(mItem), "overview_alt");

		gtk_widget_set_halign(GTK_WIDGET(mIcon), GTK_ALIGN_START);
		gtk_widget_set_valign(GTK_WIDGET(mIcon), GTK_ALIGN_CENTER);
		gtk_widget_set_hexpand(GTK_WIDGET(mIcon), false);
		gtk_widget_set_margin_top(GTK_WIDGET(mIcon), 0);
		gtk_widget_set_margin_bottom(GTK_WIDGET(mIcon), 0);
		gtk_widget_set_margin_start(GTK_WIDGET(mIcon), 0);
		gtk_widget_set_margin_end(GTK_WIDGET(mIcon), 0);

		gtk_widget_set_halign(GTK_WIDGET(mLabel), GTK_ALIGN_FILL);
		gtk_widget_set_valign(GTK_WIDGET(mLabel), GTK_ALIGN_CENTER);
		gtk_widget_set_hexpand(GTK_WIDGET(mLabel), true);
		gtk_label_set_xalign(mLabel, 0);
		gtk_label_set_width_chars(mLabel, 26);
		gtk_label_set_max_width_chars(mLabel, -1);
		gtk_widget_set_margin_top(GTK_WIDGET(mLabel), 0);
		gtk_widget_set_margin_bottom(GTK_WIDGET(mLabel), 0);
		gtk_widget_set_margin_start(GTK_WIDGET(mLabel), 0);
		gtk_widget_set_margin_end(GTK_WIDGET(mLabel), 0);

		gtk_widget_set_halign(GTK_WIDGET(mCloseButton), GTK_ALIGN_END);
		gtk_widget_set_valign(GTK_WIDGET(mCloseButton), GTK_ALIGN_CENTER);
		gtk_widget_set_margin_top(GTK_WIDGET(mCloseButton), 0);
		gtk_widget_set_margin_end(GTK_WIDGET(mCloseButton), 0);
		gtk_widget_set_margin_bottom(GTK_WIDGET(mCloseButton), 0);
		gtk_widget_set_margin_start(GTK_WIDGET(mCloseButton), 0);

		gtk_widget_set_halign(GTK_WIDGET(mPreview), GTK_ALIGN_FILL);
		gtk_widget_set_valign(GTK_WIDGET(mPreview), GTK_ALIGN_FILL);

		gtk_widget_set_opacity(GTK_WIDGET(mLabel), 1.0);
		gtk_widget_set_opacity(GTK_WIDGET(mCloseButton), 1.0);
		gtk_widget_set_sensitive(GTK_WIDGET(mCloseButton), TRUE);

		gtk_widget_set_visible(GTK_WIDGET(mPreview), Settings::showPreviews);
		gtk_widget_show(GTK_WIDGET(mIcon));
		gtk_widget_show(GTK_WIDGET(mCloseButton));
		gtk_widget_show(GTK_WIDGET(mLabel));

		updateIcon();
	}
}
