prefix = /usr/local
exec_prefix = ${prefix}
datarootdir = ${prefix}/share
bindir = ${exec_prefix}/bin
datadir = ${datarootdir}
pkgdatadir = ${datarootdir}/tilem2
mandir = ${datarootdir}/man

top_builddir = ..
top_srcdir = ..
srcdir = .



CC = clang
CFLAGS = -g -O2 -W -Wall -Wwrite-strings
CPPFLAGS = 
DEFS = -DHAVE_CONFIG_H
GUI_LDFLAGS = 
INSTALL = /usr/bin/install -c
INSTALL_PROGRAM = ${INSTALL}
LDFLAGS = 
LIBS = 
SHELL = /nix/store/yi3x6xpilh0bq0bgb61imy6gpv8b1la4-bash-5.3p3/bin/bash
WINDRES = 

GTK_CFLAGS = 
GTK_LIBS = 

SDL2_CFLAGS = $(shell pkg-config --cflags sdl2)
SDL2_LIBS = $(shell pkg-config --libs sdl2)

SDL2_TTF_CFLAGS = $(shell pkg-config --cflags SDL2_ttf)
SDL2_TTF_LIBS = $(shell pkg-config --libs SDL2_ttf)

SDL2_IMAGE_CFLAGS = $(shell pkg-config --cflags SDL2_image)
SDL2_IMAGE_LIBS = $(shell pkg-config --libs SDL2_image)

TICALCS_CFLAGS = 
TICALCS_LIBS = 

TILEMCORE_CFLAGS = -I$(top_srcdir)/emu
TILEMCORE_LIBS = -L$(top_builddir)/emu -ltilemcore

TILEMDB_CFLAGS = -I$(top_srcdir)/db
TILEMDB_LIBS = -L$(top_builddir)/db -ltilemdb

DEF_SHARE_DIR = -DSHARE_DIR=\"$(pkgdatadir)\" \
	-DUNINSTALLED_SHARE_DIR=\"$(top_srcdir)/data\"

gui_extra_objects = 

objects = tilem2.o \
	address.o \
	animatedgif.o \
	animation.o \
	breakpoints.o \
	config.o \
	charmap.o \
	debugger.o \
	disasmview.o \
	emulator.o \
	emucore.o \
	emuwin.o \
	event.o \
	filedlg.o \
	files.o \
	fixedtreeview.o \
	gifencod.o \
	headless_script.o \
	trace.o \
	icons.o \
	keybindings.o \
	keypaddlg.o \
	link.o \
	macro.o \
	memmodel.o \
	memview.o \
	memory.o \
	pbar.o \
	preferences.o \
	sdldebugger.o \
	sdlicons.o \
	sdlskin.o \
	sdlui.o \
	sdlscreenshot.o \
	sendfile.o \
	screenshot.o \
	skinops.o \
	ti81prg.o \
	menu.o \
	rcvmenu.o \
	tool.o \
	$(gui_extra_objects)

libs = $(TILEMDB_LIBS) $(TILEMCORE_LIBS) $(GTK_LIBS) $(SDL2_LIBS) $(SDL2_TTF_LIBS) $(SDL2_IMAGE_LIBS) $(TICALCS_LIBS) $(LIBS)

compile = $(CC) -I$(top_builddir) -I$(srcdir) -I$(top_srcdir)/headless \
	$(CFLAGS) $(CPPFLAGS) $(DEFS) \
	$(TILEMCORE_CFLAGS) $(TILEMDB_CFLAGS) \
	$(GTK_CFLAGS) $(SDL2_CFLAGS) $(SDL2_TTF_CFLAGS) $(SDL2_IMAGE_CFLAGS) $(TICALCS_CFLAGS)

link = $(CC) $(CFLAGS) $(LDFLAGS) $(GUI_LDFLAGS) 

common_headers = ../config.h ../emu/tilem.h ../db/tilemdb.h \
	gui.h varentry.h emulator.h debugger.h emuwin.h skinops.h animation.h \
	gtk-compat.h msgbox.h fixedtreeview.h

all: tilem2

#Main emulator GUI
tilem2: $(objects) ../emu/libtilemcore.a
	$(link) -o tilem2 $(objects) $(libs)

tilem2.o: tilem2.c icons.h files.h $(common_headers)
	$(compile) -c $(srcdir)/tilem2.c

# Debugger
debugger.o: debugger.c disasmview.h $(common_headers)
	$(compile) -c $(srcdir)/debugger.c

# Disassembly view
disasmview.o: disasmview.c disasmview.h $(common_headers)
	$(compile) -c $(srcdir)/disasmview.c

# Memory view
memview.o: memview.c memmodel.h $(common_headers)
	$(compile) -c $(srcdir)/memview.c

# Tree model interface for calc memory
memmodel.o: memmodel.c memmodel.h $(common_headers)
	$(compile) -c $(srcdir)/memmodel.c

# Breakpoint dialog
breakpoints.o: breakpoints.c $(common_headers)
	$(compile) -c $(srcdir)/breakpoints.c

# Utility functions for debugging
address.o: address.c $(common_headers)
	$(compile) -c $(srcdir)/address.c

# Keypad dialog
keypaddlg.o: keypaddlg.c $(common_headers)
	$(compile) -c $(srcdir)/keypaddlg.c

# Memory management and messages
memory.o: memory.c ../emu/tilem.h
	$(compile) -c $(srcdir)/memory.c

# Emulator management
emulator.o: emulator.c emucore.h $(common_headers)
	$(compile) -c $(srcdir)/emulator.c

# Emulator main loop
emucore.o: emucore.c emucore.h $(common_headers)
	$(compile) -c $(srcdir)/emucore.c

