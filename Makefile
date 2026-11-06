#!/bin/bash
SHELL=/bin/bash

PREFIX    ?= /usr
DATADIR   ?= $(PREFIX)/share
BUILDDIR  ?= build
DESTDIR   ?=

PKG_CONFIG      ?= pkg-config
XFCE_PANEL_PKG  ?= libxfce4panel-2.0
LIBDIR          ?= $(shell $(PKG_CONFIG) --variable=libdir $(XFCE_PANEL_PKG) 2>/dev/null)/xfce4/panel/plugins
ifeq ($(LIBDIR),/xfce4/panel/plugins)
LIBDIR := $(PREFIX)/lib/xfce4/panel/plugins
endif

# Meson shared_module name -> installed as lib<MODULE>.so
MODULE   := asmi-taskbar
DESKTOP  := asmi-taskbar-plugin.desktop
ICON     := asmi-taskbar-plugin.png

.PHONY: all install uninstall clean help update-about-version

all:
	make update-about-version
	@meson setup --wipe $(BUILDDIR) -Dprefix=$(PREFIX)
	@ninja -C $(BUILDDIR)

install: all
	@DESTDIR=$(DESTDIR) ninja -C $(BUILDDIR) install

uninstall:
	@rm -f $(DESTDIR)$(DATADIR)/pixmaps/$(ICON)
	@rm -f $(DESTDIR)$(DATADIR)/xfce4/panel/plugins/$(DESKTOP)
	@rm -f $(DESTDIR)$(LIBDIR)/lib$(MODULE).so
	@rm -f $(DESTDIR)$(DATADIR)/locale/*/LC_MESSAGES/asmi-taskbar-plugin.mo
	@echo "uninstall: removed known files under $(DESTDIR)$(PREFIX)"

clean:
	@rm -rf $(BUILDDIR)

# Update ABOUT_VERSION in src/Plugin.cpp from the top debian/changelog entry.
# debian/changelog first line looks like:  asmi-taskbar-plugin (26.11) resolute; urgency=medium
update-about-version:
	@version=$$(sed -n '1s/.*(\([^)]*\)).*/\1/p' debian/changelog); \
	if [ -z "$$version" ]; then \
		echo "update-about-version: could not parse version from debian/changelog" >&2; \
		exit 1; \
	fi; \
	sed -i 's/const char\* ABOUT_VERSION = .*/const char* ABOUT_VERSION = "'"$$version"'";/' src/Plugin.cpp; \
	echo "update-about-version: set ABOUT_VERSION to $$version"

testrun:
	make all && sudo make install && xfce4-panel -r

help:
	@echo "Usage: make [target] [VAR=value] ..."
	@echo ""
	@echo "Targets:"
	@echo "  all                - meson setup + ninja build in $(BUILDDIR)/"
	@echo "  install            - build and install to $(PREFIX)"
	@echo "  uninstall          - remove installed files from known paths under $(PREFIX)"
	@echo "  clean              - remove $(BUILDDIR)/ entirely"
	@echo "  update-about-version - replace ABOUT_VERSION in src/Plugin.cpp with the version from debian/changelog"
	@echo ""
	@echo "Variables:"
	@echo "  PREFIX      install prefix                        (default $(PREFIX))"
	@echo "  DATADIR     data dir                              (default $(DATADIR))"
	@echo "  LIBDIR      panel plugin module dir               (default $(LIBDIR))"
	@echo "              derived via pkg-config libxfce4panel-2.0"
	@echo "              override: make LIBDIR=/usr/lib/xfce4/panel/plugins"
	@echo "  BUILDDIR    meson build directory                 (default $(BUILDDIR))"
	@echo "  DESTDIR     staging dir for install               (default $(DESTDIR))"
