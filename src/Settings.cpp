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

#include "Settings.hpp"
#include "Hotkeys.hpp"
#include "LauncherEntry.hpp"
#include "Overview.hpp"

#include <xfconf/xfconf.h>

namespace Settings
{
	XfconfChannel* mChannel = nullptr;
	Store::AutoPtr<GKeyFile> mLegacyFile;
	std::string mBase;

	State<bool> forceIconSize;
	State<int> iconSize;

	State<bool> expandDock;

	State<bool> noWindowsListIfSingle;
	State<bool> onlyDisplayVisible;
	State<bool> onlyDisplayScreen;
	State<bool> showPreviews;
	State<bool> alternativeOverviewStyle;
	State<int> middleButtonBehavior;
	State<bool> hotCornerEnabled;
	State<int> hotCornerCorner;
	State<int> hotCornerDelay;

	State<int> indicatorOrientation;
	State<int> indicatorStyle;
	State<int> inactiveIndicatorStyle;
	State<int> indicatorSpacing;
	State<int> indicatorThickness;
	State<int> dockItemSpacing;
	State<bool> indicatorColorFromTheme;
	State<std::shared_ptr<GdkRGBA>> indicatorColor;
	State<std::shared_ptr<GdkRGBA>> inactiveColor;

	State<bool> keyComboActive;
	State<bool> keyAloneActive;

	State<std::list<std::string>> pinnedAppList;
	State<std::pair<std::list<std::string>, std::list<std::string>>> userSetApps;

	State<bool> showWindowCount;
	State<bool> disableLauncherCounts;
	State<bool> ungroupWindows;
	State<int> dockSize;
	State<int> previewWidth;
	State<int> previewHeight;
	State<int> previewGap;
	State<int> previewSleep;

	// Settings are stored in the "asmi-taskbar" xfconf channel
	// (~/.config/xfce4/xfconf/xfce-perchannel-xml/asmi-taskbar.xml).
	// Only one instance of this plugin is allowed, so a fixed property base is
	// used. This keeps the settings when the plugin is removed and re-added.
	static std::string prop(const gchar* key)
	{
		return mBase + "/" + key;
	}

	static bool channelHas(const gchar* key)
	{
		return mChannel != nullptr && xfconf_channel_has_property(mChannel, prop(key).c_str());
	}

	static bool legacyHas(const gchar* key)
	{
		return mLegacyFile && g_key_file_has_key(mLegacyFile.get(), "user", key, nullptr);
	}

	static bool getBool(const gchar* key, bool def)
	{
		if (channelHas(key))
			return xfconf_channel_get_bool(mChannel, prop(key).c_str(), def);

		if (legacyHas(key))
		{
			GError* error = nullptr;
			gboolean value = g_key_file_get_boolean(mLegacyFile.get(), "user", key, &error);
			g_clear_error(&error);
			return value;
		}

		return def;
	}

	static int getInt(const gchar* key, int def)
	{
		if (channelHas(key))
			return xfconf_channel_get_int(mChannel, prop(key).c_str(), def);

		if (legacyHas(key))
		{
			GError* error = nullptr;
			gint value = g_key_file_get_integer(mLegacyFile.get(), "user", key, &error);
			g_clear_error(&error);
			return value;
		}

		return def;
	}

	static std::string getString(const gchar* key, const gchar* def)
	{
		std::string result = def != nullptr ? def : "";

		if (channelHas(key))
		{
			gchar* value = xfconf_channel_get_string(mChannel, prop(key).c_str(), def);
			if (value != nullptr)
			{
				result = value;
				g_free(value);
			}
		}
		else if (legacyHas(key))
		{
			GError* error = nullptr;
			gchar* value = g_key_file_get_string(mLegacyFile.get(), "user", key, &error);
			if (value != nullptr)
			{
				result = value;
				g_free(value);
			}
			g_clear_error(&error);
		}

		return result;
	}

