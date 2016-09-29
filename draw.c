// $Id: draw.c 519 2014-01-31 14:53:41Z mo $

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

#include <wchar.h>
#include <wctype.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <gtk/gtk.h>
#include <cairo.h>
#include <cairo-ps.h>
#include <cairo-svg.h>

#include "common.h"
#include "qxw.h"
#include "draw.h"
#include "dicts.h"

int bawdpx,hbawdpx;

static int sfcxoff=0,sfcyoff=0;


static double sdirdx[NGTYPE][MAXNDIR]={{1,0},{ 1.2,1.2,0  },{1.4,0.7,-0.7},{1,0},{1,0},{1,0},{1,0},{1,0},{1,0},{1,0}};
static double sdirdy[NGTYPE][MAXNDIR]={{0,1},{-0.7,0.7,1.4},{0  ,1.2, 1.2},{0,1},{0,1},{0,1},{0,1},{0,1},{0,1},{0,1}};
char*dname[NGTYPE][MAXNDIR]={{"Across","Down"},{"Northeast","Southeast","South"},{"East","Southeast","Southwest"},{"Ring","Radial"},{"Ring","Radial"},
  {"Across","Down"},{"Across","Down"},{"Across","Down"},{"Across","Down"},{"Across","Down"}};












// GERNERAL DRAWING

void moveto(cairo_t*cc,double x,double y)                          {cairo_move_to(cc,x,y);}
void lineto(cairo_t*cc,double x,double y)                          {cairo_line_to(cc,x,y);}
void rmoveto(cairo_t*cc,double x,double y)                         {cairo_rel_move_to(cc,x,y);}
void rlineto(cairo_t*cc,double x,double y)                         {cairo_rel_line_to(cc,x,y);}
void setlinewidth(cairo_t*cc,double w)                             {cairo_set_line_width(cc,w);}
void setlinecap(cairo_t*cc,int c)                                  {cairo_set_line_cap(cc,c);}
void closepath(cairo_t*cc)                                         {cairo_close_path(cc);}
void fill(cairo_t*cc)                                              {cairo_fill(cc);}
void stroke(cairo_t*cc)                                            {cairo_stroke(cc);}
void strokepreserve(cairo_t*cc)                                    {cairo_stroke_preserve(cc);}
void clip(cairo_t*cc)                                              {cairo_clip(cc);}
void gsave(cairo_t*cc)                                             {cairo_save(cc);}
void grestore(cairo_t*cc)                                          {cairo_restore(cc);}
void setrgbcolor(cairo_t*cc,double r,double g, double b)           {cairo_set_source_rgb(cc,r,g,b);}
void setrgbacolor(cairo_t*cc,double r,double g, double b,double a) {cairo_set_source_rgba(cc,r,g,b,a);}
void setfontstyle(cairo_t*cc,int fs)                               {cairo_select_font_face(cc,"sans",
                                                                      (fs&2)?CAIRO_FONT_SLANT_ITALIC:CAIRO_FONT_SLANT_NORMAL,
                                                                      (fs&1)?CAIRO_FONT_WEIGHT_BOLD:CAIRO_FONT_WEIGHT_NORMAL
                                                                      );}
void setfontsize(cairo_t*cc,double h)                              {cairo_set_font_size(cc,h);}
void showtext(cairo_t*cc,char*s)                                   {cairo_show_text(cc,s);}

double textwidth(cairo_t*cc,char*s,double h) {
  cairo_text_extents_t te;
  cairo_set_font_size(cc,h);
  cairo_text_extents(cc,s,&te);
  return te.x_advance;
  }

// Cairo's arc drawing does not seem to work well as part of a path, so we do our own
void arc(cairo_t*cc,double x,double y,double r,double t0,double t1) {double u;
  if(t1<t0) t1+=2*PI;
  if((t1-t0)*(t1-t0)*r>1.0/pxsq) { // error of approx 1/4 px?
    u=(t0+t1)/2;
    arc(cc,x,y,r,t0,u);
    arc(cc,x,y,r,   u,t1);
    }
  else lineto(cc,x+r*cos(t1),y+r*sin(t1));
  }

void arcn(cairo_t*cc,double x,double y,double r,double t0,double t1) {double u;
  if(t0<t1) t0+=2*PI;
  if((t1-t0)*(t1-t0)*r>1.0/pxsq) { // error of approx 1/4 px?
    u=(t0+t1)/2;
    arcn(cc,x,y,r,t0,u);
    arcn(cc,x,y,r,   u,t1);
    }
  else lineto(cc,x+r*cos(t1),y+r*sin(t1));
  }

void setrgbcolor24(cairo_t*cc,int c) {setrgbcolor(cc,((c>>16)&255)/255.0,((c>>8)&255)/255.0,(c&255)/255.0);}

void ltext(cairo_t*cc,char*s,double h,int fs) {setfontstyle(cc,fs); setfontsize(cc,h); showtext(cc,s);}

// Centered text. If ocm==0, print normally spaced; if ocm==1, print one character at a time, regularly spaced
void ctext(cairo_t*cc,char*s,double x,double y,double h,int fs,int ocm) {int i,m; char t[2]; double u;
  setfontstyle(cc,fs);
  setfontsize(cc,h);
  if(!ocm) {
    u=x-textwidth(cc,s,h)/2.0;
    moveto(cc,u,y);
    showtext(cc,s);
    return;
    }
  m=strlen(s);
  t[1]=0;
  for(i=0;i<m;i++) {
    t[0]=s[i];
    u=x-textwidth(cc,t,h)/2.0+(i+i+1-m)*h*.4;
    moveto(cc,u,y);
    showtext(cc,t);
    }
  }






// GRID GEOMETRY

static void vxcoords(double*x0,double*y0,double x,double y,int d) {double t;
  switch(gshape[gtype]) {
  case 0:
    *x0=x;
    *y0=y;
    switch(d) {
    case 3:                  break;
    case 0:*x0+=1.0;         break;
    case 1:*x0+=1.0;*y0+=1.0;break;
    case 2:         *y0+=1.0;break;
    default:assert(0);break;
      }
    break;
  case 1:
    *x0=x*1.2+.4;
    *y0=y*1.4+((int)floor(x)&1)*.7;
    switch(d) {
    case 5:                  break;
    case 0:*x0+=0.8;         break;
    case 1:*x0+=1.2;*y0+=0.7;break;
    case 2:*x0+=0.8;*y0+=1.4;break;
    case 3:         *y0+=1.4;break;
    case 4:*x0-=0.4;*y0+=0.7;break;
    default:assert(0);break;
      }
    break;
  case 2:
    *y0=y*1.2+.4;
    *x0=x*1.4+((int)floor(y)&1)*.7;
    switch(d) {
    case 4:                  break;
    case 3:*y0+=0.8;         break;
    case 2:*y0+=1.2;*x0+=0.7;break;
    case 1:*y0+=0.8;*x0+=1.4;break;
    case 0:         *x0+=1.4;break;
    case 5:*y0-=0.4;*x0+=0.7;break;
    default:assert(0);break;
      }
    break;
  case 3:case 4:
    switch(d) {
    case 3:              break;
    case 0:x+=1.0;       break;
    case 1:x+=1.0;y+=1.0;break;
    case 2:       y+=1.0;break;
    }
    t=2*PI*((gshape[gtype]==4)?x-0.5:x)/width;
    *x0= (height-y)*sin(t)+height;
    *y0=-(height-y)*cos(t)+height;
    break;
    }
  }

void edgecoords(double*x0,double*y0,double*x1,double*y1,int x,int y,int d) {
  vxcoords(x0,y0,(double)x,(double)y,d);
  vxcoords(x1,y1,(double)x,(double)y,(d+1)%(ndir[gtype]*2));
  }

// find centre coordinates for merge group containing x,y
void mgcentre(double*u,double*v,int x,int y,int d,int l) {
  double x0=0,y0=0,x1=0,y1=0;
  if(gshape[gtype]==3||gshape[gtype]==4) {
    if(EVEN(d)) vxcoords(u,v,x+l/2.0,y+0.5,3);
    else        vxcoords(u,v,x+0.5,y+l/2.0,3);
    return;
    }
  if(gtype>4) {
  // we are on a square-format grid but there is potential for wrap-around
    while(l>2) stepforw(&x,&y,d),l-=2;
    if((d==0&&x==width -1)||(d==1&&y==height-1)) l=1;
    }
  vxcoords(&x0,&y0,(double)x,(double)y,0);
  vxcoords(&x1,&y1,(double)x,(double)y,ndir[gtype]);
  *u=(x0+x1)/2+sdirdx[gtype][d]*(l-1)/2.0;
  *v=(y0+y1)/2+sdirdy[gtype][d]*(l-1)/2.0;
  }