# Emulator GUI (main window)
emuwin.o: emuwin.c $(common_headers)
	$(compile) -c $(srcdir)/emuwin.c

# SDL2 UI
sdlui.o: sdlui.c sdlui.h sdlicons.h sdlskin.h $(common_headers)
	$(compile) -c $(srcdir)/sdlui.c

sdldebugger.o: sdldebugger.c sdldebugger.h sdlicons.h $(common_headers)
	$(compile) -c $(srcdir)/sdldebugger.c

sdlskin.o: sdlskin.c sdlskin.h $(common_headers)
	$(compile) -c $(srcdir)/sdlskin.c
sdlicons.o: sdlicons.c sdlicons.h files.h $(common_headers)
	$(compile) -c $(srcdir)/sdlicons.c

sdlscreenshot.o: sdlscreenshot.c sdlscreenshot.h $(common_headers)
	$(compile) -c $(srcdir)/sdlscreenshot.c

# Handle events
event.o: event.c $(common_headers)
	$(compile) -c $(srcdir)/event.c

# Preferences dialog
preferences.o: preferences.c $(common_headers)
	$(compile) -c $(srcdir)/preferences.c 

# Open skin (skn format file) originally created by Julien Blache and Romain Lievins
skinops.o: skinops.c skinops.h 
	$(compile) -c $(srcdir)/skinops.c

# Popups and other stuff
tool.o: tool.c $(common_headers)
	$(compile) -c $(srcdir)/tool.c

# Manage config.ini 
config.o: config.c files.h $(common_headers)
	$(compile) -c $(srcdir)/config.c

# Handle internal link
link.o: link.c emucore.h ti81prg.h $(common_headers)
	$(compile) -c $(srcdir)/link.c

# Handle macro
macro.o: macro.c $(common_headers)
	$(compile) -c $(srcdir)/macro.c

# Create and modify animated gif
gifencod.o: gifencod.c gifencod.h
	$(compile) -c $(srcdir)/gifencod.c

# Handle screenshot anim (animated gif)
animatedgif.o: animatedgif.c $(common_headers)
	$(compile) -c $(srcdir)/animatedgif.c

# Screenshot widget
screenshot.o: screenshot.c $(common_headers)
	$(compile) -c $(srcdir)/screenshot.c

# Progress bar widget
pbar.o: pbar.c $(common_headers)
	$(compile) -c $(srcdir)/pbar.c

# Screenshot/animation recording
animation.o: animation.c $(common_headers)
	$(compile) -c $(srcdir)/animation.c


# Shared/configuration files
files.o: files.c files.h
	$(compile) $(DEF_SHARE_DIR) -c $(srcdir)/files.c

# Custom icons
icons.o: icons.c icons.h
	$(compile) $(DEF_SHARE_DIR) -c $(srcdir)/icons.c

# Keybindings
keybindings.o: keybindings.c files.h $(common_headers)
	$(compile) -c $(srcdir)/keybindings.c

# Menu
menu.o: menu.c $(common_headers)
	$(compile) -c $(srcdir)/menu.c

# Link receive dialog
rcvmenu.o: rcvmenu.c $(common_headers)
	$(compile) -c $(srcdir)/rcvmenu.c

# Link send dialog
sendfile.o: sendfile.c emucore.h $(common_headers)
	$(compile) -c $(srcdir)/sendfile.c

# File open/save dialogs
filedlg.o: filedlg.c filedlg.h
	$(compile) -c $(srcdir)/filedlg.c

# Headless script parser (shared with headless build)
headless_script.o: $(top_srcdir)/headless/script.c $(top_srcdir)/headless/script.h
	$(compile) -c $(top_srcdir)/headless/script.c -o headless_script.o

trace.o: $(top_srcdir)/headless/trace.c $(top_srcdir)/headless/trace.h
	$(compile) -c $(top_srcdir)/headless/trace.c -o trace.o

# Fixed-width tree view
fixedtreeview.o: fixedtreeview.c fixedtreeview.h
	$(compile) -c $(srcdir)/fixedtreeview.c

# TI-81 program file functions
ti81prg.o: ti81prg.c ti81prg.h ../emu/tilem.h
	$(compile) -c $(srcdir)/ti81prg.c

# Character conversion
charmap.o: charmap.c charmap.h ../emu/tilem.h
	$(compile) -c $(srcdir)/charmap.c

# Windows resource file
tilem2rc.o: tilem2.rc
	major=`echo "2.0" | sed 's/\..*//'` ; \
	minor=`echo "2.0" | sed 's/.*\.//;s/[^0-9].*//'` ; \
	svnver=`svnversion "$(top_srcdir)" 2>/dev/null | sed 's/[^0-9].*//'` ; \
	[ -n "$$svnver" ] || svnver=0 ; \
	$(WINDRES) -DBUILD_VERSION=$$major,$$minor,0,$$svnver tilem2.rc tilem2rc.o

tilem2.rc: tilem2.rc.in $(top_builddir)/config.status
	cd $(top_builddir) && $(SHELL) ./config.status gui/tilem2.rc

install: tilem2
	$(INSTALL) -d -m 755 $(DESTDIR)$(bindir)
	$(INSTALL_PROGRAM) -m 755 tilem2 $(DESTDIR)$(bindir)

uninstall:
	rm -f $(DESTDIR)$(bindir)/tilem2

clean:
	rm -f *.o
	rm -f tilem2

Makefile: Makefile.in $(top_builddir)/config.status
	cd $(top_builddir) && $(SHELL) ./config.status

$(top_builddir)/config.status: $(top_srcdir)/configure
	cd $(top_builddir) && $(SHELL) ./config.status --recheck

.PRECIOUS: Makefile $(top_builddir)/config.status
.PHONY: all clean install uninstall
