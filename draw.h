// $Id: draw.h 517 2014-01-31 14:22:28Z mo $

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





#ifndef __DRAW_H__
#define __DRAW_H__

#include <cairo.h>
extern void moveto(cairo_t*cc,double x,double y);
extern void lineto(cairo_t*cc,double x,double y);
extern void rmoveto(cairo_t*cc,double x,double y);
extern void rlineto(cairo_t*cc,double x,double y);
extern void setlinewidth(cairo_t*cc,double w);
extern void setlinecap(cairo_t*cc,int c);
extern void closepath(cairo_t*cc);
extern void fill(cairo_t*cc);
extern void stroke(cairo_t*cc);
extern void strokepreserve(cairo_t*cc);
extern void clip(cairo_t*cc);
extern void gsave(cairo_t*cc);
extern void grestore(cairo_t*cc);
extern void setrgbcolor(cairo_t*cc,double r,double g, double b);
extern void setrgbacolor(cairo_t*cc,double r,double g, double b,double a);
extern void setfontstyle(cairo_t*cc,int fs);
extern void setfontsize(cairo_t*cc,double h);
extern void showtext(cairo_t*cc,char*s);

extern double textwidth(cairo_t*cc,char*s,double h);
extern void arc(cairo_t*cc,double x,double y,double r,double t0,double t1);
extern void arcn(cairo_t*cc,double x,double y,double r,double t0,double t1);
extern void setrgbcolor24(cairo_t*cc,int c);
extern void ltext(cairo_t*cc,char*s,double h,int fs);
extern void ctext(cairo_t*cc,char*s,double x,double y,double h,int fs,int ocm);

extern void ansform(char*t0,int t0l,int ln,int wlen,unsigned int dmask);

extern int bawdpx;
extern int hbawdpx;

extern void draw_init();
extern void draw_finit();
extern void repaint(cairo_t*cr);

extern void refreshsq(int x,int y);
extern void refreshsqlist(int l,int*gx,int*gy);
extern void refreshsqmg(int x,int y);
extern void refreshsel();
extern void refreshcur();
extern void refreshnum();
extern void refreshhin();
extern void refreshall();

extern int dawidth(void);
extern int daheight(void);

extern void a_exportg(char*fn,int f0,int f1);
extern void a_exportgh(int f,char*url);
extern void a_exporta(int f);
extern void a_exporthp(int f,int f0,int f1);

extern void edgecoords(double*x0,double*y0,double*x1,double*y1,int x,int y,int d);
extern void getsqbbox(double*x0,double*y0,double*x1,double*y1,int x,int y);
extern void getmgbbox(double*x0,double*y0,double*x1,double*y1,int x,int y);
extern void mgcentre(double*u,double*v,int x,int y,int d,int l);

extern char*dname[NGTYPE][MAXNDIR];

#endif