static void addvxcoordsbbox(double*x0,double*y0,double*x1,double*y1,double x,double y,int d) {double vx=0,vy=0;
  vxcoords(&vx,&vy,x,y,d);
  if(vx<*x0) *x0=vx;
  if(vx>*x1) *x1=vx;
  if(vy<*y0) *y0=vy;
  if(vy>*y1) *y1=vy;
  }

static void addsqbbox(double*x0,double*y0,double*x1,double*y1,int x,int y) {int d; double vx=0,vy=0;
  mgcentre(&vx,&vy,x,y,0,1);
  if(vx-0.5<*x0) *x0=vx-0.5; // include a 1x1 square centred in the middle of the merge group for the letter
  if(vx+0.5>*x1) *x1=vx+0.5;
  if(vy-0.5<*y0) *y0=vy-0.5;
  if(vy+0.5>*y1) *y1=vy+0.5;
  for(d=0;d<ndir[gtype]*2;d++) addvxcoordsbbox(x0,y0,x1,y1,(double)x,(double)y,d);
  if(gshape[gtype]!=3&&gshape[gtype]!=4) return; // done if grid is not circular
  // edge 3 is convex; one of the following points may be tangent to a vertical
  // or horizontal
  addvxcoordsbbox(x0,y0,x1,y1,x+0.25,(double)y,3);
  addvxcoordsbbox(x0,y0,x1,y1,x+0.5 ,(double)y,3);
  addvxcoordsbbox(x0,y0,x1,y1,x+0.75,(double)y,3);
  }

void getsqbbox(double*x0,double*y0,double*x1,double*y1,int x,int y) {
  *x0=1e9; *x1=-1e9;
  *y0=1e9; *y1=-1e9;
  addsqbbox(x0,y0,x1,y1,x,y);
  }

void getmgbbox(double*x0,double*y0,double*x1,double*y1,int x,int y) {int i,l,mx[MXCL],my[MXCL];
  *x0=1e9; *x1=-1e9;
  *y0=1e9; *y1=-1e9;
  l=getmergegroup(mx,my,x,y);
  for(i=0;i<l;i++) addsqbbox(x0,y0,x1,y1,mx[i],my[i]);
  }


static void getdxdy(double*c,double*s,int x,int y,int d) {double t,u,v;
  switch(gshape[gtype]) {
  case 0:case 1:case 2:
    u=sdirdx[gtype][d];
    v=sdirdy[gtype][d];
    t=sqrt(u*u+v*v);
    *c=u/t;*s=v/t;
    break;
  case 3:case 4:
    t=2*PI*((gshape[gtype]==4)?x:x+0.5)/width;
    if(d==0) {*c= cos(t); *s=sin(t);}
    else     {*c=-sin(t); *s=cos(t);}
    break;
    }
  }

// get suitable scale factor (so that chars fit in small cells) for group
// in direction d of length l
static double getscale(int x,int y,int d,int l) {double s;
  if(gshape[gtype]<3) return 1;
  s=(height-y-.3)*2*PI/width;
  if(EVEN(d)) s*=l;
  if(s>1) s=1;
  return s;
  }


static void movetoorig(cairo_t*cc,int x,int y) {double x0=0,y0=0;
  switch(gshape[gtype]) {
  case 0: vxcoords(&x0,&y0,(double)x,(double)y,3);break;
  case 1: vxcoords(&x0,&y0,(double)x,(double)y,5);break;
  case 2: vxcoords(&x0,&y0,(double)x,(double)y,4);break;
  case 3:case 4:
    vxcoords(&x0,&y0,(double)x,(double)y,1);
    break;
    }
  moveto(cc,x0,y0);
  }

static void movetomgcentre(cairo_t*cc,int x,int y,int d,int l) {double u=0,v=0;
  mgcentre(&u,&v,x,y,d,l);
  moveto(cc,u,v);
  }


// draw path for edges with bits set in mask b
static void edges(cairo_t*cc,int x,int y,int b) {int f,g,i,j,n,o;double x0=0,y0=0,x1=0,y1=0,t;
  n=ndir[gtype]*2;
  b&=(1<<n)-1;
  if(b==0) return;
  for(o=0;o<n;o++) if(((b>>o)&1)==0) break; // find a place to start drawing
  f=0;g=0;
  for(i=0;i<ndir[gtype]*2;i++) {
    j=(o+i)%n;
    if((b>>j)&1) {
      edgecoords(&x0,&y0,&x1,&y1,x,y,j);
      if(f==0) moveto(cc,x0,y0);
      if((gshape[gtype]==3||gshape[gtype]==4)) {
        if(j&1) {
          t=2*PI*((gshape[gtype]==4)?x-0.5:x)/width-PI/2;
          if(j==1) arcn(cc,(double)height,(double)height,height-y-.999,  t+2*PI/width,t);
          else     arc (cc,(double)height,(double)height,height-y     ,t,t+2*PI/width);
          }
        else lineto(cc,x1,y1);
        }
      else {
      // the following is to defeat this bug:
      // https://bugs.freedesktop.org/show_bug.cgi?id=39551
        lineto(cc,(x0+x1)/2,(y0+y1)/2);
        moveto(cc,(x0+x1)/2,(y0+y1)/2);
        lineto(cc,x1,y1);
        }
      f=1;
      }
    else f=0,g=1;
    }
  if(g==0) closepath(cc);
  }

// draw path for outline of a `square'
static void sqpath(cairo_t*cc,int x,int y) {int i;double x0=0,y0=0,t;
  vxcoords(&x0,&y0,(double)x,(double)y,0);
  moveto(cc,x0,y0);
  switch(gshape[gtype]) {
  case 0: case 1: case 2:
    for(i=1;i<ndir[gtype]*2;i++) {
      vxcoords(&x0,&y0,(double)x,(double)y,i);
      lineto(cc,x0,y0);
      }
    break;
  case 3: case 4:
    vxcoords(&x0,&y0,(double)x,(double)y,1);
    lineto(cc,x0,y0);
    t=2*PI*((gshape[gtype]==4)?x-0.5:x)/width-PI/2;
    arcn(cc,(double)height,(double)height,height-y-.999,t+2*PI/width,t);
    vxcoords(&x0,&y0,(double)x,(double)y,3);
    lineto(cc,x0,y0);
    arc (cc,(double)height,(double)height,height-y     ,t,t+2*PI/width);
    break;
    }
  closepath(cc);
  }

static void rtop(double*r,double*t,double x,double y) {
  x-=height;y=height-y;
  *r=sqrt(x*x+y*y);
  if(*r<1e-3) *t=0;
  else *t=atan2(x,y);
  if(*t<0) *t+=2*PI;
  }

static void ptor(double*x,double*y,double r,double t) {
  *x=r*sin(t)+height;
  *y=height-r*cos(t);
  }

// move to suitable position to plot number (or other marker text) of width w, height h
// in corner number c
static void movetonum(cairo_t*cc,int x,int y,double w,double h,int c) {double x0=0,y0=0,r0,t0; int d;
  switch(gshape[gtype]) {
  case 0:
    movetoorig(cc,x,y);
    switch(c) {
    case 0: rmoveto(cc,.03  ,    h*1.25); break;
    case 1: rmoveto(cc,.97-w,    h*1.25); break;
    case 2: rmoveto(cc,.97-w,.97-h*.1  ); break;
    case 3: rmoveto(cc,.03  ,.97-h*.1  ); break;
      }
    break;
  case 1:
    movetoorig(cc,x,y);
    switch(c) {
    case 0: rmoveto(cc,.00        ,h*1.25   ); break;
    case 1: rmoveto(cc,.80-w      ,h*1.25   ); break;
    case 2: rmoveto(cc,1.20-w-h*.5,.7+h*.5  ); break;
    case 3: rmoveto(cc,.80-w      ,1.37-h*.1); break;
    case 4: rmoveto(cc,.00        ,1.37-h*.1); break;
    case 5: rmoveto(cc,-.40+h*.5  ,.7+h*.5  ); break;
      }
    break;
  case 2:
    movetoorig(cc,x,y);
    switch(c) {
    case 0: rmoveto(cc,.03    ,h*1.25         ); break;
    case 1: rmoveto(cc,.7-w*.5,h*1.25-.45+w*.5); break;
    case 2: rmoveto(cc,1.37-w ,h*1.25         ); break;
    case 3: rmoveto(cc,1.37-w ,0.8            ); break;
    case 4: rmoveto(cc,.7-w*.5,1.25-w*.5      ); break;
    case 5: rmoveto(cc,.03    ,0.8            ); break;
      }
    break;
  case 3:case 4:
    vxcoords(&x0,&y0,(double)x,(double)y,(c+3)%4);
    rtop(&r0,&t0,x0,y0);
    switch(c) {
    case 0: r0-=.05;t0+=2*PI*0.015/r0; break;
    case 1: r0-=.05;t0-=2*PI*0.015/r0; break;
    case 2: r0+=.05;t0-=2*PI*0.015/r0; break;
    case 3: r0+=.05;t0+=2*PI*0.015/r0; break;
      }
    if     (t0<=  PI/2) d=0;
    else if(t0<=  PI)   d=1;
    else if(t0<=3*PI/2) d=2;
    else                d=3;
    h*=1.25;
    w+=0.03;
    switch((4-c+d)%4) {
    case 0: r0-=w*sin(t0); break;
    case 1: r0+=h*cos(t0); break;
    case 2: r0+=w*sin(t0); break;
    case 3: r0-=h*cos(t0); break;
      }
    ptor(&x0,&y0,r0,t0);
    if(c==0||c==3) d^=2;
    if(d==0||d==3) x0-=w;
    if(d==2||d==3) y0+=h;
    moveto(cc,x0,y0);
    break;
    }
  }





