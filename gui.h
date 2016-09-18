// $Id: gui.h 517 2014-01-31 14:22:28Z mo $

/*
Qxw is a program to help construct and publish crosswords.

Copyright 2011-2014 Mark Owen; Windows port by Peter Flippant
http://www.quinapalus.com
E-mail: qxw@quinapalus.com

This file is part of Qxw.

Qxw is free software: you can redistribute it and/or modify
it under the terms of version 2 of the GNU General Public License
as published by the Free Software Foundation.

Qxw is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Qxw.  If not, see <http://www.gnu.org/licenses/> or
write to the Free Software Foundation, Inc., 51 Franklin Street,
Fifth Floor, Boston, MA  02110-1301, USA.
*/





#ifndef __GUI_H__
#define __GUI_H__

#define PXEDGE .2  // size of edge of box for clicking bar
#define BAWD .139    // width of bar

extern int selmode;
extern int nsel;
extern int pxsq; // pixels per square on display

extern char filename[SLEN+50];
extern char filenamebase[SLEN];
extern int havesavefn;

extern void setfilenamebase(char*s);

extern void startgtk(void);
extern void stopgtk(void);

extern void invaldarect(int x0,int y0,int x1,int y1);
extern void invaldaall(void);

extern void stats_upd(void);
extern void syncgui(void);
extern void fserror(void);
extern void fsgerr(void);
extern void reperr(const char*s);

extern void killcurrdia(void);
extern void setposslabel(char*s);


#endif
