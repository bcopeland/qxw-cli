## REL+

# Copyright 2011-2014 Mark Owen; Windows port by Peter Flippant
# http://www.quinapalus.com
# E-mail: qxw@quinapalus.com
# 
# This file is part of Qxw.
# 
# Qxw is free software: you can redistribute it and/or modify
# it under the terms of version 2 of the GNU General Public License
# as published by the Free Software Foundation.
# 
# Qxw is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
# 
# You should have received a copy of the GNU General Public License
# along with Qxw.  If not, see <http://www.gnu.org/licenses/> or
# write to the Free Software Foundation, Inc., 51 Franklin Street,
# Fifth Floor, Boston, MA  02110-1301, USA.


all:: qxw
deb:: qxw

CFLAGS := -Wall -fstack-protector --param=ssp-buffer-size=4 -Wformat -Wformat-security -Werror=format-security `pkg-config --cflags glib-2.0` `pkg-config --cflags gtk+-2.0` -I/opt/local/include
LFLAGS := -Wl,-Bsymbolic-functions -Wl,-z,relro -L/opt/local/lib -lgtk-x11-2.0 -lgdk-x11-2.0 -lm -lcairo -lgobject-2.0 -lpcre -lglib-2.0 -pthread -lgthread-2.0
# -lrt as well?
ifneq ($(filter deb,$(MAKECMDGOALS)),)
  CFLAGS:= $(CFLAGS) -g
else
  CFLAGS:= $(CFLAGS) -O9
endif

qxw: qxw.o filler.o dicts.o gui.o draw.o Makefile
	gcc -rdynamic -Wall -ldl qxw.o filler.o dicts.o gui.o draw.o $(LFLAGS) -o qxw

qxw.o: qxw.c qxw.h filler.h dicts.h draw.h gui.h common.h Makefile
	gcc $(CFLAGS) -c qxw.c -o qxw.o

gui.o: gui.c qxw.h filler.h dicts.h draw.h gui.h common.h Makefile
	gcc $(CFLAGS) -c gui.c -o gui.o

filler.o: filler.c qxw.h filler.h dicts.h common.h Makefile
	gcc $(CFLAGS) -c filler.c -o filler.o

dicts.o: dicts.c dicts.h gui.h common.h Makefile
	gcc $(CFLAGS) -fno-strict-aliasing -c dicts.c -o dicts.o

draw.o: draw.c qxw.h draw.h gui.h common.h Makefile
	gcc $(CFLAGS) -c draw.c -o draw.o

.PHONY: clean
clean:
	rm -f dicts.o draw.o filler.o gui.o qxw.o qxw

.PHONY: install
install:
	mkdir -p $(DESTDIR)/usr/games
	cp -a qxw $(DESTDIR)/usr/games/qxw
	mkdir -p $(DESTDIR)/usr/include/qxw
	cp -a qxwplugin.h $(DESTDIR)/usr/include/qxw/qxwplugin.h
	mkdir -p $(DESTDIR)/usr/share/applications
	cp -a qxw.desktop $(DESTDIR)/usr/share/applications/qxw.desktop
	mkdir -p $(DESTDIR)/usr/share/pixmaps
	cp -a qxw.xpm $(DESTDIR)/usr/share/pixmaps/qxw.xpm
	mkdir -p $(DESTDIR)/usr/share/icons/hicolor/48x48/apps
	cp -a icon-48x48.png $(DESTDIR)/usr/share/icons/hicolor/48x48/apps/qxw.png

## REL-