/*
Surfaces that make up main grid drawing area:

  sfb       : background
  sfc       : cursor
  sfs       : selected cells/lights
  sfh       : hint letters, hotspots
  sfn       : numbers
  sfq[x][y] : one for each square, containing entry letter, number, edges/bars, block, cutout
*/

static cairo_surface_t*sfb=0,*sfc=0,*sfs=0,*sfh=0,*sfn=0,*sfq[MXSZ][MXSZ]={{0}};
static int sfqx0[MXSZ][MXSZ],sfqy0[MXSZ][MXSZ],sfqx1[MXSZ][MXSZ],sfqy1[MXSZ][MXSZ]; // bounding boxes of squares

void draw_finit() {int x,y;
  if(sfb) cairo_surface_destroy(sfb);  sfb=0;
  if(sfc) cairo_surface_destroy(sfc);  sfc=0;
  if(sfs) cairo_surface_destroy(sfs);  sfs=0;
  if(sfn) cairo_surface_destroy(sfn);  sfn=0;
  if(sfh) cairo_surface_destroy(sfh);  sfh=0;
  for(x=0;x<MXSZ;x++) for(y=0;y<MXSZ;y++) {
    if(sfq[x][y]) cairo_surface_destroy(sfq[x][y]);  sfq[x][y]=0;
    }
  }

// call this at init and whenever
// - grid props change
// - zoom changes
void draw_init() {
  int x,y;
  double x0,y0,x1,y1;
  int sx0=0,sy0=0,sx1=0,sy1=0;
  int gx0=0,gy0=0,gx1=0,gy1=0;

  draw_finit(); // make sure everything is freed
  hbawdpx=((int)floor(pxsq*BAWD/2.0+0.5));
  if(hbawdpx<2) hbawdpx=2;
  bawdpx=hbawdpx*2-1;
  DEB4 printf("bawdpx=%d hbawdpx=%d\n",bawdpx,hbawdpx);
  for(x=0;x<MXSZ;x++) for(y=0;y<MXSZ;y++) if(isingrid(x,y)) {
    getsqbbox(&x0,&y0,&x1,&y1,x,y);
    sx0=(int)floor(x0*pxsq-hbawdpx);
    sy0=(int)floor(y0*pxsq-hbawdpx);
    sx1=(int) ceil(x1*pxsq+hbawdpx);
    sy1=(int) ceil(y1*pxsq+hbawdpx);
    sfqx0[x][y]=sx0;
    sfqy0[x][y]=sy0;
    sfqx1[x][y]=sx1;
    sfqy1[x][y]=sy1;
    if(sx0<gx0) gx0=sx0;
    if(sy0<gy0) gy0=sy0;
    if(sx1>gx1) gx1=sx1;
    if(sy1>gy1) gy1=sy1;
    sfq[x][y]=cairo_image_surface_create(CAIRO_FORMAT_ARGB32,sx1-sx0,sy1-sy0);
    }
  DEB4 printf("Overall bbox = (%d,%d)-(%d,%d)\n",gx0,gy0,gx1,gy1);
  sfb=cairo_image_surface_create(CAIRO_FORMAT_ARGB32,gx1,gy1); // take top left as (0,0)
  sfc=cairo_image_surface_create(CAIRO_FORMAT_ARGB32,pxsq,pxsq);
  sfs=cairo_image_surface_create(CAIRO_FORMAT_ARGB32,gx1+hbawdpx-1,gy1+hbawdpx-1);
  sfh=cairo_image_surface_create(CAIRO_FORMAT_ARGB32,gx1,gy1);
  sfn=cairo_image_surface_create(CAIRO_FORMAT_ARGB32,gx1,gy1);
  }

// Refresh drawing area: called here with clip already set on cr, so we can
// just repaint everything.
void repaint(cairo_t*cc) {int i,j;
  assert(sfb);
  cairo_set_source_surface(cc,sfb,bawdpx,bawdpx); // paint background
  cairo_paint(cc);
  cairo_set_source_surface(cc,sfc,sfcxoff,sfcyoff); // paint cursor
  cairo_paint(cc);
  cairo_set_source_surface(cc,sfh,bawdpx,bawdpx); // paint hints
  cairo_paint(cc);
  for(j=0;j<MXSZ;j++) for(i=0;i<MXSZ;i++) if(isingrid(i,j)) { // paint each square
    assert(sfq[i][j]);
    cairo_set_source_surface(cc,sfq[i][j],sfqx0[i][j]+bawdpx,sfqy0[i][j]+bawdpx);
    cairo_paint(cc);
    }
  if(shownums) cairo_set_source_surface(cc,sfn,bawdpx,bawdpx); // paint numbers
  cairo_paint(cc);
  cairo_set_source_surface(cc,sfs,hbawdpx,hbawdpx); // paint selection
  cairo_paint(cc);
  }


static unsigned int hpat[]={ // hatch pattern
  0x808080, 0xcccccc, 0xffffff, 0xffffff, 0xffffff, 0xcccccc, 0x808080, 0x808080,
  0xcccccc, 0xffffff, 0xffffff, 0xffffff, 0xcccccc, 0x808080, 0x808080, 0x808080, 
  0xffffff, 0xffffff, 0xffffff, 0xcccccc, 0x808080, 0x808080, 0x808080, 0xcccccc, 
  0xffffff, 0xffffff, 0xcccccc, 0x808080, 0x808080, 0x808080, 0xcccccc, 0xffffff, 
  0xffffff, 0xcccccc, 0x808080, 0x808080, 0x808080, 0xcccccc, 0xffffff, 0xffffff, 
  0xcccccc, 0x808080, 0x808080, 0x808080, 0xcccccc, 0xffffff, 0xffffff, 0xffffff, 
  0x808080, 0x808080, 0x808080, 0xcccccc, 0xffffff, 0xffffff, 0xffffff, 0xcccccc, 
  0x808080, 0x808080, 0xcccccc, 0xffffff, 0xffffff, 0xffffff, 0xcccccc, 0x808080}; 

// ex=1 (export mode): don't stroke around edge, don't shade cutouts
static void drawsqbg(cairo_t*cc,int x,int y,int sq,int ex) {int f,bg;
  cairo_surface_t*cs;
  cairo_pattern_t*cp;
  cairo_matrix_t cm;

  f=getflags(x,y);
  bg=getbgcol(x,y);
  if(f&8) { // hatching for cutout
    if(ex) return;
    gsave(cc);
    cs=cairo_image_surface_create_for_data((unsigned char*)hpat,CAIRO_FORMAT_RGB24,8,8,32);
    cp=cairo_pattern_create_for_surface(cs);
    cairo_matrix_init_scale(&cm,sq,sq);
    cairo_pattern_set_matrix(cp,&cm);
    cairo_set_source(cc,cp);
    cairo_pattern_set_extend(cairo_get_source(cc),CAIRO_EXTEND_REPEAT);
    sqpath(cc,x,y);
    fill(cc);
    cairo_pattern_destroy(cp);
    cairo_surface_destroy(cs);
    grestore(cc);
  } else {
    sqpath(cc,x,y);
    if(f&1) setrgbcolor(cc,0,0,0); // solid block? then black
    else setrgbcolor24(cc,bg); // otherwise bg
    setlinewidth(cc,1.0/sq);
    if(!ex) strokepreserve(cc);
    fill(cc);
    }
  }

