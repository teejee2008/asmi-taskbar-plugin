# Asmi Taskbar for Xfce

Forked from [xfce4-docklike-plugin](https://gitlab.xfce.org/panel-plugins/xfce4-docklike-plugin)

A modern, minimalist taskbar for Xfce

Asmi Taskbar behaves similarly to many other desktop environments and operating systems. Wherein all application windows are grouped together as an icon and can be pinned to act as a launcher when the application is not running. Commonly referred to as a dock.

### New Features

**Drag and drop**
* Drag dock items to rearrange without holding Ctrl
* Drag and drop apps from WhiskerMenu to pin them to the dock
* Drag out apps to unpin and remove
* Drag and drop files from Thunar on a dock item to open files in that app
* Drag window previews to rearrange

**New Settings**
* Redesigned settings window
* Add option for changing indicator line thickness
* Add option for changing indicator gap
* Add option for adding gap between panel and previews
* New indicator styles - Segmented, Badge, and Count
* Add option to expand dock in panel
* Add option to pick indicator color from app icon
* Add _Reset_ button to reset all settings to default values
* Store settings in the `asmi-taskbar` xfconf channel
* Settings will persist if you remove and re-add the plugin to panel
* Add setting to ungroup windows into separate dock items

**Overview Hotcorner**
* Option to enable a hotcorner to trigger window overview mode similar to
overview mode of GNOME shell.
This shows a window picker with previews of all open windows.
* Option to close windows in overview mode
* Option to choose the corner (top-left, top-right, bottom-left, bottom-right)

**Other Improvements**
* Show launch spinner until the app window opens
* When there are too many open windows, the window previews flow into rows and columns instead of going off-screen.
* Windows previews will maintain their order when switching between windows
* Show placeholder for window preview when no thumbnail is available
* Add minimize and unminimize actions to dock item context menu
* Add 'Launch application' action to dock item context menu
## Build & Install

```bash
git clone https://github.com/teejee2008/asmi-taskbar-plugin.git && cd asmi-taskbar-plugin
meson setup build
meson compile -C build
sudo meson install -C build
```

### Uninstallation

```bash
sudo ninja uninstall -C build
```

## Reporting bugs

To assist with troubleshooting plugin issues, please run Xfce Panel in debugging mode, and include the relevant output in issues.

<https://github.com/teejee2008/asmi-taskbar-plugin/issues>

### Running Xfce Panel in debugging mode

- Open a terminal
- Quit the Xfce Panel using `xfce4-panel -q`
- Start the panel in debugging mode with `G_MESSAGES_PREFIXED= G_MESSAGES_DEBUG=asmi-taskbar-plugin xfce4-panel`
- Perform any actions you want to debug and copy the relevant output
- Stop debugging by pressing `Ctrl^C`
- Start the panel again using `xfce4-panel &`

The Xfce wiki has more details on panel debugging:
<https://docs.xfce.org/xfce/xfce4-panel/debugging>.
