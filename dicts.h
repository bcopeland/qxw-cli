// $Id: dicts.h 518 2014-01-31 14:31:26Z mo $

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




#ifndef __DICTS_H__
#define __DICTS_H__

#define MAXNDICTS 9 // must fit in a word
extern char dfnames[MAXNDICTS][SLEN];
extern char dsfilters[MAXNDICTS][SLEN];
extern char dafilters[MAXNDICTS][SLEN];

extern char lemdesc[NLEM][LEMDESCLEN];
extern char*lemdescADVP[NLEM];

extern int atotal;                // total answers
extern int ltotal;                // total lights
extern int ultotal;               // total uniquified lights

extern int loaddicts(int sil);
extern void freedicts(void);
extern int loaddefdicts(void);
extern int getinitflist(int**l,int*ll,struct lprop*lp,int wlen);
extern int pregetinitflist(void);
extern int postgetinitflist(void);
extern char*loadtpi(void);
extern void unloadtpi(void);

#endif