// Draw cell contents. ocm ("one character at a time mode") flag passed on to ctext.
static void drawct(cairo_t*cc,int xr,int yr,struct square*sq,int ocm,int dots,double textscale) {
  int i,j,l,md,de,nd;
  double h,sc,u,v;
  char s[MXCT*MAXNDIR+1];

  l=getmergegroup(0,0,xr,yr);
  md=getmergedir(xr,yr);
  if(md<0) md=0;
  mgcentre(&u,&v,xr,yr,md,l);
  sc=getscale(xr,yr,0,l)*textscale;
  de=getdech(xr,yr);
  if(de==0) nd=1,de=1; else nd=ndir[gtype];
  if(de==2) { // vertical display
    l=1;
    for(i=0;i<nd;i++) if(sq->ctlen[i]>l) l=sq->ctlen[i];
    h=0.7/nd;
    if(h>1.0/l) h=1.0/l;
    for(i=0;i<nd;i++) {
      abmstodispstr(s,sq->ctbm[i],sq->ctlen[i]);
      if(dots) for(j=0;s[j];j++) if(s[j]==' ') s[j]='.';
      ctext(cc,s,u,v+((i-(nd-1.0)/2.0)*h+0.42*h)*sc,h*sc,getfstyle(xr,yr),ocm);
      }
  } else {
    s[0]=0;
    for(i=0;i<nd;i++) abmstodispstr(s+strlen(s),sq->ctbm[i],sq->ctlen[i]);
    l=strlen(s); if(l<1) l=1;
    if(dots&&l>1) for(i=0;i<l;i++) if(s[i]==' ') s[i]='.';
    h=0.7;
    if(l>1) h=1.0/l;
    ctext(cc,s,u,v+0.42*h*sc,h*sc,getfstyle(xr,yr),ocm);
    }
  }

// draw bars and edges 
// lf=1 draw entered letter also
// ocm flag passed on to ctext
static void drawsqfg(cairo_t*cc,int x,int y,int sq,int ba,int lf,int ocm,double textscale) {
  int b,d,f,m,xr,yr;

  // edges and bars
  b=0; for(d=0;d<ndir[gtype]*2;d++) if(isbar  (x,y,d)) b|=1<<d;
  m=0; for(d=0;d<ndir[gtype]*2;d++) if(ismerge(x,y,d)) m|=1<<d;
  setrgbcolor(cc,0,0,0);
  if(sqexists(x,y)) {
    setlinewidth(cc,1.0/pxsq);
    edges(cc,x,y,~m);
    stroke(cc);
    }
  setlinewidth(cc,ba/(double)sq);
  edges(cc,x,y,b);
  stroke(cc);
  // entered letter
  if(!lf) return;
  getmergerep(&xr,&yr,x,y);
  f=getflags(xr,yr);
  if((f&9)!=0) return;
  setrgbcolor24(cc,getfgcol(xr,yr));
  drawct(cc,xr,yr,&gsq[xr][yr],ocm,0,textscale);
  }

// Draw one square into sfq[x][y].
void refreshsq(int x,int y) {cairo_t*cc;
  DEB4 printf("refreshsq(%d,%d)\n",x,y);
  assert(sfq[x][y]);
  // background
  cc=cairo_create(sfb);
  cairo_translate(cc,0.5,0.5);
  cairo_scale(cc,pxsq,pxsq);
  drawsqbg(cc,x,y,pxsq,0);
  cairo_destroy(cc);
  // foreground
  cc=cairo_create(sfq[x][y]);
  setrgbacolor(cc,0.0,0.0,0.0,0.0);
//DEB4 setrgbacolor(cc,drand48(),drand48(),drand48(),0.5);
  cairo_set_operator(cc,CAIRO_OPERATOR_SOURCE);
  cairo_paint(cc);
  cairo_set_operator(cc,CAIRO_OPERATOR_OVER);
  cairo_translate(cc,-sfqx0[x][y]+0.5,-sfqy0[x][y]+0.5);
  cairo_scale(cc,pxsq,pxsq);
  drawsqfg(cc,x,y,pxsq,bawdpx,1,1,1.0);
  cairo_destroy(cc);
  invaldarect(sfqx0[x][y]+bawdpx,sfqy0[x][y]+bawdpx,sfqx1[x][y]+bawdpx,sfqy1[x][y]+bawdpx);
  }

void refreshsqlist(int l,int*gx,int*gy) {int i;
  DEB4 printf("refreshsqlist([%d])\n",l);
  for(i=0;i<l;i++) refreshsq(gx[i],gy[i]);
  }

void refreshsqmg(int x,int y) {int l,gx[MXCL],gy[MXCL];
  DEB4 printf("refreshsqmg(%d,%d)\n",x,y);
  if(!isingrid(x,y)) return;
  l=getmergegroup(gx,gy,x,y);
  refreshsqlist(l,gx,gy);
  }

static void refreshsel0(cairo_t*cc) {int i,j; // fill selected cells
  for(j=0;j<MXSZ;j++) for(i=0;i<MXSZ;i++) if(isingrid(i,j)&&(getflags(i,j)&0x10)) {
    DEB4 printf("sel fill %d,%d\n",i,j);
    sqpath(cc,i,j);
    fill(cc);
    }
  }

static void refreshsel1(cairo_t*cc) { // refresh selected lights
  int d,i,j,k,l,lx[MXCL],ly[MXCL],tl;
  double u,v,u0,v0;

  setlinecap(cc,1);
  setlinewidth(cc,.5);
  for(j=0;j<height;j++) for(i=0;i<width;i++) for(d=0;d<ndir[gtype];d++)
    if(isstartoflight(i,j,d)&&issellight(i,j,d)) {
      l=getlightd(lx,ly,i,j,d);
      assert(l>=2);
      tl=getmergegroupd(0,0,lx[0],ly[0],d);
      movetomgcentre(cc,lx[0],ly[0],d,tl);
      mgcentre(&u,&v,lx[0],ly[0],d,tl);
      for(k=1;k<l;k++) {
        u0=u; v0=v;
        tl=getmergegroupd(0,0,lx[k],ly[k],d);
        mgcentre(&u,&v,lx[k],ly[k],d,tl);
        if(gtype>=5) { // potential for wrap-around?
          if(d==0&&(lx[k]<lx[k-1]||ly[k]!=ly[k-1])) { lineto(cc,width+0.5,v0);  moveto(cc,-0.5,v); }
          if(d==1&&(ly[k]<ly[k-1]||lx[k]!=lx[k-1])) { lineto(cc,u0,height+0.5); moveto(cc,u,-0.5); }
          }
        DEB4 printf("%f,%f (%f) - %f,%f (%f)\n",u0,v0,atan2(v0,u0),u,v,atan2(v,u));
        if((gshape[gtype]==3||gshape[gtype]==4)&&d==0) {
          arc(cc,(double)height,(double)height,height-ly[k]-0.5,atan2(v0-height,u0-height),atan2(v-height,u-height));
          } else lineto(cc,u,v);
        }
      stroke(cc);
      }
  setlinecap(cc,0);
  }

