/* $Id: qxw.h 517 2014-01-31 14:22:28Z mo $ */

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

extern int dir,curx,cury;
extern int unsaved;
extern int symmr,symmd,symmm;
extern int gtype,ndir[NGTYPE],gshape[NGTYPE];
extern int width,height;
extern char gtitle[SLEN];
extern char gauthor[SLEN];

extern int uhead,utail,uhwm;

extern int st_lc[MXLE+1];
extern int st_lucc[MXLE+1];
extern int st_locc[MXLE+1];
extern int st_lsc[MXLE+1];
extern int st_lmnc[MXLE+1];
extern int st_lmxc[MXLE+1];
extern int st_hist[NL+2];
extern int st_sc;
extern int st_ce;
extern int st_2u,st_3u;
extern int st_tlf,st_vltlf,st_tmtlf;

extern struct sprop dsp;
extern struct lprop dlp;

// functions called by grid filler
extern void updatefeas(void);
extern void updategrid(void);
extern void mkfeas(void);

// interface functions to gsq[][]
extern int getflags(int x,int y);
extern int getbgcol(int x,int y);
extern int getfgcol(int x,int y);
extern int getmkcol(int x,int y);
extern int getfstyle(int x,int y);
extern int getdech(int x,int y);
extern int getnumber(int x,int y);
extern void getmk(char*s,int x,int y,int c);

extern void a_load(void);
extern void a_save(void);
extern void a_importvls(char*fn);
extern void a_exportvls(char*fn);
extern void a_filenew(int flags);
extern void saveprefs(void);
extern void a_editblock (int k,int x,int y,int d);
extern void a_editcutout(int k,int x,int y,int d);
extern void a_editempty (int k,int x,int y,int d);
extern void a_editmerge (int k,int x,int y,int d);
extern void a_editbar   (int k,int x,int y,int d);
extern int symmrmask(void);
extern int symmmmask(void);
extern int symmdmask(void);
extern int cbits(ABM x);
extern int logbase2(ABM x);
extern void undo_push(void);
extern void undo_pop(void);
extern char abmtoechar(ABM b);
extern void abmtocs(char*s,ABM b,int inv,int dash);
extern void abmtostr(char*s,ABM b,int dash);
extern void abmstodispstr(char*s,ABM*b,int l);
extern void abmstostr(char*s,ABM*b,int l,int dash);
extern int strtoabms(ABM*p,int l,char*s,int dash);
extern int getlightd(int*lx,int*ly,int x,int y,int d);
extern int getlightbmp(ABM**p,int x,int y,int d);
extern int getlight(int*lx,int*ly,int x,int y,int d);
extern int isstartoflight(int x,int y,int d);
extern int issellight(int x,int y,int d);
extern void sellight(int x,int y,int d,int k);
extern int isclear(int x,int y);
extern int isbar(int x,int y,int d);
extern int ismerge(int x,int y,int d);
extern int isingrid(int x,int y);
extern int sqexists(int i,int j);
extern int clearbefore(int x,int y,int d);
extern int clearafter(int x,int y,int d);
extern int getword(int x,int y,int d,char*s);
extern int getmergegroupd(int*gx,int*gy,int x,int y,int d);
extern int getmergegroup(int*gx,int*gy,int x,int y);
extern int getmergedir(int x,int y);
extern void getmergerep(int*mx,int*my,int x,int y);
extern int isownmergerep(int x,int y);
extern void getmergerepd(int*mx,int*my,int x,int y,int d);
extern int compute(int mode);
extern void symmdo(void f(int,int,int,int),int k,int x,int y,int d);
extern char getechar(int x,int y);
extern int setechar(int x,int y,int d,char c);
extern void clrcont(int x,int y);
extern void stepback(int*x,int*y,int d);
extern void donumbers(void);
extern int stepbackifingrid (int*x,int*y,int d);
extern void stepforw (int*x,int*y,int d);
extern int stepforwmifingrid(int*x,int*y,int d);
extern char*titlebyauthor(void);
extern int preexport(void);
extern void postexport(void);
extern void make7bitclean(char*s);
extern int onebit(ABM x);
extern void resetlp(struct lprop*lp);
