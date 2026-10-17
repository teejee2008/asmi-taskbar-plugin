# Asmi Taskbar for Xfce

A modern, minimalist taskbar for Xfce

Asmi Taskbar behaves similarly to many other desktop environments and operating systems. Wherein all application windows are grouped together as an icon and can be pinned to act as a launcher when the application is not running. Commonly referred to as a dock.

For usage instructions, keyboard shortcuts, and screenshots, see:
<https://github.com/teejee2008/asmi-taskbar-plugin>.

## Build & Install

```bash
tar xvf asmi-taskbar-plugin-<version>.tar.xz && cd asmi-taskbar-plugin-<version>
meson setup build 
meson compile -C build 
sudo meson install -C build
```

### From git

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