static void refreshsel2(cairo_t*cc) { // refresh virtual lights, including current one if dir>=100
  int i,j,l,md,nc,pd[MXSZ][MXSZ],vis[MXSZ][MXSZ],x,y,x0,y0;
  double sc[MXCL],u,v,xc[MXCL],yc[MXCL];

  setlinecap(cc,1);
  memset(vis,0,sizeof(vis));
  for(i=0;i<nvl;i++) for(j=0;j<vls[i].l;j++) vis[vls[i].x[j]][vls[i].y[j]]++; // count visits: allocation does not depend on which are selected
  for(i=0;i<MXSZ;i++) for(j=0;j<MXSZ;j++) pd[i][j]=(int)ceil(sqrt(vis[i][j])); // packing density in each square
  memset(vis,0,sizeof(vis));
  for(i=0;i<nvl;i++) {
    nc=vls[i].l;
    for(j=0;j<nc;j++) {
      x0=vls[i].x[j];
      y0=vls[i].y[j];
      if(vls[i].sel||i+100==dir) {
        getmergerep(&x,&y,x0,y0);
        l=getmergegroup(0,0,x,y);
        md=getmergedir(x,y);
        if(md<0) md=0;
        mgcentre(&u,&v,x,y,md,l);
        sc[j]=getscale(x,y,0,1);
        xc[j]=u+(((vis[x0][y0]%pd[x0][y0])*2+1)/(pd[x0][y0]*2.0)-0.5)*sc[j];
        yc[j]=v+(((vis[x0][y0]/pd[x0][y0])*2+1)/(pd[x0][y0]*2.0)-0.5)*sc[j];
        }
      vis[x0][y0]++;
      }
    if(vls[i].sel||i+100==dir) { // is selected or is current?
      if     (vls[i].sel&&i+100!=dir) setrgbacolor(cc,1.0,0.8,0.0,0.5); // just selected
      else if(vls[i].sel&&i+100==dir) setrgbacolor(cc,0.8,0.7,0.3,0.5); // selected and current
      else                            setrgbacolor(cc,0.6,0.6,0.6,0.5); // just current
      for(j=0;j<nc;j++) { // draw blobs
        DEB1 printf("refreshsel2: %d,%5.2f %5.2f %5.3f\n",j,xc[j],yc[j],sc[j]);
        if(j==0) setlinecap(cc,2); // first one square
        if(j==1) setlinecap(cc,1); // remainder circular (like IC pads)
        if(j==0||sc[j]!=sc[j-1]) setlinewidth(cc,sc[j]/4);
        moveto(cc,xc[j]-0.001,yc[j]);
        rlineto(cc,0.001,0);
        stroke(cc);
        }
      setlinewidth(cc,0.05); // joining lines
      moveto(cc,xc[0],yc[0]);
      for(j=1;j<nc;j++) lineto(cc,xc[j],yc[j]);
      stroke(cc);
      }
    }
  setlinecap(cc,0);
  }

void refreshsel() {cairo_t*cc;
  cc=cairo_create(sfs);
  setrgbacolor(cc,0.0,0.0,0.0,0.0);
  cairo_set_operator(cc,CAIRO_OPERATOR_SOURCE);
  cairo_paint(cc);
  cairo_set_operator(cc,CAIRO_OPERATOR_OVER);
  cairo_translate(cc,hbawdpx-0.5,hbawdpx-0.5);
  cairo_scale(cc,pxsq,pxsq);
  setrgbacolor(cc,1.0,0.8,0.0,0.5);
  if(selmode==0) refreshsel0(cc);
  if(selmode==1) refreshsel1(cc);
  if(selmode==2||dir>=ndir[gtype]) refreshsel2(cc);
  cairo_destroy(cc);
  invaldaall();
  }

void refreshcur() {
  double sc,c=0,s=0,u,v;
  int x,y;
  static int ocx=0,ocy=0;
  cairo_t*cc;

  cc=cairo_create(sfc);
  setrgbacolor(cc,0.0,0.0,0.0,0.0);
  cairo_set_operator(cc,CAIRO_OPERATOR_SOURCE);
  cairo_paint(cc);
  cairo_set_operator(cc,CAIRO_OPERATOR_OVER);
  sc=getscale(curx,cury,0,1)*pxsq;
  setrgbacolor(cc,0.6,0.6,0.6,0.8);
  setlinewidth(cc,2);
  if(dir<ndir[gtype]) {
    getdxdy(&c,&s,curx,cury,dir);c*=sc;s*=sc;
    moveto(cc,pxsq/2.0,pxsq/2.0);
    rmoveto(cc,c* .4      ,      s* .4);
    rlineto(cc,c*-.8+s*-.3,c* .3+s*-.8);
    rlineto(cc,      s* .6,c*-.6      );
    closepath(cc);
    fill(cc);
    }
  else {
//    moveto(cc,pxsq/2+sc/4,pxsq/2); // circle in VL mode
//    arc(cc,pxsq/2,pxsq/2,sc/4,0,2*PI);
//    closepath(cc);
//    fill(cc);
    }
  mgcentre(&u,&v,curx,cury,0,1);
  x=(int)floor(u*pxsq+0.5); y=(int)floor(v*pxsq+0.5);
  sfcxoff=x-pxsq/2+bawdpx;
  sfcyoff=y-pxsq/2+bawdpx;
  invaldarect(sfcxoff,sfcyoff,sfcxoff+pxsq,sfcyoff+pxsq);
  mgcentre(&u,&v,ocx,ocy,0,1);
  x=(int)floor(u*pxsq+0.5); y=(int)floor(v*pxsq+0.5);
  invaldarect(x-pxsq/2+bawdpx,y-pxsq/2+bawdpx,x+pxsq/2+bawdpx,y+pxsq/2+bawdpx);
  ocx=curx;ocy=cury;
  cairo_destroy(cc);
  }

static void drawnums(cairo_t*cc) {int c,i,x,y,n; double sc; char s[MXMK+1];
  for(y=0;y<MXSZ;y++) for(x=0;x<MXSZ;x++) if(isingrid(x,y)) {
    n=getnumber(x,y);
    sc=getscale(x,y,0,1);
    for(c=0;c<ndir[gtype]*2;c++) {
      getmk(s,x,y,c);
      if(!strcmp(s,"")) continue;
      if(!strcmp(s,"\\c")) {
        i=getechar(x,y);
        if(i==0||i==' ') strcpy(s,"-");
        else sprintf(s,"%d",cwperm[chartol[i]]+1);
        }
      else if(!strcmp(s,"\\#")) {
        if(n<=0) continue;
        sprintf(s,"%d",n);
        }
      movetonum(cc,x,y,textwidth(cc,s,.25*sc)+.1*sc,.27*sc,c);
      rmoveto(cc,.05*sc,-.05*sc);
      setrgbcolor24(cc,getmkcol(x,y));
      ltext(cc,s,.25*sc,0);
      }
    }
  }

void refreshnum() {cairo_t*cc;
  cc=cairo_create(sfn);
  setrgbacolor(cc,0.0,0.0,0.0,0.0);
  cairo_set_operator(cc,CAIRO_OPERATOR_SOURCE);
  cairo_paint(cc);
  cairo_set_operator(cc,CAIRO_OPERATOR_OVER);
  cairo_translate(cc,0.5,0.5);
  cairo_scale(cc,pxsq,pxsq);
  drawnums(cc);
  cairo_destroy(cc);
  invaldaall();
  }

void refreshhin() {
  int de,nd,f,i,j,k,l,md,x,y;
  struct entry*e;
  struct square sq;
  ABM m;
  char c;
  char s[SLEN];
  double sc,u,v;
  cairo_t*cc;

  cc=cairo_create(sfh);
  setrgbacolor(cc,0.0,0.0,0.0,0.0);
  cairo_set_operator(cc,CAIRO_OPERATOR_SOURCE);
  cairo_paint(cc);
  cairo_set_operator(cc,CAIRO_OPERATOR_OVER);
  cairo_translate(cc,0.5,0.5);
  cairo_scale(cc,pxsq,pxsq);
  setrgbcolor(cc,0,0,0);
  for(y=0;y<MXSZ;y++) for(x=0;x<MXSZ;x++) if(isingrid(x,y)&&isownmergerep(x,y)) {
    e=gsq[x][y].e0;
    if(e==0) continue; // no entry information
    l=getmergegroup(0,0,x,y);
    md=getmergedir(x,y);
    if(md<0) md=0;
    f=getflags(x,y);
    if((f&9)!=0) continue; // not empty square
    sc=getscale(x,y,0,l);
    c=getechar(x,y);
    if(c==0) {
      de=getdech(x,y);
      if(de==0) nd=1,de=1; else nd=ndir[gtype];
      memcpy(&sq,&gsq[x][y],sizeof(sq)); // in particular initalise ctlen[]:s
      for(i=0;i<nd;i++) for(j=0;j<gsq[x][y].ctlen[i];j++) sq.ctbm[i][j]=e->flbm,e++;
      setrgbcolor(cc,0.75,0.75,0.75);
      drawct(cc,x,y,&sq,1,1,1.0);
      }
    else if(c==' ') {
      m=e->flbm;k=cbits(m); // any info from filler?
      DEB4 printf("%16llx ",m);
      if(k==0) c='?'; // no feasible letters
      else if(k==1) c=ltochar[logbase2(m)]; // unique feasible letter
      else c=' ';
      s[0]=c;s[1]=0;
      setrgbcolor(cc,0.75,0.75,0.75);
      mgcentre(&u,&v,x,y,md,l);
      ctext(cc,s,u,v+0.3*sc,.7*sc,getfstyle(x,y),1);
      if(k>1&&k<pxsq/2) { // more than one feasible letter
        u=1.0/k*sc; // size of red square
        setrgbcolor(cc,1,0,0);
        movetomgcentre(cc,x,y,md,l);
        rmoveto(cc,u/2.0,u/2.0);
        rlineto(cc,0,-u);
        rlineto(cc,-u,0);
        rlineto(cc,0,u);
        closepath(cc);
        fill(cc);
        }
      }
    }
  cairo_destroy(cc);
  invaldaall();
  }

