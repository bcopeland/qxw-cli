// $Id: qxwplugin.h 517 2014-01-31 14:22:28Z mo $

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


#include <stdio.h>
#include <ctype.h>
#include <string.h>

#define MXSZ 63 // maximum grid dimension
#define MXLE 250 // maximum entries in light

extern int treatedanswer(const char*light);
extern int isword(const char*light);

#ifdef _WIN32
  __declspec(dllexport) void init();
  __declspec(dllexport) void finit();
  __declspec(dllexport) int treat(const char*answer);

  __declspec(dllimport) int clueorderindex;
  __declspec(dllimport) int gridorderindex[];
  __declspec(dllimport) int checking[];
  __declspec(dllimport) int lightlength;
  __declspec(dllimport) int lightx;
  __declspec(dllimport) int lighty;
  __declspec(dllimport) int lightdir;
  __declspec(dllimport) char*treatmessage[];
  __declspec(dllimport) char*treatmessageAZ[];
  __declspec(dllimport) char*treatmessageAZ09[];
  __declspec(dllimport) char msgchar[];
  __declspec(dllimport) char msgcharAZ[];
  __declspec(dllimport) char msgcharAZ09[];
#else
  extern int clueorderindex;
  extern int*gridorderindex;
  extern int*checking;
  extern int lightlength;
  extern int lightx;
  extern int lighty;
  extern int lightdir;
  extern char*treatmessage[];
  extern char*treatmessageAZ[];
  extern char*treatmessageAZ09[];
  extern char msgchar[];
  extern char msgcharAZ[];
  extern char msgcharAZ09[];
#endif

static char light[MXLE+1];
