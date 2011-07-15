SUBDIRS = icons theme

@INTLTOOL_DESKTOP_RULE@

if WITH_MEEGO
paneldir = @PANEL_DIR@
panel_DATA = carrick.desktop

autostartdir = $(sysconfdir)/xdg/autostart
autostart_DATA = carrick.desktop

else

desktopdir = $(datadir)/applications
desktop_DATA = carrick-standalone.desktop

endif

EXTRA_DIST = carrick.desktop.in.in carrick-standalone.desktop.in.in
CLEANFILES = carrick.desktop carrick-standalone.desktop

-include $(top_srcdir)/git.mk