void refreshall() {int i,j;
  for(j=0;j<MXSZ;j++) for(i=0;i<MXSZ;i++) if(isingrid(i,j)) {refreshsq(i,j);}
  refreshcur();
  if(shownums) refreshnum();
  refreshhin();
  refreshsel();
  }


static int gwidth(int sq,int ba) {
  switch(gshape[gtype]) {
  case 0:return             width         *sq+ba*2-1;
  case 1:return (int)floor((width*1.2+0.4)*sq+ba*2-1+.5);
  case 2:if(EVEN(height)) return (int)floor((width*1.4+0.7)*sq+ba*2-1+.5);
         else             return (int)floor((width*1.4    )*sq+ba*2-1+.5);
  case 3:case 4:return height*sq*2+ba*2-1;
    }
  assert(0);
  return 0; // keep VC happy
  }

static int gheight(int sq,int ba) {
  switch(gshape[gtype]) {
  case 0:return (int)floor(height*sq+ba*2-1+.5);
  case 1:if(EVEN(width)) return (int)floor((height*1.4+0.7)*sq+ba*2-1+.5);
         else            return (int)floor((height*1.4    )*sq+ba*2-1+.5);
  case 2:return (int)floor((height*1.2+0.4)*sq+ba*2-1+.5);
  case 3:case 4:return height*sq*2+ba*2-1;
    }
  assert(0);
  return 0; // keep VC happy
  }

int dawidth(void)  {return gwidth (pxsq,bawdpx);}
int daheight(void) {return gheight(pxsq,bawdpx);}


// EXPORT FUNCTIONS

// output string escaped so that it will reproduce correctly in HTML
static void escstr(FILE*fp,char*p) {
  int i,u;
  mbstate_t ps;
  wchar_t wc;

  memset(&ps,0,sizeof(ps));
  for(i=0;p[i];) {
    u=mbrtowc(&wc,p+i,100,&ps);
    if(u<1) break;
    i+=u;
    if(!iswprint(wc)) continue;
    u=(int)wc;
    if(u>123) fprintf(fp,"&#%d;",u);
    else switch(u) {
      case'&': fprintf(fp,"&amp;");break;
      case'\"':fprintf(fp,"&quot;");break;
      case'<': fprintf(fp,"&lt;");break;
      case'>': fprintf(fp,"&gt;");break;
      default: fputc(u,fp);break;
      }
    }
  }


// convert light/answer to string form for display
void ansform(char*t0,int t0l,int ln,int wlen,unsigned int dmask) {
  struct answer*a;
  int em;
  em=lts[ln].em;
  if(em>0&&em<4) { // special entry method (but not jumble)? give grid form too
    strncpy(t0,lts[ln].s,wlen);
    strcpy(t0+wlen,": ");
    }
  else t0[0]=0;
  a=ansp[lts[ln].ans]; // negative ans values canno occur here
  for(;;) {
    if(a->cfdmask&dmask) {
      if((int)strlen(t0)+(int)strlen(a->cf)+2>t0l-LEMDESCLEN-10) {strcat(t0,"...");break;}
      strcat(t0,a->cf);
      if(a->acf) strcat(t0,", ");
      }
    if(!a->acf) break;
    a=a->acf;
    }
  if(em>0) strcat(t0,lemdesc[lts[ln].em]);
  }

// Output answers in direction dir; if dir==-1 do VL:s
// html=0: plain text
// html=1: block HTML with enumeration
// html=2: block HTML
// html=3: table HTML
// cf=1: word lists are valid, can be used for citation forms
static void panswers(FILE*fp,int dir,int html,int cf) {
  int d,f,i,j,k,n,vlf;
  char s[SLEN];
  char t[MXLE+1];
  char t0[MXFL*3+100];
  struct word*w;

  vlf=(dir==-1); // doing virtual lights?
  for(j=0;j<(vlf?1:height);j++) for(i=0;i<(vlf?1:width);i++) for(d=vlf?100:dir;d<(vlf?100+nvl:(dir+1));d++) { // loop over all possible answers
    if(!vlf&&!isstartoflight(i,j,d)) continue;
    f=getword(i,j,d,t);
    if(f<2) {
      if(f==-2) fprintf(fp,"(entry too long)\n");
      continue; // silently skip other errors
      }
    f=cf;
    for(k=0;t[k];k++) if(t[k]==' ') t[k]='.',f=0;
    // f=1 now if we are to print citation forms
    n=vlf?d-100+1:gsq[i][j].number; // answer number
    if(n>0) sprintf(s,"%d",n);
    else    strcpy(s,"-"); // unnumbered light
    if     (html==0) fprintf(fp,"%s ",s);
    else if(html==1) fprintf(fp,"<b>%s</b>&nbsp;",s);
    else if(html==2) fprintf(fp,"<b>%s</b>&nbsp;",s);
    else if(html==3) fprintf(fp,"<tr><td width=0 align=right valign=top><b>%s</b></td>\n<td valign=top>",s);
    if(f) {
      if(vlf) w=vls[d-100].w;
      else    w=gsq[i][j].w[d];
      }
    else w=0;
    if(w&&w->flistlen>0) {
      for(k=0;k<w->flistlen;k++) {
        if(k>0) fprintf(fp,"; ");
        ansform(t0,sizeof(t0),w->flist[k],w->wlen,w->lp->dmask);
        if(html==0) fprintf(fp,"%s",t0);
        else        escstr(fp,t0);
        }
      }
    else fprintf(fp,"%s",t);
    if     (html==0) fprintf(fp," (%d)\n",(int)strlen(t));
    else if(html==1) fprintf(fp,"&nbsp;(%d)<br>\n",(int)strlen(t));
    else if(html==2) fprintf(fp,"\n");
    else if(html==3) fprintf(fp,"&nbsp;(%d)</td></tr>\n",(int)strlen(t)); // answer and length
    }
  }