	static std::list<std::string> getStringList(const gchar* key)
	{
		std::list<std::string> result;
		gchar** values = nullptr;

		if (channelHas(key))
			values = xfconf_channel_get_string_list(mChannel, prop(key).c_str());
		else if (legacyHas(key))
			values = g_key_file_get_string_list(mLegacyFile.get(), "user", key, nullptr, nullptr);

		if (values != nullptr)
		{
			for (gchar** value = values; *value != nullptr; ++value)
				result.push_back(*value);
			g_strfreev(values);
		}

		return result;
	}

	static void setBool(const gchar* key, bool value)
	{
		if (mChannel != nullptr)
			xfconf_channel_set_bool(mChannel, prop(key).c_str(), value);
	}

	static void setInt(const gchar* key, int value)
	{
		if (mChannel != nullptr)
			xfconf_channel_set_int(mChannel, prop(key).c_str(), value);
	}

	static void setString(const gchar* key, const gchar* value)
	{
		if (mChannel != nullptr)
			xfconf_channel_set_string(mChannel, prop(key).c_str(), value);
	}

	static void setStringList(const gchar* key, const std::list<std::string>& list)
	{
		if (mChannel == nullptr)
			return;

		std::vector<const gchar*> values;
		values.reserve(list.size() + 1);

		for (const std::string& value : list)
			values.push_back(value.c_str());

		values.push_back(nullptr);
		xfconf_channel_set_string_list(mChannel, prop(key).c_str(), values.data());
	}

	static void setColor(const gchar* key, std::shared_ptr<GdkRGBA> color)
	{
		gchar* str = gdk_rgba_to_string(color.get());
		setString(key, str);
		g_free(str);
	}

	// Writes every setting to the channel. Used once on startup so the xfconf
	// file is populated (and so settings migrated from the legacy rc file stick).
	static void saveAll()
	{
		setBool("forceIconSize", forceIconSize.get());
		setInt("iconSize", iconSize.get());
		setBool("expandDock", expandDock.get());
		setBool("noWindowsListIfSingle", noWindowsListIfSingle.get());
		setBool("onlyDisplayVisible", onlyDisplayVisible.get());
		setBool("onlyDisplayScreen", onlyDisplayScreen.get());
		setBool("showPreviews", showPreviews.get());
		setBool("alternativeOverviewStyle", alternativeOverviewStyle.get());
		setInt("previewWidth", previewWidth.get());
		setInt("previewHeight", previewHeight.get());
		setInt("previewGap", previewGap.get());
		setBool("showWindowCount", showWindowCount.get());
		setBool("disableLauncherCounts", disableLauncherCounts.get());
		setBool("ungroupWindows", ungroupWindows.get());
		setInt("middleButtonBehavior", middleButtonBehavior.get());
		setBool("hotCornerEnabled", hotCornerEnabled.get());
		setInt("hotCornerCorner", hotCornerCorner.get());
		setInt("hotCornerDelay", hotCornerDelay.get());
		setInt("indicatorOrientation", indicatorOrientation.get());
		setInt("indicatorStyle", indicatorStyle.get());
		setInt("inactiveIndicatorStyle", inactiveIndicatorStyle.get());
		setInt("indicatorSpacing", indicatorSpacing.get());
		setInt("indicatorThickness", indicatorThickness.get());
		setInt("dockItemSpacing", dockItemSpacing.get());
		setBool("indicatorColorFromTheme", indicatorColorFromTheme.get());
		setColor("indicatorColor", indicatorColor.get());
		setColor("inactiveColor", inactiveColor.get());
		setBool("keyComboActive", keyComboActive.get());
		setBool("keyAloneActive", keyAloneActive.get());
		setStringList("pinned", pinnedAppList.get());
		setStringList("userSetIds", userSetApps.get().first);
		setStringList("userSetPaths", userSetApps.get().second);
		setInt("dockSize", dockSize.get());
		setInt("previewSleep", previewSleep.get());
	}

