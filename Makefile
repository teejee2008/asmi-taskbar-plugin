# Simple Makefile wrapper around meson + ninja for asmi-taskbar-plugin.
#
# Install prefix (default /usr). Override with: make PREFIX=/opt/foo ...
# Library dir (default lib). On Debian/Ubuntu multiarch systems use e.g.
#   make LIBDIR=lib/x86_64-linux-gnu ...

PREFIX   ?= /usr
DATADIR  ?= $(PREFIX)/share
LIBDIR   ?= $(PREFIX)/lib
BUILDDIR ?= build
DESTDIR  ?=

# Meson shared_module name -> installed as lib<MODULE>.so
MODULE   := asmi-taskbar
DESKTOP  := asmi-taskbar-plugin.desktop
ICON     := asmi-taskbar-plugin.png

.PHONY: all install uninstall clean help

all:
	@meson setup $(BUILDDIR) -Dprefix=$(PREFIX)
	@ninja -C $(BUILDDIR)

install: all
	@DESTDIR=$(DESTDIR) ninja -C $(BUILDDIR) install

uninstall:
	@rm -f $(DESTDIR)$(DATADIR)/pixmaps/$(ICON)
	@rm -f $(DESTDIR)$(DATADIR)/xfce4/panel/plugins/$(DESKTOP)
	@rm -f $(DESTDIR)$(LIBDIR)/xfce4/panel/plugins/lib$(MODULE).so
	@rm -f $(DESTDIR)$(DATADIR)/locale/*/LC_MESSAGES/asmi-taskbar-plugin.mo
	@echo "uninstall: removed known files under $(DESTDIR)$(PREFIX)"

clean:
	@rm -rf $(BUILDDIR)

help:
	@echo "Usage: make [target] [VAR=value] ..."
	@echo ""
	@echo "Targets:"
	@echo "  all       - meson setup + ninja build in $(BUILDDIR)/"
	@echo "  install   - build and install to $(PREFIX)"
	@echo "  uninstall - remove installed files from known paths under $(PREFIX)"
	@echo "  clean     - remove $(BUILDDIR)/ entirely"
	@echo ""
	@echo "Variables:"
	@echo "  PREFIX    install prefix            (default $(PREFIX))"
	@echo "  DATADIR   data dir                  (default $(DATADIR))"
	@echo "  LIBDIR    library dir               (default $(LIBDIR))"
	@echo "  BUILDDIR  meson build directory     (default $(BUILDDIR))"
	@echo "  DESTDIR   staging dir for install   (default $(DESTDIR))"