// HTML export
//  f b0: include grid
//  f b1: include answers in grid
//  f b2: include numbers and marks in grid
//  f b3: include answers in table (to be edited into clues)
//  f b4: include answers in compact format
//  f b5: include link to grid image (given URL)
// Thanks to Paul Boorer for CSS suggestions
void a_exportgh(int f,char*url) {
  int c,cf,d,fl,i,j,i0,j0,k,l,md,n,u,v,x,y,bg,fg,mk,fs,fh;
  int de,nd;
  int hbawd;
  double textscale;
  FILE*fp;

  textscale=((f&6)==6)?0.8:1; // smaller text if we are doing numbers and entries
  hbawd=hpxsq/8; // a suitable bar width
  if(hbawd<3) hbawd=3;
  hbawd|=1; // ensure it is odd
  DEB1 printf("export HTML %s\n",filename);
  fp=fopen(filename,"w");
  if(!fp) {fserror();return;}
  cf=!preexport();
  // emit header and CSS
  fprintf(fp,"\
  <!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.0 Transitional//EN\"\n\
  \"http://www.w3.org/TR/REC-html40/loose.dtd\">\n\
  <html>\n\
  <!-- Created by Qxw %s http://www.quinapalus.com -->\n\
  <head>\n\
  <title>",RELEASE);
  escstr(fp,titlebyauthor());
  fprintf(fp,"</title>\n");
  if(f&1) { // need the styles for the grid
    fprintf(fp,"<style type=\"text/css\">\n");
    fprintf(fp,"div.bk {position:absolute;font-size:0px;border-left:black 0px solid;border-right:black 0px solid;border-top:black %dpx solid;border-bottom:black 0px solid;width:%dpx;height:%dpx;}\n",hpxsq,hpxsq,hpxsq);
    fprintf(fp,"div.hb {position:absolute;font-size:0px;border-left:black 0px solid;border-right:black 0px solid;border-top:black %dpx solid;border-bottom:black 0px solid;width:%dpx;}\n",hbawd,hpxsq+1);
    fprintf(fp,"div.hb0 {position:absolute;font-size:0px;border-left:black 0px solid;border-right:black 0px solid;border-top:black %dpx solid;border-bottom:black 0px solid;width:%dpx;}\n",hbawd,hpxsq+hbawd/2+1);
    fprintf(fp,"div.vb {position:absolute;font-size:0px;border-left:black %dpx solid;border-right:black 0px solid;border-top:black 0px solid;border-bottom:black 0px solid;height:%dpx;}\n",hbawd,hpxsq+1);
    fprintf(fp,"div.vb0 {position:absolute;font-size:0px;border-left:black %dpx solid;border-right:black 0px solid;border-top:black 0px solid;border-bottom:black 0px solid;height:%dpx;}\n",hbawd,hpxsq+hbawd/2+1);
    fprintf(fp,"div.hr {position:absolute;font-size:0px;border-left:black 0px solid;border-right:black 0px solid;border-top:black 1px solid;border-bottom:black 0px solid;}\n");
    fprintf(fp,"div.hr {position:absolute;font-size:0px;border-left:black 0px solid;border-right:black 0px solid;border-top:black 1px solid;border-bottom:black 0px solid;}\n");
    fprintf(fp,"div.vr {position:absolute;font-size:0px;border-left:black 1px solid;border-right:black 0px solid;border-top:black 0px solid;border-bottom:black 0px solid;}\n");
    fprintf(fp,"div.nu {position:absolute;font-size:%dpx;font-family:sans-serif;width:%dpx;height:%dpx}\n",hpxsq/3,hpxsq-4,hpxsq-4);
    fprintf(fp,"div.lt {position:absolute;text-align:center;width:%dpx;font-family:sans-serif}\n",hpxsq);
    fprintf(fp,"</style>\n");
    }
  fprintf(fp,"</head>\n\n<body>\n");
  if(f&1) {
    fprintf(fp,"<center>\n");
    fprintf(fp,"<div style=\"border-width:0px;width:%dpx;height:%dpx;position:relative;\">\n",width*hpxsq+1,height*hpxsq+1);
    fprintf(fp,"<div style=\"border-width:0px;top:0px;left:0px;width:%dpx;height:%dpx;position:absolute;\">\n",width*hpxsq+1,height*hpxsq+1);
    for(j=0;j<height;j++) for(i=0;i<width;i++) {
      bg=getbgcol(i,j);
      fl=getflags(i,j);
      if(fl&1)                      {fprintf(fp,"<div class=\"bk\" style=\"left:%dpx;top:%dpx;\"></div>\n",i*hpxsq+1,j*hpxsq+1);continue;}
      fprintf(fp,"<div class=\"bk\" style=\"left:%dpx;top:%dpx;border-top:#%06X %dpx solid;\"></div>\n",i*hpxsq+1,j*hpxsq+1,bg,hpxsq);
      }
    for(j=0;j<height;j++) for(i=0;i<width;i++) { // plots each bar twice
      if(isbar(i,j,0)) {
        if(isbar(i,j,1)) fprintf(fp,"<div class=\"vb0\" style=\"left:%dpx;top:%dpx;\"></div>\n",(i+1)*hpxsq-hbawd/2,j*hpxsq);
        else             fprintf(fp,"<div class=\"vb\" style=\"left:%dpx;top:%dpx;\"></div>\n",(i+1)*hpxsq-hbawd/2,j*hpxsq);
        }
      if(isbar(i,j,1)) {
        if(isbar(i,j,2)) fprintf(fp,"<div class=\"hb0\" style=\"left:%dpx;top:%dpx;\"></div>\n",i*hpxsq-hbawd/2,(j+1)*hpxsq-hbawd/2);
        else             fprintf(fp,"<div class=\"hb\" style=\"left:%dpx;top:%dpx;\"></div>\n",i*hpxsq,(j+1)*hpxsq-hbawd/2);
        }
      if(isbar(i,j,2)) {
        if(isbar(i,j,3)) fprintf(fp,"<div class=\"vb0\" style=\"left:%dpx;top:%dpx;\"></div>\n",i*hpxsq-hbawd/2,j*hpxsq-hbawd/2);
        else             fprintf(fp,"<div class=\"vb\" style=\"left:%dpx;top:%dpx;\"></div>\n",i*hpxsq-hbawd/2,j*hpxsq);
        }
      if(isbar(i,j,3)) {
        if(isbar(i,j,0)) fprintf(fp,"<div class=\"hb0\" style=\"left:%dpx;top:%dpx;\"></div>\n",i*hpxsq,j*hpxsq-hbawd/2);
        else             fprintf(fp,"<div class=\"hb\" style=\"left:%dpx;top:%dpx;\"></div>\n",i*hpxsq,j*hpxsq-hbawd/2);
        }
      if(f&4) {
        char s[MXMK+1];
        for(c=0;c<4;c++) {
          getmk(s,i,j,c);
          mk=getmkcol(i,j);
          if(!strcmp(s,"")) continue;
          if(!strcmp(s,"\\#")) {
            n=getnumber(i,j);
            if(n<=0) continue;
            sprintf(s,"%d",n);
            }
          switch(c) {
          case 0: fprintf(fp,"<div class=\"nu\" style=\"left:%dpx;top:%dpx;color:%06X;text-align:left\">%s</div>\n" ,i*hpxsq+4,j*hpxsq+1,mk,s); break;
          case 1: fprintf(fp,"<div class=\"nu\" style=\"left:%dpx;top:%dpx;color:%06X;text-align:right\">%s</div>\n",i*hpxsq+1,j*hpxsq+1,mk,s); break;
          case 2: fprintf(fp,"<div class=\"nu\" style=\"left:%dpx;top:%dpx;color:%06X;text-align:right\">%s</div>\n",i*hpxsq+1,j*hpxsq+hpxsq*2/3-4,mk,s); break;
          case 3: fprintf(fp,"<div class=\"nu\" style=\"left:%dpx;top:%dpx;color:%06X;text-align:left\">%s</div>\n" ,i*hpxsq+4,j*hpxsq+hpxsq*2/3-4,mk,s); break;
            }
          }
        }
      }
    for(j=0;j<height;j++) for(i=0;i<width;i++) {
      if((f&2)&&isingrid(i,j)&&isownmergerep(i,j)) {
        char s[MAXNDIR*MXCT+1];
        fg=getfgcol(i,j);
        fs=getfstyle(i,j);
        l=getmergegroup(0,0,i,j);
        md=getmergedir(i,j);
        if(md<0) md=0;
        i0=i; j0=j;
        while(l>2) stepforw(&i0,&j0,md),l-=2;
        if((md==0&&i0==width-1)||(md==1&&j0==height-1)) l=1;
        x=((i0*2+(md==0)*(l-1))*hpxsq)/2+1;
        y=((j0*2+(md==1)*(l-1))*hpxsq)/2+hpxsq/2;
        de=getdech(i,j);
        nd=ndir[gtype];
        if(de==0) nd=1,de=1;
        if(de==2) {
          l=3;
          for(k=0;k<2;k++) if(gsq[i][j].ctlen[k]>l) l=gsq[i][j].ctlen[k];
          fh=(int)floor(hpxsq*textscale/l+.5);
          abmstodispstr(s,gsq[i][j].ctbm[0],gsq[i][j].ctlen[0]);
          fprintf(fp,"<div class=\"lt\" style=\"left:%dpx;top:%dpx;color:%06X;font-size:%dpx;\">%s%s%s%s%s</div>\n",
            x,y-fh,fg,fh,
            (fs&1)?"<b>":"",(fs&2)?"<i>":"",s,(fs&2)?"</i>":"",(fs&1)?"</b>":"");
          abmstodispstr(s,gsq[i][j].ctbm[1],gsq[i][j].ctlen[1]);
          fprintf(fp,"<div class=\"lt\" style=\"left:%dpx;top:%dpx;color:%06X;font-size:%dpx;\">%s%s%s%s%s</div>\n",
            x,y,fg,fh,
            (fs&1)?"<b>":"",(fs&2)?"<i>":"",s,(fs&2)?"</i>":"",(fs&1)?"</b>":"");
        } else {
          s[0]=0;
          for(k=0;k<nd;k++) abmstodispstr(s+strlen(s),gsq[i][j].ctbm[k],gsq[i][j].ctlen[k]);
          l=strlen(s);
          if(l<=1) fh=(int)floor(hpxsq*textscale*3/4+.5);
          else     fh=(int)floor(hpxsq*textscale/l+.5);
          fprintf(fp,"<div class=\"lt\" style=\"left:%dpx;top:%dpx;color:%06X;font-size:%dpx;\">%s%s%s%s%s</div>\n",
            x,y-fh/2,fg,fh,
            (fs&1)?"<b>":"",(fs&2)?"<i>":"",s,(fs&2)?"</i>":"",(fs&1)?"</b>":"");
          }
        }
      }
    for(j=0;j<=height;j++) for(i=0,v=0;i<=width;i++) {
      if(j==0) u=!ismerge(i,j  ,3);
      else     u=!ismerge(i,j-1,1);
      u&=sqexists(i,j)|sqexists(i,j-1);
      if(u==0&&v>0) fprintf(fp,"<div class=\"hr\" style=\"left:%dpx;top:%dpx;width:%dpx;\"></div>\n",(i-v)*hpxsq,j*hpxsq,v*hpxsq+1);
      if(u==0) v=0; else v++;
      }
    for(i=0;i<=width;i++) for(j=0,v=0;j<=height;j++) {
      if(i==0) u=!ismerge(i  ,j,2);
      else     u=!ismerge(i-1,j,0);
      u&=sqexists(i,j)|sqexists(i-1,j);
      if(u==0&&v>0) fprintf(fp,"<div class=\"vr\" style=\"left:%dpx;top:%dpx;height:%dpx;\"></div>\n",i*hpxsq,(j-v)*hpxsq,v*hpxsq+1);
      if(u==0) v=0; else v++;
      }
    fprintf(fp,"</div></div></center>\n");
    }
  if(f&0x20) {
    fprintf(fp,"<center>\n");
    fprintf(fp,"<img src=\"%s\" alt=\"grid image\">\n",url);
    fprintf(fp,"</center>\n");
    }

  if(f&8) { // print table of `clues'
    fprintf(fp,"<center><table><tr>\n");
    for(d=0;d<ndir[gtype];d++) {
      fprintf(fp,"<td width=\"%d%%\"><b>%s</b></td>\n",96/ndir[gtype],dname[gtype][d]);
      if(d<ndir[gtype]-1) fprintf(fp,"<td>&nbsp;</td>\n");
      }
    fprintf(fp,"</tr><tr>\n");
    for(d=0;d<ndir[gtype];d++) {
      fprintf(fp,"<td valign=top><table>\n");
      panswers(fp,d,3,cf);
      fprintf(fp,"</table></td>\n");
      if(d<ndir[gtype]-1) fprintf(fp,"<td>&nbsp;</td>\n");
      }
    fprintf(fp,"</tr>\n");
    if(nvl>0) {
      fprintf(fp,"<tr><td colspan=\"%d\"><b>Other</b></td></tr>\n",ndir[gtype]*2-1);
      fprintf(fp,"<tr><td colspan=\"%d\"><table>\n",ndir[gtype]*2-1);
      panswers(fp,-1,2,cf);
      fprintf(fp,"</table></td></tr>\n");
      }
    fprintf(fp,"</table></center>\n");
    }

  if(f&16) { // print answers in condensed form
    for(d=0;d<ndir[gtype];d++) {
      fprintf(fp,"<b>%s:</b>\n",dname[gtype][d]);
      panswers(fp,d,2,cf);
      fprintf(fp,"<br>\n");
      }
    if(nvl>0) {
      fprintf(fp,"<b>Other:</b>\n");
      panswers(fp,-1,2,cf);
      fprintf(fp,"<br>\n");
      }
    }

  fprintf(fp,"</body></html>\n");
  if(ferror(fp)) fserror();
  if(fclose(fp)) fserror();
  postexport();
  }