	void init()
	{
		GError* error = nullptr;

		if (!xfconf_init(&error))
		{
			g_critical("Failed to initialize Xfconf: %s", error->message);
			g_clear_error(&error);
		}

		mChannel = xfconf_channel_new("asmi-taskbar");
		mBase = "";

		// Load the legacy per-plugin rc file (if any) so existing settings are
		// kept. Its keys are used as a fallback for values not yet in xfconf.
		gchar* path = xfce_panel_plugin_save_location(Plugin::mXfPlugin, true);

		// Also look for the pre-rename config file (docklike-<id>.rc) so pinned
		// apps and other settings are carried over from the old fork.
		gchar* legacyPath = nullptr;
		if (path != nullptr)
		{
			gchar* base = g_path_get_basename(path);
			if (g_str_has_prefix(base, "asmi-taskbar-"))
			{
				gchar* dir = g_path_get_dirname(path);
				gchar* oldBase = g_strconcat("docklike-", base + strlen("asmi-taskbar-"), nullptr);
				legacyPath = g_build_filename(dir, oldBase, nullptr);
				g_free(oldBase);
				g_free(dir);
			}
			g_free(base);
		}

		const gchar* candidates[2] = { path, legacyPath };
		for (int i = 0; i < 2; i++)
		{
			if (candidates[i] == nullptr || !g_file_test(candidates[i], G_FILE_TEST_IS_REGULAR))
				continue;

			GKeyFile* legacy = g_key_file_new();
			if (g_key_file_load_from_file(legacy, candidates[i], G_KEY_FILE_NONE, nullptr))
			{
				mLegacyFile = Store::AutoPtr<GKeyFile>(legacy, (GDestroyNotify)g_key_file_unref);
				break;
			}
			g_key_file_unref(legacy);
		}

		g_free(legacyPath);
		g_free(path);

		int intValue;

		showPreviews.setup(getBool("showPreviews", true),
			[](bool _showPreviews) -> void {
				setBool("showPreviews", _showPreviews);

				Dock::forEachGroup([](Group* g) -> void {
					g->mGroupMenu.showPreviewsChanged();
				});
			});

		alternativeOverviewStyle.setup(getBool("alternativeOverviewStyle", true),
			[](bool _alternativeOverviewStyle) -> void {
				setBool("alternativeOverviewStyle", _alternativeOverviewStyle);
			});

		intValue = getInt("previewWidth", 0);
		previewWidth.setup(intValue > 0 ? intValue : defPreviewWidth,
			[](int _previewWidth) -> void {
				if (_previewWidth <= 0)
				{
					previewWidth.set(defPreviewWidth);
					return;
				}

				setInt("previewWidth", _previewWidth);

				Dock::forEachGroup([](Group* g) -> void {
					g->mGroupMenu.showPreviewsChanged();
				});
			});

		intValue = getInt("previewHeight", 0);
		previewHeight.setup(intValue > 0 ? intValue : defPreviewHeight,
			[](int _previewHeight) -> void {
				if (_previewHeight <= 0)
				{
					previewHeight.set(defPreviewHeight);
					return;
				}

				setInt("previewHeight", _previewHeight);

				Dock::forEachGroup([](Group* g) -> void {
					g->mGroupMenu.showPreviewsChanged();
				});
			});

		intValue = getInt("previewGap", defPreviewGap);
		previewGap.setup(CLAMP(intValue, 0, 64),
			[](int _previewGap) -> void {
				setInt("previewGap", _previewGap);

				Dock::forEachGroup([](Group* g) -> void {
					g->mGroupMenu.reposition();
				});
			});

		gboolean expandValue = getBool("expandDock", true);
		expandDock.setup(expandValue,
			[](bool _expandDock) -> void {
				setBool("expandDock", _expandDock);

				xfce_panel_plugin_set_expand(Plugin::mXfPlugin, _expandDock);
			});

		showWindowCount.setup(getBool("showWindowCount", false),
			[](bool _showWindowCount) -> void {
				setBool("showWindowCount", _showWindowCount);

				Dock::drawGroups();
			});

		disableLauncherCounts.setup(getBool("disableLauncherCounts", false),
			[](bool _disableLauncherCounts) -> void {
				setBool("disableLauncherCounts", _disableLauncherCounts);

				LauncherEntry::refreshGroups();
			});

		ungroupWindows.setup(getBool("ungroupWindows", false),
			[](bool _ungroupWindows) -> void {
				setBool("ungroupWindows", _ungroupWindows);

				Dock::drawGroups();
			});

		middleButtonBehavior.setup(getInt("middleButtonBehavior", BEHAVIOR_DO_NOTHING),
			[](int _middleButtonBehavior) -> void {
				setInt("middleButtonBehavior", _middleButtonBehavior);
			});

		hotCornerEnabled.setup(getBool("hotCornerEnabled", false),
			[](bool _hotCornerEnabled) -> void {
				setBool("hotCornerEnabled", _hotCornerEnabled);
				Overview::updateSettings();
			});

		hotCornerCorner.setup(CLAMP(getInt("hotCornerCorner", 0), 0, 3),
			[](int _hotCornerCorner) -> void {
				setInt("hotCornerCorner", _hotCornerCorner);
				Overview::updateSettings();
			});

		intValue = getInt("hotCornerDelay", defHotCornerDelay);
		hotCornerDelay.setup(CLAMP(intValue, 0, 5000),
			[](int _hotCornerDelay) -> void {
				setInt("hotCornerDelay", _hotCornerDelay);
				Overview::updateSettings();
			});

		indicatorOrientation.setup(getInt("indicatorOrientation", 0),
			[](int _indicatorOrientation) -> void {
				setInt("indicatorOrientation", _indicatorOrientation);

				Dock::onPanelResize();
			});

		forceIconSize.setup(getBool("forceIconSize", false),
			[](bool _forceIconSize) -> void {
				setBool("forceIconSize", _forceIconSize);

				Dock::onPanelResize();
			});

		iconSize.setup(CLAMP(getInt("iconSize", 0), Settings::minIconSize, Settings::maxIconSize),
			[](int _iconSize) -> void {
				int clamped = CLAMP(_iconSize, Settings::minIconSize, Settings::maxIconSize);
				if (clamped != _iconSize)
				{
					iconSize.set(clamped);
					return;
				}
				setInt("iconSize", _iconSize);

				Dock::onPanelResize();
			});

		indicatorStyle.setup(getInt("indicatorStyle", STYLE_SEGMENTS),
			[](int _indicatorStyle) -> void {
				setInt("indicatorStyle", _indicatorStyle);

				Dock::drawGroups();
			});

		inactiveIndicatorStyle.setup(getInt("inactiveIndicatorStyle", STYLE_SEGMENTS),
			[](int _inactiveIndicatorStyle) -> void {
				setInt("inactiveIndicatorStyle", _inactiveIndicatorStyle);

				Dock::drawGroups();
			});

		indicatorSpacing.setup(CLAMP(getInt("indicatorSpacing", 2), 0, 64),
			[](int _indicatorSpacing) -> void {
				setInt("indicatorSpacing", _indicatorSpacing);

				Dock::onPanelResize();
			});

		intValue = getInt("indicatorThickness", Settings::defIndicatorThickness);
		indicatorThickness.setup(CLAMP(intValue, 1, 64),
			[](int _indicatorThickness) -> void {
				setInt("indicatorThickness", _indicatorThickness);

				Dock::onPanelResize();
			});

		intValue = getInt("dockItemSpacing", Settings::defDockItemSpacing);
		dockItemSpacing.setup(CLAMP(intValue, 0, 64),
			[](int _dockItemSpacing) -> void {
				setInt("dockItemSpacing", _dockItemSpacing);

				Dock::onPanelResize();
			});

		indicatorColorFromTheme.setup(getBool("indicatorColorFromTheme", true),
			[](bool _indicatorColorFromTheme) -> void {
				setBool("indicatorColorFromTheme", _indicatorColorFromTheme);

				Theme::load();
				Dock::drawGroups();
			});

		std::string colorString = getString("indicatorColor", nullptr);
		std::shared_ptr<GdkRGBA> color(g_new(GdkRGBA, 1), g_free);

		if (colorString.empty() || !gdk_rgba_parse(color.get(), colorString.c_str()))
			gdk_rgba_parse(color.get(), "rgb(53,132,228)");

		indicatorColor.setup(color,
			[](std::shared_ptr<GdkRGBA> _indicatorColor) -> void {
				setColor("indicatorColor", _indicatorColor);

				Theme::load();
				Dock::drawGroups();
			});

		colorString = getString("inactiveColor", nullptr);
		color = std::shared_ptr<GdkRGBA>(g_new(GdkRGBA, 1), g_free);

		if (colorString.empty() || !gdk_rgba_parse(color.get(), colorString.c_str()))
			gdk_rgba_parse(color.get(), "rgb(51,209,122)");

		inactiveColor.setup(color,
			[](std::shared_ptr<GdkRGBA> _inactiveColor) -> void {
				setColor("inactiveColor", _inactiveColor);

				Theme::load();
				Dock::drawGroups();
			});

		noWindowsListIfSingle.setup(getBool("noWindowsListIfSingle", false),
			[](bool _noWindowsListIfSingle) -> void {
				setBool("noWindowsListIfSingle", _noWindowsListIfSingle);
			});

		onlyDisplayVisible.setup(getBool("onlyDisplayVisible", false),
			[](bool _onlyDisplayVisible) -> void {
				setBool("onlyDisplayVisible", _onlyDisplayVisible);
			});

		onlyDisplayScreen.setup(getBool("onlyDisplayScreen", false),
			[](bool _onlyDisplayScreen) -> void {
				setBool("onlyDisplayScreen", _onlyDisplayScreen);
			});

		keyComboActive.setup(getBool("keyComboActive", false),
			[](bool _keyComboActive) -> void {
				setBool("keyComboActive", _keyComboActive);
				Hotkeys::updateSettings();
			});

		keyAloneActive.setup(getBool("keyAloneActive", false),
			[](bool _keyAloneActive) -> void {
				setBool("keyAloneActive", _keyAloneActive);
				Hotkeys::updateSettings();
			});

		std::list<std::string> pinnedList = getStringList("pinned");

		// try to be backward compatible: retrieve ids from paths
		for (std::list<std::string>::iterator it = pinnedList.begin(); it != pinnedList.end(); ++it)
			if (!it->empty() && (*it)[0] == '/' && g_str_has_suffix(it->c_str(), ".desktop"))
				*it = Help::String::pathBasename(*it, true);

		pinnedAppList.setup(pinnedList,
			[](std::list<std::string> list) -> void {
				setStringList("pinned", list);
			});

		std::list<std::string> ids = getStringList("userSetIds");
		std::list<std::string> paths = getStringList("userSetPaths");
		ids.resize(paths.size());
		paths.resize(ids.size());
		std::pair<std::list<std::string>, std::list<std::string>> _userSetApps{ids, paths};

		userSetApps.setup(_userSetApps,
			[](std::pair<std::list<std::string>, std::list<std::string>> pair) -> void {
				setStringList("userSetIds", pair.first);
				setStringList("userSetPaths", pair.second);
			});

		// HIDDEN SETTINGS:
		dockSize.setup(getInt("dockSize", 0),
			[](int _dockSize) -> void {
				setInt("dockSize", _dockSize);
			});

		previewSleep.setup(getInt("previewSleep", 0),
			[](int _previewSleep) -> void {
				setInt("previewSleep", _previewSleep);
			});

		saveAll();
	}

	void finalize()
	{
		if (mChannel != nullptr)
		{
			g_object_unref(mChannel);
			mChannel = nullptr;
		}

		mLegacyFile.reset();
		indicatorColor.get().reset();
		inactiveColor.get().reset();
		pinnedAppList.get().clear();
	}
} // namespace Settings