// GRID AS EPS OR PNG

static cairo_status_t filewrite(FILE*fp,unsigned char*p,unsigned int n) {
  return (fwrite(p,1,n,fp)==n)?CAIRO_STATUS_SUCCESS:CAIRO_STATUS_WRITE_ERROR;
  }

// f0 b1: include answers in grid
// f0 b2: include numbers and marks in grid
// f1: 0=EPS 1=SVG 2=PNG 
void a_exportg(char*fn,int f0,int f1) {
  cairo_surface_t*sf=0;
  cairo_t*cc;
  int i,j,sq=0,ba,px,py;
  char s[1000];
  double textscale;
  FILE*fp=0;

  DEB1 printf("a_exportg(\"%s\",%d,%d)\n",fn,f0,f1);
  switch(f1) {
  case 0: sq=eptsq; break;
  case 1: sq=eptsq; break;
  case 2: sq=hpxsq; break;
    }
  textscale=((f0&6)==6)?0.8:1; // smaller text if we are doing numbers and entries
  ba=sq/8; // a suitable bar width
  if(ba<2) ba=2;
  px=gwidth(sq,ba);
  py=gheight(sq,ba);
  switch(f1) {
  case 0:
    fp=fopen(fn,"w");
    if(!fp) {fserror();return;}
    sf=cairo_ps_surface_create_for_stream((cairo_write_func_t)filewrite,fp,px,py);
    cairo_ps_surface_restrict_to_level(sf,CAIRO_PS_LEVEL_2);
    cairo_ps_surface_set_eps(sf,1);
    sprintf(s,"%%%%Title: %s generated by Qxw",fn);
    cairo_ps_surface_dsc_comment(sf,s);
    break;
  case 1:
    fp=fopen(fn,"w");
    if(!fp) {fserror();return;}
    sf=cairo_svg_surface_create_for_stream((cairo_write_func_t)filewrite,fp,px,py);
    cairo_svg_surface_restrict_to_version(sf,CAIRO_SVG_VERSION_1_1);
    break;
  case 2:
    sf=cairo_image_surface_create(CAIRO_FORMAT_ARGB32,px,py);
    break;
    }
  cc=cairo_create(sf);
  if(f1==2) {
    cairo_set_source_rgb(cc,1,1,1); // flood PNG to white to avoid nasty transparency effect in merged cells
    cairo_paint(cc);
    }
  cairo_translate(cc,ba-.5,ba-.5);
  cairo_scale(cc,sq,sq);
  for(j=0;j<MXSZ;j++) for(i=0;i<MXSZ;i++) if(isingrid(i,j)) {
    drawsqbg(cc,i,j,sq,1);
    }
  for(j=0;j<MXSZ;j++) for(i=0;i<MXSZ;i++) if(isingrid(i,j)) {
    gsave(cc);
    drawsqfg(cc,i,j,sq,ba,!!(f0&2),0,textscale);
    grestore(cc);
    }
  setrgbcolor(cc,0,0,0);
  if(f0&4) drawnums(cc);
  switch(f1) {
  case 0: 
  case 1: 
    cairo_show_page(cc);
    break;
  case 2: 
    if(cairo_surface_write_to_png(sf,fn)!=CAIRO_STATUS_SUCCESS) fsgerr();
    break;
    }
  cairo_destroy(cc);
  cairo_surface_destroy(sf);
  switch(f1) {
  case 0: 
  case 1: 
    if(ferror(fp)) {fserror();fclose(fp);}
    else if(fclose(fp)) fserror();
    break;
  case 2: 
    break;
    }
  }


// ANSWERS

// f: 0=text, 1=HTML
void a_exporta(int f) {
  int cf,d;
  FILE*fp;

  cf=!preexport();
  fp=fopen(filename,"w");
  if(!fp) {fserror();return;}
  fprintf(fp,f?"<html><body><!-- ":"# ");
  fprintf(fp,"file %s generated by Qxw %s: http://www.quinapalus.com",filename,RELEASE);
  fprintf(fp,f?" -->\n":"\n");
  for(d=0;d<ndir[gtype];d++) {
    if(f) fprintf(fp,"\n<b>%s</b><br>\n\n",dname[gtype][d]);
    else  fprintf(fp,"\n%s\n\n",dname[gtype][d]);
    panswers(fp,d,f,cf);
    }
  if(nvl>0) {
    if(f) fprintf(fp,"\n<b>Other</b><br>\n\n");
    else  fprintf(fp,"\nOther\n\n");
    panswers(fp,-1,f,cf);
    }
  if(f) fprintf(fp,"</body></html>\n");
  if(ferror(fp)) fserror();
  if(fclose(fp)) fserror();
  postexport();
  }

// COMBINED HTML AND PNG

// f: 0=puzzle, 1=solution
// f0: f0 flags for exportg
// f1: 1=SVG, 2=PNG
void a_exporthp(int f,int f0,int f1) {char url[SLEN+200];
  strcpy(url,filename);
  strcat(url,"_files");
#ifdef _WIN32   // Windows version of mkdir()
  _mkdir(url);
#else
  mkdir(url,0777);
#endif
  strcat(url,f?"/solution":"/grid");
  strcat(url,f1==1?".svg":".png");
  a_exportgh(f?0x30:0x28,url);
  a_exportg(url,f0,f1);
  }
