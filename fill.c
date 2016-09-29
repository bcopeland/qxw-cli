// $Id: qxw.c 553 2014-03-04 15:19:02Z mo $

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
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <time.h>
#include <math.h>
#include <unistd.h>
#include <pwd.h>
#include <wchar.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>

#include "common.h"
#include "filler.h"
#include "dicts.h"

#define fserror(x) exit(0)

char filename[SLEN+50]; // result from filedia()
char filenamebase[SLEN]; // base to use for constructing default filenames

// GLOBAL PARAMETERS

// these are saved:

int width=4,height=4;          // grid size
int gtype=0;                     // grid type: 0=square, 1=hexH, 2=hexV, 3=circleA (edge at 12 o'clock), 4=circleB (cell at 12 o'clock)
                                 // 5=cylinder L-R, 6=cylinder T-B, 7=Moebius L-R, 8=Moebius T-B, 9=torus
int symmr=2,symmm=0,symmd=0;     // symmetry mode flags (rotation, mirror, up-and-down/left-and-right)
int nvl=0;                       // number of virtual lights
struct sprop dsp;                // default square properties
struct lprop dlp;                // default light properties

char gtitle[SLEN]="";
char gauthor[SLEN]="";

// these are not saved:
int debug=0;

int curx=0,cury=0,dir=0;         // cursor position, and direction: 0=to right, 1=down
int unsaved=0;                   // edited-since-save flag
int cwperm[NL-1];

int ndir[NGTYPE]={2,3,3,2,2, 2,2,2,2,2};  // number of directions per grid type
int gshape[NGTYPE]={0,1,2,3,4,0,0,0,0,0};  // basic shape of grid
int dhflip[NGTYPE][MAXNDIR*2]={
{2,1,0,3},
{4,3,2,1,0,5},
{3,2,1,0,5,4},
{2,1,0,3},
{2,1,0,3},
{2,1,0,3},
{2,1,0,3},
{2,1,0,3},
{2,1,0,3},
{2,1,0,3}
};
int dvflip[NGTYPE][MAXNDIR*2]={
{0,3,2,1},
{1,0,5,4,3,2},
{0,5,4,3,2,1},
{2,1,0,3},
{2,1,0,3},
{0,3,2,1},
{0,3,2,1},
{0,3,2,1},
{0,3,2,1},
{0,3,2,1}
};


void reperr(const char*s) {
	printf("%s\n", s);
}
static unsigned char log2lut[65536];

int cbits(ABM x) {ABM i; int j; for(i=1,j=0;i<ABM_ALL;i+=i) if(x&i) j++; return j;} // count set bits
int onebit(ABM x) {return x!=0&&(x&(x-1))==0;}
int logbase2(ABM x) {int u;    // find lowest set bit
  u= x     &0xffff; if(u) return log2lut[u];
  u=(x>>16)&0xffff; if(u) return log2lut[u]+16;
  u=(x>>32)&0xffff; if(u) return log2lut[u]+32;
  u=(x>>48)&0xffff; if(u) return log2lut[u]+48;
  return -1;
  }

int*llistp=0;    // ptr to matching lights
int llistn=0;    // number of matching lights
unsigned int llistdm=0;   // dictionary mask applicable to matching lights
int llistwlen=0; // word length (less tags if any) applicable to matching lights
int llistem=0;   // entry method mask applicable to matching lights
int*llist=0;     // buffer for light index list

// GRID

struct square gsq[MXSZ][MXSZ];
struct vl vls[NVL];

int getdech(int x,int y) {int xr,yr;
  getmergerep(&xr,&yr,x,y);
  return gsq[xr][yr].sp.spor?gsq[xr][yr].sp.dech:dsp.dech;
  }

// move backwards one cell in direction d
void stepback(int*x,int*y,int d) {
//  printf("stepback(%d,%d,%d)\n",*x,*y,d);
  if(d>=ndir[gtype]) {stepforw(x,y,d-ndir[gtype]); return;}
  if(gtype==1) {
    switch(d) {
      case 0: if(((*x)&1)==1) (*y)++; (*x)--; break;
      case 1: if(((*x)&1)==0) (*y)--; (*x)--; break;
      case 2: (*y)--;break;
        }
    return;
    }
  if(gtype==2) {
    switch(d) {
      case 2: if(((*y)&1)==1) (*x)++; (*y)--; break;
      case 1: if(((*y)&1)==0) (*x)--; (*y)--; break;
      case 0: (*x)--;break;
        }
    return;
    }
  // rectangular grid cases
  if(d) (*y)--; else (*x)--;
  if(gtype==3||gtype==4||gtype==5||gtype==7||gtype==9) { // loop in x direction
    if(*x<0) {
      *x+=width;
      if(gtype==7) *y=height-1-*y;
      }
    }
  if(gtype==6||gtype==8||gtype==9) { // loop in y direction
    if(*y<0) {
      *y+=height;
      if(gtype==8) *x=width-1-*x;
      }
    }
//  printf("=(%d,%d)\n",*x,*y);
  }

// move forwards one cell in direction d
void stepforw(int*x,int*y,int d) {
//  printf("stepforw(%d,%d,%d)\n",*x,*y,d);
  if(d>=ndir[gtype]) {stepback(x,y,d-ndir[gtype]); return;}
  if(gtype==1) {
    switch(d) {
      case 0: (*x)++; if(((*x)&1)==1) (*y)--; break;
      case 1: (*x)++; if(((*x)&1)==0) (*y)++; break;
      case 2: (*y)++;break;
        }
    return;
    }
  if(gtype==2) {
    switch(d) {
      case 2: (*y)++; if(((*y)&1)==1) (*x)--; break;
      case 1: (*y)++; if(((*y)&1)==0) (*x)++; break;
      case 0: (*x)++;break;
        }
    return;
    }
  // rectangular grid cases
  if(d) (*y)++; else (*x)++;
  if(gtype==3||gtype==4||gtype==5||gtype==7||gtype==9) { // loop in x direction
    if(*x>=width) {
      *x-=width;
      if(gtype==7) *y=height-1-*y;
      }
    }
  if(gtype==6||gtype==8||gtype==9) { // loop in y direction
    if(*y>=height) {
      *y-=height;
      if(gtype==8) *x=width-1-*x;
      }
    }
//  printf("=(%d,%d)\n",*x,*y);
  }

int isingrid(int x,int y) {
  if(x<0||y<0||x>=width||y>=height) return 0;
  if(gshape[gtype]==1&&ODD(width )&&ODD(x)&&y==height-1) return 0;
  if(gshape[gtype]==2&&ODD(height)&&ODD(y)&&x==width -1) return 0;
  return 1;
  }

int isclear(int x,int y) {
  if(!isingrid(x,y)) return 0;
  if(gsq[x][y].fl&0x09) return 0;
  return 1;
  }

int isbar(int x,int y,int d) {int u;
  if(d>=ndir[gtype]) {d-=ndir[gtype];stepback(&x,&y,d);}
  if(!isingrid(x,y)) return 0;
  u=(gsq[x][y].bars>>d)&1;
//  DEB2 printf("  x=%d y=%d d=%d u=%d g[x,y].bars=%08x\n",x,y,d,u,gsq[x][y].bars);
  stepforw(&x,&y,d);
  if(!isingrid(x,y)) return 0;
  return u;
  }

int ismerge(int x,int y,int d) {int u;
  if(d>=ndir[gtype]) {d-=ndir[gtype];stepback(&x,&y,d);}
  if(!isingrid(x,y)) return 0;
  u=(gsq[x][y].merge>>d)&1;
  stepforw(&x,&y,d);
  if(!isingrid(x,y)) return 0;
  return u;
  }

// is a step backwards clear? (not obstructed by a bar, block, cutout or edge of grid)
int clearbefore(int x,int y,int d) {int tx,ty;
  tx=x;ty=y;stepback(&tx,&ty,d);
  if(!isingrid(tx,ty)) return 0;
  if(!isclear(tx,ty)) return 0;
  if(isbar(tx,ty,d)) return 0;
  return 1;
  }

// is a step forwards clear?
int clearafter(int x,int y,int d) {int tx,ty;
  tx=x;ty=y;stepforw(&tx,&ty,d);
  if(!isingrid(tx,ty)) return 0;
  if(!isclear(tx,ty)) return 0;
  if(isbar(x,y,d)) return 0;
  return 1;
  }

// move forwards one (merged) cell in direction d
static void stepforwm(int*x,int*y,int d) {int x0,y0;
  x0=*x;y0=*y;
  while(ismerge(*x,*y,d)) {stepforw(x,y,d);if(*x==x0&&*y==y0) return;}
  stepforw(x,y,d);
  }

// is a step forwards clear?
static int clearafterm(int x,int y,int d) {int tx,ty;
  tx=x;ty=y;stepforwm(&tx,&ty,d);
  if(x==tx&&y==ty) return 0;
  if(!isingrid(tx,ty)) return 0;
  if(!isclear(tx,ty)) return 0;
  stepback(&tx,&ty,d);
  if(isbar(tx,ty,d)) return 0;
  return 1;
  }

int stepbackifingrid (int*x,int*y,int d) {int tx,ty; tx=*x;ty=*y; stepback (&tx,&ty,d);if(!isingrid(tx,ty)) return 0; *x=tx;*y=ty; return 1;}
//static int stepforwifingrid (int*x,int*y,int d) {int tx,ty; tx=*x;ty=*y; stepforw (&tx,&ty,d);if(!isingrid(tx,ty)) return 0; *x=tx;*y=ty; return 1;} // not currently used
int stepforwmifingrid(int*x,int*y,int d) {int tx,ty; tx=*x;ty=*y; stepforwm(&tx,&ty,d);if(!isingrid(tx,ty)) return 0; *x=tx;*y=ty; return 1;}


void getmergerepd(int*mx,int*my,int x,int y,int d) {int x0,y0; // find merge representative, direction d (0<=d<ndir[gtype]) only
  assert(0<=d);
  assert(d<ndir[gtype]);
  *mx=x;*my=y;
  if(!isclear(x,y)) return;
  if(!ismerge(x,y,d+ndir[gtype])) return;
  x0=x;y0=y;
  do {
    stepback(&x,&y,d);
    if(!isclear(x,y)) goto ew1;
    if(x+y*MXSZ<*mx+*my*MXSZ) *mx=x,*my=y; // first lexicographically if loop
    if(x==x0&&y==y0) goto ew1; // loop detected
    } while(ismerge(x,y,d+ndir[gtype]));
  *mx=x;*my=y;
  ew1: ;
  }

int getmergedir(int x,int y) {int d; // get merge direction
  if(!isclear(x,y)) return -1;
  for(d=0;d<ndir[gtype];d++) if(ismerge(x,y,d)||ismerge(x,y,d+ndir[gtype])) return d;
  return 0;
  }

void getmergerep(int*mx,int*my,int x,int y) {int d; // find merge representative, any direction
  *mx=x;*my=y;
  d=getmergedir(x,y); if(d<0) return;
  getmergerepd(mx,my,x,y,d);
  }

int isownmergerep(x,y) {int x0,y0;
  getmergerep(&x0,&y0,x,y);
  return x==x0&&y==y0;
  }

// get coordinates of cells (max MXCL) in merge group with (x,y) in direction d
int getmergegroupd(int*gx,int*gy,int x,int y,int d) {int l,x0,y0;
  if(!isclear(x,y)) {if(gx) *gx=x;if(gy) *gy=y;return 1;}
  getmergerepd(&x,&y,x,y,d);
  x0=x;y0=y;l=0;
  for(;;) {
    assert(l<MXCL);
    if(gx) gx[l]=x;if(gy) gy[l]=y;l++;
    if(!ismerge(x,y,d)) break;
    stepforw(&x,&y,d);
    if(!isclear(x,y)) break;
    if(x==x0&&y==y0) break;
    }
  return l;
  }

// get coordinates of cells (max MXCL) in merge group with (x,y) in merge direction
int getmergegroup(int*gx,int*gy,int x,int y) {int d;
  if(!isclear(x,y)) {if(gx) *gx=x;if(gy) *gy=y;return 1;}
  d=getmergedir(x,y); assert(d>=0);
  return getmergegroupd(gx,gy,x,y,d);
  }

int isstartoflight(int x,int y,int d) {
  if(!isclear(x,y)) return 0;
  if(clearbefore(x,y,d)) return 0;
  if(clearafterm(x,y,d)) return 1;
  return 0;
  }

// returns:
// -1: looped, no light found
//  0: blocked, no light found
//  1: light found, start (not mergerep'ed) stored in *lx,*ly
static int getstartoflight(int*lx,int*ly,int x,int y,int d) {int x0,y0;
  if(!isclear(x,y)) return 0;
  x0=x;y0=y;
  while(clearbefore(x,y,d)) {
    stepback(&x,&y,d); // to start of light
    if(x==x0&&y==y0) return -1; // loop found
    }
  *lx=x;*ly=y;
  return 1;
  }

// is light selected?
int issellight(int x,int y,int d) {int l,lx,ly;
  l=getstartoflight(&lx,&ly,x,y,d);
  if(l<1) return 0;
  if(!isstartoflight(lx,ly,d)) return 0; // not actually a light
  return (gsq[lx][ly].dsel>>d)&1;
  }

// set selected flag on a light to k
void sellight(int x,int y,int d,int k) {int l,lx,ly;
  DEB1 printf("sellight(%d,%d,%d,%d)\n",x,y,d,k);
  l=getstartoflight(&lx,&ly,x,y,d);
  if(l<1) return;
  if(!isstartoflight(lx,ly,d)) return; // not actually a light
  gsq[lx][ly].dsel=(gsq[lx][ly].dsel&(~(1<<d)))|(k<<d);
  }

// extract mergerepd'ed grid squares (max MXCL) forming light running through (x,y) in direction d
// returns:
//   -1: loop detected
//    0: (x,y) blocked
//    1: no light in this direction (lx[0], ly[0] still set)
// n>=2: length of light (lx[0..n-1], ly[0..n-1] set), can be up to MXCL for VL, up to MXSZ*2 in Mobius case
// if d>=100 returns data for VL #d-100
int getlightd(int*lx,int*ly,int x,int y,int d) {int i,j;
  if(d>=100) {
    d-=100;
    for(i=0,j=0;i<vls[d].l;i++) if(isclear(vls[d].x[i],vls[d].y[i])) lx[j]=vls[d].x[i],ly[j]=vls[d].y[i],j++; // skip invalid squares
    return j;
    }
  i=getstartoflight(&x,&y,x,y,d);
  if(i<1) return i;
  i=0;
  for(;;) {
    assert(i<MXSZ*2);
    lx[i]=x;ly[i]=y;i++;
    if(!clearafterm(x,y,d)) break;
    stepforwm(&x,&y,d);
    }
  return i;
  }

// extract merge-representative grid squares (up to MXCL) forming light running through (x,y) in direction d
// if d>=100 returns data for VL #d-100
// errors as for getlightd()
int getlight(int*lx,int*ly,int x,int y,int d) {int i,l;
  l=getlightd(lx,ly,x,y,d);
  for(i=0;i<l;i++) getmergerep(lx+i,ly+i,lx[i],ly[i]);
  return l;
  }

// extract data for light running through (x,y) in direction d
// if d>=100 returns data for VL #d-100
// returns -2 if light overflows plus errors as for getlightd()
// lp: bitmap ptrs (<=MXLE)
// lx: mergerep square x (<=MXLE)
// ly: mergerep square y (<=MXLE)
// ls: index of contributing string (<=MXLE)
// lo: offset in contributing string (<=MXLE)
// le: entry number (<=MXLE)
static int getlightdat(ABM**lp,int*lx,int*ly,int*ls,int*lo,int*le,int x0,int y0,int d) {int c,e,i,j,k,l,m,tx[MXCL],ty[MXCL],x,y;
  l=getlight(tx,ty,x0,y0,d);
  if(l<1) return l;
  for(i=0,m=0;i<l;i++) { // for each square in the light...
    x=tx[i]; y=ty[i];
    // compute contribution mask
    if(getdech(x,y)==0) c=1; // normal checking
    else if(d<100)      c=1<<d; // de-checked: not VL
    else                c=(1<<ndir[gtype])-1; // all directions contribute to VL:s
    e=gsq[x][y].e0-entries;
    for(j=0;j<ndir[gtype];j++) {
      if((c&(1<<j))==0) {e+=gsq[x][y].ctlen[j]; continue;} // skip unused contributions
      for(k=0;k<gsq[x][y].ctlen[j];k++) {
        if(m==MXLE) return -2;
        if(lp) lp[m]=gsq[x][y].ctbm[j]+k;
        if(lx) lx[m]=x;
        if(ly) ly[m]=y;
        if(ls) ls[m]=j;
        if(lo) lo[m]=k;
        if(le) le[m]=e++;
        m++;
        }
      }
    }
  return m;
  }

char abmtoechar(ABM b) {
  if(b==0) return '?';
  if(onebit(b)) return ltochar[logbase2(b)];
  return ' ';
  }

// extract pointers to bitmaps forming light running through (x,y) in direction d
// returns errors as getlightdat
int getlightbmp(ABM**p,int x,int y,int d) {return getlightdat(p,0,0,0,0,0,x,y,d);}

// extract word running through x,y in direction d from grid: s must have space for MXLE+1 chars
// returns errors as getlightdat
int getword(int x,int y,int d,char*s) {int i,l; ABM*chp[MXLE];
  l=getlightbmp(chp,x,y,d);
  for(i=0;i<l;i++) {
    s[i]=abmtoechar(chp[i][0]);
    if(s[i]==' ') s[i]='.';
    }
  s[i]='\0';
  return l;
  }

// if normally-checked, single-character square, get character in mergerep square; else return 0
char getechar(int x,int y) {
  if(getdech(x,y)) return 0;
  getmergerep(&x,&y,x,y);
  if(gsq[x][y].ctlen[0]!=1) return 0;
  return abmtoechar(gsq[x][y].ctbm[0][0]);
  }

// if normally-checked, single-character square or
// dechecked but only single character in direction d,
// set character in mergrep square and return 0; else return 1
int setechar(int x,int y,int d,char c) {
  if(!getdech(x,y)) d=0;
  getmergerep(&x,&y,x,y);
  if(gsq[x][y].ctlen[d]!=1) return 1;
  gsq[x][y].ctbm[d][0]=c==' '?ABM_ALNUM:chartoabm[(int)c];
  return 0;
  }

// clear contents of cell
void clrcont(int x,int y) {int i,j;
  getmergerep(&x,&y,x,y);
  for(i=0;i<MAXNDIR;i++) for(j=0;j<gsq[x][y].ctlen[i];j++) gsq[x][y].ctbm[i][j]=ABM_ALNUM;
  }





static void setmerge(int x,int y,int d,int k) { // set merge state in direction d to k, make bar state consistent
  if(d>=ndir[gtype]) {d-=ndir[gtype];stepback(&x,&y,d);}
  if(!isingrid(x,y)) return;
  if(k) k=1;
  gsq[x][y].merge&=~(1<<d);
  gsq[x][y].merge|=  k<<d;
  gsq[x][y].bars &=~gsq[x][y].merge;
  }

static void setbars(int x,int y,int d,int k) { // set bar state in direction d to k, make merge state consistent
  if(d>=ndir[gtype]) {d-=ndir[gtype];stepback(&x,&y,d);}
  if(!isingrid(x,y)) return;
  if(k) k=1;
  gsq[x][y].bars &=~(1<<d);
  gsq[x][y].bars |=  k<<d;
  gsq[x][y].merge&=~gsq[x][y].bars;
  }

static void demerge(int x,int y) {int i; // demerge from all neighbours
  for(i=0;i<ndir[gtype]*2;i++) setmerge(x,y,i,0);
  }

// calculate mask of feasible symmetries
int symmrmask(void) {int i,m;
  switch(gshape[gtype]) {
case 0: return 0x16;break;
case 1:
    if(EVEN(width))          return 0x06;
    if(EVEN(width/2+height)) return 0x06;
    else                     return 0x5e;
    break;
case 2:
    if(EVEN(height))          return 0x06;
    if(EVEN(height/2+width))  return 0x06;
    else                      return 0x5e;
    break;
case 3: case 4:
    m=0;
    for(i=1;i<=12;i++) if(width%i==0) m|=1<<i;
    return m;
    break;
    }
  return 0x02;
  }

int symmmmask(void) {
  switch(gshape[gtype]) {
case 0: case 1: case 2: return 0x0f;break;
case 3: case 4:
    if(width%2) return 0x03;
    else        return 0x0f;
    break;
    }
  return 0x01;
  }

int symmdmask(void) {
  switch(gshape[gtype]) {
case 0: case 1: case 2:return 0x0f;break;
case 3: case 4:return 0x01;break;
    }
  return 0x01;
  }

int nw,ntw,nc,ne,ne0; // count of words, treated words, cells, entries
struct word*words=0;
struct entry*entries=0;
struct entry*treatmsge0[NMSG][MXFL]={{0}};

struct lprop msglprop[NMSG];
char msgword[NMSG][MXFL+1];

// statistics calculated en passant by bldstructs()
int st_lc[MXLE+1];             // total lights, by length
int st_lucc[MXLE+1];           // underchecked lights, by length
int st_locc[MXLE+1];           // overchecked lights, by length
int st_lsc[MXLE+1];            // total of checked entries in lights, by length
int st_lmnc[MXLE+1];           // minimum checked entries in lights, by length
int st_lmxc[MXLE+1];           // maximum checked entries in lights, by length
int st_hist[NL+2];             // entry letter histogram: [NL]=uncommitted, [NL+1]=partially committed
int st_sc;                     // total checked cells
int st_ce;                     // total checked entries (i.e., squares)
int st_2u,st_3u;               // count of double+ and triple+ unches
int st_tlf,st_vltlf,st_tmtlf;  // count of (free) lights too long for filler



// calculate numbers for squares and grid-order-indices of treated squares
void donumbers(void) {
  int d,i,i0,j,num=1,goi=0;
  struct lprop*lp;

  for(j=0;j<height;j++) for(i=0;i<width;i++) gsq[i][j].number=-1,gsq[i][j].goi=-1;
  for(j=0;j<height;j++) for(i0=0;i0<width;i0++) {
    if(gshape[gtype]==1) {i=i0*2;if(i>=width) i=(i-width)|1;} // special case for hexH grid
    else i=i0;
    if(isclear(i,j)) {
      if(isownmergerep(i,j)&&(gsq[i][j].sp.spor?gsq[i][j].sp.ten:dsp.ten)) gsq[i][j].goi=goi++;
      for(d=0;d<ndir[gtype];d++)
        if(isstartoflight(i,j,d)) {
          lp=gsq[i][j].lp[d].lpor?&gsq[i][j].lp[d]:&dlp;
          if(!lp->dnran) {gsq[i][j].number=num++; break;}
          }
      }
    }
  }

// add word to filler data, starting at (i,j) in direction d
// if d>=100, add virtual light #d-100
// return:
// 0: OK
// 1: too long
// 2: ignored as dmask is zero
static int addwordfd(int i,int j,int d) {
  int f,k,l,le[MXLE],lx[MXLE],ly[MXLE];
  ABM*lcp[MXLE];
  struct lprop*lp;

  if(d<100) lp=gsq[i][j].lp[d].lpor?&gsq[i][j].lp[d]:&dlp;
  else      lp=&vls[d-100].lp;
  if(lp->dmask==0) return 2; // skip if dictionary mask empty
  l=getlightdat(lcp,lx,ly,0,0,le,i,j,d);
  if(l<1) return 1; // too long: ignore
  DEB8 printf("new word %d: l=%d\n",nw,l);
  f=0;
  for(k=0;k<l;k++) if(!onebit(lcp[k][0])) f=1;
  words[nw].fe=!f; // fully entered?
  words[nw].gx0=i;
  words[nw].gy0=j;
  if(d<100) {
    gsq[i][j].w[d]=words+nw;
    words[nw].ldir=d;
    }
  else {
    vls[d-100].w=words+nw;
    words[nw].ldir=d;
    }
  words[nw].lp=lp;
  for(k=0;k<l;k++) {
    words[nw].e[k]=entries+le[k];
    words[nw].goi[k]=gsq[lx[k]][ly[k]].goi;
    DEB8 printf("  d=%d x=%d y=%d k=%d e=%d goi=%d\n",d,lx[k],ly[k],k,le[k],words[nw].goi[k]);
    nc++;
    }
  words[nw].nent=k;
  words[nw].wlen=k;
  words[nw].jlen=k;
  DEB8 printf("  nent=%d\n",k);
  nw++;
  if(lp->ten) ntw++;
  return 0;
  }

static void initstructs() {int i,j,k,d;
  nw=0; ntw=0; nc=0; ne=0; ne0=0;
  for(i=0;i<NVL;i++) vls[i].w=0;
  for(j=0;j<height;j++) for(i=0;i<width;i++) {
    for(d=0;d<MAXNDIR;d++) gsq[i][j].w[d]=0,gsq[i][j].vflags[d]=0;
    gsq[i][j].e0=0;
    gsq[i][j].ne=0;
    gsq[i][j].number=-1;
    }
  for(i=0;i<NMSG;i++) for(j=0;j<MXFL;j++) treatmsge0[i][j]=0;
  for(i=0;i<MXLE+1;i++) st_lc[i]=st_lucc[i]=st_locc[i]=st_lsc[i]=0,st_lmxc[i]=-1,st_lmnc[i]=99; // initialise stats
  for(i=0;i<NL+2;i++) st_hist[i]=0;
  st_tlf=st_vltlf=st_tmtlf=0;
  for(i=0;i<NMSG;i++) {
    for(j=0,k=0;treatmsg[i][j];j++) { // make all capitals, letters-only version
      if     (isupper(treatmsg[i][j])) treatmsgAZ[i][k++]=        treatmsg[i][j];
      else if(islower(treatmsg[i][j])) treatmsgAZ[i][k++]=toupper(treatmsg[i][j]);
      }
    treatmsgAZ[i][k]=0;
    }
  for(i=0;i<NMSG;i++) {
    for(j=0,k=0;treatmsg[i][j];j++) { // make all capitals, letters+digits version
      if     (isupper(treatmsg[i][j])) treatmsgAZ09[i][k++]=        treatmsg[i][j];
      else if(islower(treatmsg[i][j])) treatmsgAZ09[i][k++]=toupper(treatmsg[i][j]);
      else if(isdigit(treatmsg[i][j])) treatmsgAZ09[i][k++]=        treatmsg[i][j];
      }
    treatmsgAZ09[i][k]=0;
    }
  }

// build structures for solver and compile statistics
// 0: OK
// 1: too complex
// Ensure filler is stopped before calling this (reallocates e.g. words[])
static int bldstructs(void) {
  int cnw,cne,d,i,j,k,l,nd,v,ti,tj;
  unsigned char unch[MXLE];
  ABM c;

  if(words) for(i=0;i<nw;i++) {FREEX(words[i].flist); FREEX(words[i].jdata); FREEX(words[i].sdata); words[i].flistlen=0;}
  FREEX(words);
  FREEX(entries);
  initstructs();
  cnw=nvl;
  for(d=0;d<ndir[gtype];d++) for(j=0;j<height;j++) for(i=0;i<width;i++) if(isstartoflight(i,j,d)) cnw++; // look for normal lights
  cne=0;
  for(j=0;j<height;j++) for(i=0;i<width;i++) if(isclear(i,j)&&isownmergerep(i,j)) {
    if(getdech(i,j)) nd=ndir[gtype];
    else             nd=1;
    for(k=0;k<nd;k++) cne+=gsq[i][j].ctlen[k];
    }

  words=calloc(cnw+NMSG,sizeof(struct word)); // one spare word for each message
  if(!words) return 1;
  entries=calloc(cne+NMSG*cnw,sizeof(struct entry)); // cnw spares for each message
  if(!entries) return 1;

  for(j=0;j<height;j++) for(i=0;i<width;i++) if(isclear(i,j)&&isownmergerep(i,j)) {
    gsq[i][j].e0=entries+ne;
    gsq[i][j].ne=0;
    if(getdech(i,j)) nd=ndir[gtype];
    else             nd=1;
    for(k=0;k<nd;k++) for(l=0;l<gsq[i][j].ctlen[k];l++) {
      entries[ne].gx=i;
      entries[ne].gy=j;
      entries[ne].sel=!!(gsq[i][j].fl&16); // selected?
      c=gsq[i][j].ctbm[k][l]&ABM_ALNUM; // letter in square
      entries[ne].flbm=c; // bitmap of feasible letters
      if(c==ABM_ALNUM) st_hist[NL]++; // uncommitted
      else if(onebit(c)) st_hist[logbase2(c)]++;
      else st_hist[NL+1]++; // partially committed
      gsq[i][j].ne++;
      ne++;
      }
//    DEB2 printf("gsq %d,%d: entries %d-+%d\n",i,j,(int)(gsq[i][j].e0-entries),gsq[i][j].ne);
    }
  else {
    gsq[i][j].e0=0;
    gsq[i][j].ne=0;
    }
  DEB2 printf("total entries=%d\n",ne);
  assert(ne==cne);

  for(j=0;j<height;j++) for(i=0;i<width;i++) if(isclear(i,j)&&gsq[i][j].e0==0) {
      getmergerep(&ti,&tj,i,j);
      gsq[i][j].e0=gsq[ti][tj].e0;
      gsq[i][j].ne=gsq[ti][tj].ne;
      }

  for(d=0;d<ndir[gtype];d++) for(j=0;j<height;j++) for(i=0;i<width;i++) if(isstartoflight(i,j,d)) { // look for normal lights
    if(addwordfd(i,j,d)==1) st_tlf++;
    }
  for(i=0;i<nvl;i++) if(addwordfd(0,0,i+100)==1) st_vltlf++; // add in virtual lights

  for(i=0;i<ne;i++) entries[i].checking=0; // now calculate checking (and other) statistics
  for(i=0;i<nw;i++) for(j=0;j<words[i].nent;j++) words[i].e[j]->checking++;
  st_ce=0;
  for(i=0;i<ne;i++) if(entries[i].checking>1) st_ce++; // number of checked squares
  st_sc=0;st_2u=0;st_3u=0;
  for(i=0;i<nw;i++) {
    l=words[i].nent;
    st_lc[l]++;
    v=0;
    for(j=0,k=0;j<l;j++) {
      unch[j]=(words[i].e[j]->checking<=1);
      if(!unch[j]) k++; // count checked cells in word
      if(j>0&&unch[j]&&unch[j-1]           ) v|=1; // double unch?
      if(j>1&&unch[j]&&unch[j-1]&&unch[j-2]) v|=2; // triple+ unch?
      }
    if(v&1) st_2u++; // double+ unch?
    if(v&2) st_3u++; // triple+ unch?
  // k is now number of checked cells (out of l total)
    if( k   *100<l*mincheck) st_lucc[l]++,v|=4; // violation flags
    if((k-1)*100>l*maxcheck) st_locc[l]++,v|=8;
    st_lsc[l]+=k;
    if(k<st_lmnc[l]) st_lmnc[l]=k;
    if(k>st_lmxc[l]) st_lmxc[l]=k;
    st_sc+=k;
    if(words[i].ldir<ndir[gtype])
      for(j=0;j<l;j++) gsq[words[i].gx0][words[i].gy0].vflags[words[i].ldir]|=v; // update violation flags for non VLs
    }

  ne0=ne; // number of entries before implicit words added: needed for stats
  k=nw;
  if(ntw) { // if there are any treated words add in "implicit" words as needed
    for(i=0;i<NMSG;i++) {
      for(j=0,l=0;j<k;j++) if(words[j].lp->ten) {
        if(treatorder[i]>0) entries[ne].flbm=treatcstr[i][l];
        else                entries[ne].flbm=ABM_ALL;
        entries[ne].sel=1; // make sure we fill even if only in "fill selection" mode
        entries[ne].checking=2;
        if(l==MXFL) st_tmtlf=1; // too many treated lights for filler to use discretion
        if(l<MXFL) {
          treatmsge0[i][l]=entries+ne;
          words[nw].e[l++]=entries+ne; // build up new implicit word
          }
        words[j].e[words[j].nent]=entries+ne; // add tag to end of word in grid
        words[j].nent++; // note that we do not increment jlen here
        if(!onebit(entries[ne].flbm)) words[j].fe=0; // not fully entered if any ambiguity
        ne++;
        }
      words[nw].nent=l;
      words[nw].fe=0;
      words[nw].lp=msglprop+i;
      memset(msglprop+i,0,sizeof(struct lprop));
      msglprop[i].dmask=1<<(MAXNDICTS+i); // special dictionary mask flag value
      strncpy(msgword[i],treatmsgAZ09[i],l); // truncate message if necessary
      msgword[i][l]=0;
      if(treatorder[i]==0) { // first come first served mode
        msglprop[i].emask=EM_FWD;
        words[nw].wlen=strlen(msgword[i]);
      } else if(treatorder[i]==1) { // "spread" mode
        msglprop[i].emask=EM_SPR;
        words[nw].wlen=strlen(msgword[i]);
      } else {
        msglprop[i].emask=EM_ALL;
        for(j=strlen(msgword[i]);j<l;j++) msgword[i][j]='-'; // pad message if necessary
        words[nw].wlen=l;
        words[nw].jlen=l;
        }
      if(l&&treatorder[i]>0) nw++; // don't create an empty word, and don't create one for first come first served entry
      }
    }

  donumbers();
  DEB8 printf("nw=%d ntw=%d nc=%d ne=%d\n",nw,ntw,nc,ne);
  return 0;
  }

// symmetry functions
// call f on (x,y) if in range
static void symmdo5(void f(int,int,int,int),int k,int x,int y,int d) {
  if(isingrid(x,y)) f(k,x,y,d);
  }

// call symmdo5 on (x,y) and other square implied by up-and-down symmetry flag (if any)
static void symmdo4(void f(int,int,int,int),int k,int x,int y,int d) {
  if(symmd&2) switch(gshape[gtype]) {
case 0: case 1: case 2:
    symmdo5(f,k,x,(y+(height+1)/2)%((height+1)&~1),d);break;
case 3: case 4: break; // not for circular grids
    }
  symmdo5(f,k,x,y,d);
  }

// call symmdo4 on (x,y) and other square implied by left-and-right symmetry flag (if any)
static void symmdo3(void f(int,int,int,int),int k,int x,int y,int d) {
  if(symmd&1) switch(gshape[gtype]) { 
case 0: case 1: case 2:
    symmdo4(f,k,(x+(width+1)/2)%((width+1)&~1),y,d);break;
case 3: case 4: break; // not for circular grids
    }
  symmdo4(f,k,x,y,d);
  }

// call symmdo3 on (x,y) and other square implied by vertical mirror flag (if any)
static void symmdo2(void f(int,int,int,int),int k,int x,int y,int d) {int h;
  h=height;
  if(gshape[gtype]==1&&ODD(width)&&ODD(x)) h--;
  if(symmm&2) switch(gshape[gtype]) {
case 0: case 1: case 2:
    symmdo3(f,k,x,h-y-1,dvflip[gtype][d]);break;
case 3:
    if(width%2) break; // not for odd-size circular grids
    symmdo3(f,k,(width*3/2-x-1)%width,y,dvflip[gtype][d]);
    break;
case 4:
    if(width%2) break; // not for odd-size circular grids
    symmdo3(f,k,(width*3/2-x)%width,y,dvflip[gtype][d]);
    break;
    } 
  symmdo3(f,k,x,y,d);
  }

// call symmdo2 on (x,y) and other square implied by horizontal mirror flag (if any)
static void symmdo1(void f(int,int,int,int),int k,int x,int y,int d) {int w;
  w=width;
  if(gshape[gtype]==2&&ODD(height)&&ODD(y)) w--;
  if(symmm&1) switch(gtype) {
case 0: case 1: case 2: case 3:
    symmdo2(f,k,w-x-1,y,dhflip[gtype][d]);break;
case 4:
    symmdo2(f,k,(w-x)%w,y,dhflip[gtype][d]);break;
    break;
    } 
  symmdo2(f,k,x,y,d);
  }

// get centre of rotation in 6x h-units, 4x v-units (gshape[gtype]=1; mutatis mutandis for gshape[gtype]=2)
static void getcrot(int*cx,int*cy) {int w,h;
  w=width;h=height;
  if(gshape[gtype]==1) {
    *cx=(w-1)*3;
    if(EVEN(w)) *cy=h*2-1;
    else        *cy=h*2-2;
  } else {
    *cy=(h-1)*3;
    if(EVEN(h)) *cx=w*2-1;
    else        *cx=w*2-2;
    }
  }

// rotate by d/6 of a revolution in 6x h-units, 4x v-units (gshape[gtype]=1; mutatis mutandis for gshape[gtype]=2)
static void rot6(int*x0,int*y0,int x,int y,int d) {int u,v;
  u=1;v=1;
  switch(d) {
case 0:u= x*2    ;v=   2*y;break;
case 1:u= x  -3*y;v= x+  y;break;
case 2:u=-x  -3*y;v= x-  y;break;
case 3:u=-x*2    ;v=  -2*y;break;
case 4:u=-x  +3*y;v=-x-  y;break;
case 5:u= x  +3*y;v=-x+  y;break;
    }
  assert(EVEN(u));assert(EVEN(v));
  *x0=u/2;*y0=v/2;
  }

// call f on (x,y) and any other squares implied by symmetry flags by
// calling symmdo1 on (x,y) and any other squares implied by rotational symmetry flags
void symmdo(void f(int,int,int,int),int k,int x,int y,int d) {int i,mx,my,x0,y0;
  switch(gshape[gtype]) {
case 0:
    switch(symmr) {
    case 4:symmdo1(f,k,width-y-1,x,         (d+1)%4);
           symmdo1(f,k,y,        height-x-1,(d+3)%4);
    case 2:symmdo1(f,k,width-x-1,height-y-1,(d+2)%4);
    case 1:symmdo1(f,k,x,        y,          d);
      }
    break;
case 1:
    getcrot(&mx,&my);
    y=y*4+(x&1)*2-my;x=x*6-mx;
    for(i=5;i>=0;i--) {
      if((i*symmr)%6==0) {
        rot6(&x0,&y0,x,y,i);
        x0+=mx;y0+=my;
        assert(x0%6==0); x0/=6;
        y0-=(x0&1)*2;
        assert(y0%4==0); y0/=4;
        symmdo1(f,k,x0,y0,(d+i)%6);
        }
      }
    break;
case 2:
    getcrot(&mx,&my);
    x=x*4+(y&1)*2-mx;y=y*6-my;
    for(i=5;i>=0;i--) {
      if((i*symmr)%6==0) {
        rot6(&y0,&x0,y,x,i);
        x0+=mx;y0+=my;
        assert(y0%6==0); y0/=6;
        x0-=(y0&1)*2;
        assert(x0%4==0); x0/=4;
        symmdo1(f,k,x0,y0,(d-i+6)%6);
        }
      }
    break;
case 3: case 4:
    if(symmr==0) break; // assertion
    for(i=symmr-1;i>=0;i--) symmdo1(f,k,(x+i*width/symmr)%width,y,d);
    break;
    }
  }

// basic grid editing commands (candidates for f() in symmdo above)
void a_editblock (int k,int x,int y,int d) {int l,gx[MXCL],gy[MXCL];
  l=getmergegroup(gx,gy,x,y);
  gsq[x][y].fl=(gsq[x][y].fl&0x06)|1;
  demerge(x,y);
  }

void a_editempty (int k,int x,int y,int d) {
  gsq[x][y].fl= gsq[x][y].fl&0x16;
  }

void a_editcutout(int k,int x,int y,int d) {int l,gx[MXCL],gy[MXCL];
  l=getmergegroup(gx,gy,x,y);
  gsq[x][y].fl=(gsq[x][y].fl&0x06)|8;
  demerge(x,y);
  }

// set bar state in direction d to k
void a_editbar(int k,int x,int y,int d) {int tx,ty;
  if(!isingrid(x,y)) return;
  tx=x;ty=y;
  stepforw(&tx,&ty,d);
//  printf("<%d,%d %d,%d %d>\n",x,y,tx,ty,d);
  if(!isingrid(tx,ty)) return;
  setbars(x,y,d,k);
  donumbers();
  }

// set merge state in direction d to k, deal with consequences
void a_editmerge(int k,int x,int y,int d) {int f,i,l,tl,tx,ty,gx[MXCL],gy[MXCL],tgx[MXCL],tgy[MXCL];
  if(!isclear(x,y)) return;
  tx=x;ty=y;
  stepforw(&tx,&ty,d);
  if(!isclear(tx,ty)) return;
  l=getmergegroup(gx,gy,x,y);
  tl=getmergegroup(tgx,tgy,tx,ty);
  if(k) for(i=0;i<ndir[gtype];i++) if(i!=d&&i+ndir[gtype]!=d) {
    setmerge(x,y,i,0);
    setmerge(x,y,i+ndir[gtype],0);
    setmerge(tx,ty,i,0);
    setmerge(tx,ty,i+ndir[gtype],0);
    }
  setmerge(x,y,d,k);
  f=gsq[x][y].fl&16; // make selection flags consistent
  l=getmergegroup(gx,gy,x,y);
  for(i=0;i<l;i++) gsq[gx[i]][gy[i]].fl=(gsq[gx[i]][gy[i]].fl&~16)|f;
  donumbers();
  }





// PREFERENCES

int prefdata[NPREFS]={0,0,0,66,75, 1,0,36,36,0};

static char*prefname[NPREFS]={ // names as used in preferences file
  "edit_click_for_block", "edit_click_for_bar", "edit_show_numbers",
  "stats_min_check_percent", "stats_max_check_percent",
  "autofill_no_duplicates", "autofill_random",
  "export_EPS_square_points","export_HTML_square_pixels",
  "light_numbers_in_solutions" };

static int prefminv[NPREFS]={0,0,0,0,0,0,0,10,10,0}; // legal range
static int prefmaxv[NPREFS]={1,1,1,100,100,1,2,72,72,1};

// read preferences from file
// fail silently
static void loadprefs(void) {FILE*fp;
  char s[SLEN],t[SLEN];
  int i,u;
  
	#ifdef _WIN32		// Folder for preferences file
		if(SUCCEEDED(SHGetFolderPath(NULL, CSIDL_APPDATA, NULL, 0, s))) {
			strcat(s,"\\Qxw\\Qxw.ini"); 
			}
		else return;
	#else
		struct passwd*p;
		p=getpwuid(getuid());
		if(!p) return;
		if(strlen(p->pw_dir)>SLEN-20) return;
		strcpy(s,p->pw_dir);
		strcat(s,"/.qxw/preferences");
  #endif

  DEB1 printf("loadprefs %s\n",s);
  fp=fopen(s,"r");if(!fp) return;
  while(fgets(s,SLEN,fp)) {
    if(sscanf(s,"%s %d",t,&u)==2) {
      for(i=0;i<NPREFS;i++)
        if(!strcmp(t,prefname[i])) {
          if(u<prefminv[i]) u=prefminv[i];
          if(u>prefmaxv[i]) u=prefmaxv[i];
          prefdata[i]=u;
          }
      }
    }
  fclose(fp);
  }

// write preferences to file
// fail silently
void saveprefs(void) {FILE*fp;
  int i;
  char s[SLEN];
  
	#ifdef _WIN32		// Preferences in app data folder
		if(SUCCEEDED(SHGetFolderPath(NULL, CSIDL_APPDATA, NULL, 0, s))) {
			strcat(s,"\\Qxw"); 
			_mkdir(s);   
			strcat(s,"\\Qxw.ini"); 
		  }
		else return;
	#else
		struct passwd*p;
		p=getpwuid(getuid());
		if(!p) return;
		if(strlen(p->pw_dir)>SLEN-20) return;
		strcpy(s,p->pw_dir);
		strcat(s,"/.qxw");
		mkdir(s,0777);
		strcat(s,"/preferences");
	#endif

  DEB1 printf("saveprefs %s\n",s);
  fp=fopen(s,"w");if(!fp) return;
  for(i=0;i<NPREFS;i++) if(fprintf(fp,"%s %d\n",prefname[i],prefdata[i])<0) break;
  fclose(fp);
  }




static char*defaultmk(k) {return k?"":"\\#";} // default mark is just number in NW corner

void make7bitclean(char*s) {
  char*t;
  for(t=s;*s;s++) if(*s>=0x20&&*s<0x7f) *t++=*s;
  *t=0;
  }

// as abmtocs below, but just do letters or digits
char*abmtocs0(char*s,ABM b) {
  ABM b0,b1;
  char c0,c1;
  while(b) {
    b0=b&~(b-1); // bottom 1
    b1=b&~(b+b0); // bottom run of 1:s
    c0=ltochar[logbase2(b0)];
    c1=ltochar[logbase2(b0+b1)-1];
    *s++=c0;
    if(c0!=c1) {
      if(c1-c0>1) *s++='-';
      *s++=c1;
      }
    b&=~b1;
    }
  return s;
  }

// convert ABM to choice string of the form [a-eghq-z0-39], using inverting notation if inv==1 and
// including "-" if dash==1
void abmtocs(char*s,ABM b,int inv,int dash) {
  *s++='[';
  if(inv) *s++='^',b^=dash?ABM_ALL:ABM_ALNUM;
  s=abmtocs0(s,b&ABM_LET);
  s=abmtocs0(s,b&ABM_NUM);
  if(dash&&(b&ABM_DASH)) *s++='-';
  *s++=']';
  *s++=0;
  }

// convert ABM to string of the form "s" or "[aeiou]" or whatever
// NL+4 is a very safe upper bound on the resulting length
void abmtostr(char*s,ABM b,int dash) {
  char s0[NL+4],s1[NL+4];
  b&=dash?ABM_ALL:ABM_ALNUM;
  if(b==ABM_ALL) {strcpy(s,"?"); return;}
  if(b==ABM_ALNUM) {strcpy(s,"."); return;}
  if(b==ABM_VOW) {strcpy(s,"@"); return;}
  if(b==ABM_CON) {strcpy(s,"#"); return;}
  if(onebit(b)) {s[0]=ltochar[logbase2(b)]; s[1]=0; return;}
  abmtocs(s0,b,0,dash);
  abmtocs(s1,b,1,dash);
  if(strlen(s0)<=strlen(s1)) strcpy(s,s0); // choose shorter of the two representations
  else                       strcpy(s,s1);
  }

// convert sequence of ABMs to string in compact format suitable for display: concantenates results of abmtoechar()
void abmstodispstr(char*s,ABM*b,int l) {int i;
  for(i=0;i<l;i++) s[i]=abmtoechar(*b++);
  s[l]=0;
  }

// convert sequence of ABMs to string: concantenates results of abmtostr()
void abmstostr(char*s,ABM*b,int l,int dash) {int i;
  *s=0;
  for(i=0;i<l;i++) abmtostr(s,*b++,dash),s+=strlen(s);
  }

int strtoabms(ABM*p,int l,char*s,int dash) {
  int c,c0,c1,f,i;
  ABM b;

  for(i=0;*s&&i<l;i++) {
    if (isalnum(*s)||(dash&&*s=='-')) b=chartoabm[(int)*s],s++;
    else if(*s=='?') b=ABM_ALL,s++;
    else if(*s=='.') b=ABM_ALNUM,s++;
    else if(*s==' ') b=ABM_ALNUM,s++;
    else if(*s=='@') b=ABM_VOW,s++;
    else if(*s=='#') b=ABM_CON,s++;
    else if(*s=='[') {
      s++;
      f=0; b=0;
      if(*s=='^') f=1,s++;
      for(;;) {
        if(dash&&*s=='-') {b|=ABM_DASH; s++; continue;}
        if(!isalnum(*s)) break;
        c0=c1=chartol[(int)s[0]];
        if(s[1]=='-'&&isalnum(s[2])) {
          c=chartol[(int)s[2]];
          if(c>c0) c1=c; else c0=c;
          s+=2;
          }
        for(c=c0;c<=c1;c++) b|=1ULL<<c;
        s++;
        }
      if(*s==']') s++;
      if(f) b^=dash?ABM_ALL:ABM_ALNUM; // negation flag
      }
    else break;
    b&=dash?ABM_ALL:ABM_ALNUM;
    p[i]=b;
    }
  return i;
  }

static void resetsp(struct sprop*p) {int k;
  p->bgcol=0xffffff;
  p->fgcol=0x000000;
  p->mkcol=0x000000;
  p->fstyle=0;
  p->dech=0;
  p->ten=0;
//  for(k=0;k<MAXNMK;k++) sprintf(p->mk[k],"%c",k+'a'); // for testing
  for(k=0;k<MAXNMK;k++) strcpy(p->mk[k],defaultmk(k));
  p->spor=0;
  }

void resetlp(struct lprop*p) {
  p->dmask=1;
  p->emask=EM_FWD;
  p->ten=0;
  p->dnran=0;
  p->lpor=0;
  }

static void resetstate(void) {int i,j,k,u0,u1;
  for(i=0;i<MXSZ;i++) for(j=0;j<MXSZ;j++) {
    for(k=0;k<MAXNDIR;k++) gsq[i][j].ctlen[k]=0;
    gsq[i][j].ctlen[0]=1;
    gsq[i][j].ctbm[0][0]=ABM_ALNUM;
    gsq[i][j].fl=0;gsq[i][j].bars=0;gsq[i][j].merge=0;gsq[i][j].dsel=0;
    resetsp(&gsq[i][j].sp);
    for(k=0;k<MAXNDIR;k++) resetlp(&gsq[i][j].lp[k]);
    }
  resetsp(&dsp);
  resetlp(&dlp);
  for(i=0;i<NVL;i++) vls[i].l=0,vls[i].sel=0;
  for(i=0;i<NVL;i++) resetlp(&vls[i].lp);
  for(i=0;i<NVL;i++) for(j=0;j<MXCL;j++) vls[i].x[j]=0,vls[i].y[j]=0;
  nvl=0;
  treatmode=0;
  for(i=0;i<NMSG;i++) {
    treatorder[i]=0;
    treatmsg[i][0]=0;
    for(j=0;j<MXFL;j++) treatcstr[i][j]=ABM_ALL;
    }
  tpifname[0]=0;
  tambaw=0;
  unsaved=0;
  bldstructs();
  // generate "codeword" permutation
  for(i=0;i<26;i++) cwperm[i]=i;
  for(i=0;i<1000;i++) {u0=rand()%26; u1=rand()%26; j=cwperm[u0]; cwperm[u0]=cwperm[u1]; cwperm[u1]=j;}
  for(i=0;i<10;i++) cwperm[i+26]=i;
  for(i=0;i<1000;i++) {u0=rand()%10+26; u1=rand()%10+26; j=cwperm[u0]; cwperm[u0]=cwperm[u1]; cwperm[u1]=j;}
  }

// tidy up square properties structure
static void fixsp(struct sprop*sp) {
  int k;

  sp->bgcol&=0xffffff;
  sp->fgcol&=0xffffff;
  sp->mkcol&=0xffffff;
//  if(sp->fstyle<0) sp->fstyle=0;
  if(sp->fstyle>3) sp->fstyle=3;
  sp->ten=!!sp->ten;
//  if(sp->dech<0) sp->dech=0;
  if(sp->dech>2) sp->dech=2;
  sp->spor=!!sp->spor;
  for(k=0;k<MAXNMK;k++) make7bitclean(sp->mk[k]);
  }

// tidy up light properties structure
static void fixlp(struct lprop*lp) {
  lp->dmask&=(1<<MAXNDICTS)-1;
  lp->emask&=(1<<NLEM)-1;
  if(lp->emask==0) lp->emask=EM_FWD;
  lp->ten=!!lp->ten;
  lp->lpor=!!lp->lpor;
  lp->dnran=!!lp->dnran;
  }

// FILE SAVE/LOAD

// flags b23..16: grid width
// flags  b15..8: grid height
// flags   b7..0: grain
void a_filenew(int flags) {
  char*p;
  int u,v,x,y;

  resetstate();
  u=(flags>>16)&0xff;
  v=(flags>>8)&0xff;
  if(u>0&&u<=MXSZ&&v>0&&v<MXSZ) {
    gtype=0,width=u,height=v;
    if((flags&0x80)==0) { // pre-fill with blocks?
      u=!!(flags&1); v=!!(flags&2);
      for(x=0;x<width;x++) for(y=0;y<height;y++) if((x+u)&(y+v)&1) gsq[x][y].fl|=1;
      }
    }
  if(!strcmp(filenamebase,"")) {   // brand new file
#ifdef _WIN32
    if(SHGetFolderPath(NULL, CSIDL_MYDOCUMENTS, NULL, 0, filenamebase)==S_OK) strcat(filenamebase,"\\");
    else strcpy(filenamebase,"");
#else
    struct passwd*pw;
    pw=getpwuid(getuid());
    if(!pw||strlen(pw->pw_dir)>SLEN-20) strcpy(filenamebase,"");
    else                                strcpy(filenamebase,pw->pw_dir),strcat(filenamebase,"/");;
#endif
  } else { // we have a path to start from
    p=strrchr(filenamebase,DIR_SEP_CHAR);
    if(p) strcpy(p,DIR_SEP_STR);
    else strcpy(filenamebase,"");
    }
  strcat(filenamebase,"untitled");
  donumbers();
  unsaved=0;
  }

extern char*optarg;
extern int optind,opterr,optopt;

int main(int argc,char*argv[]) {

	int i,nd;

	srand((int)time(0));
	for(i=0;i<26;i++) ltochar[i]   =i+'A',chartol[i   +'A']=i,chartol[i+'a']=i,chartoabm[i   +'A']=1ULL<<i,chartoabm[i+'a']=1ULL<<i;
	for(i=0;i<10;i++) ltochar[i+26]=i+'0',chartol[i   +'0']=i+26              ,chartoabm[i   +'0']=1ULL<<(i+26);
	ltochar[36]  =  '-',chartol[(int)'-']=36                ,chartoabm[(int)'-']=1ULL<<36;
	log2lut[0]=log2lut[1]=0; for(i=2;i<65536;i+=2) { log2lut[i]=log2lut[i/2]+1; log2lut[i+1]=0; }
	resetstate();
	for(i=0;i<MAXNDICTS;i++) dfnames[i][0]='\0';
	for(i=0;i<MAXNDICTS;i++) strcpy(dsfilters[i],"");
	for(i=0;i<MAXNDICTS;i++) strcpy(dafilters[i],"");
	freedicts();

	nd=0;
	i=0;
	for(;;) switch(getopt(argc,argv,"d:?D:")) {
		case -1: goto ew0;
		case 'd':
			 if(strlen(optarg)<SLEN&&nd<MAXNDICTS) strcpy(dfnames[nd++],optarg);
			 break;
		case 'D':debug=atoi(optarg);break;
		case '?':
		default:i=1;break;
	}

ew0:
	if(i) {
		printf("Usage: %s [-d <dictionary_file>]* [qxw_file]\n",argv[0]);
		printf("This is Qxw, release %s.\n\n\
				Copyright 2011-2014 Mark Owen; Windows port by Peter Flippant\n\
				\n\
				This program is free software; you can redistribute it and/or modify\n\
				it under the terms of version 2 of the GNU General Public License as\n\
				published by the Free Software Foundation.\n\
				\n\
				This program is distributed in the hope that it will be useful,\n\
				but WITHOUT ANY WARRANTY; without even the implied warranty of\n\
				MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the\n\
				GNU General Public License for more details.\n\
				\n\
				You should have received a copy of the GNU General Public License along\n\
				with this program; if not, write to the Free Software Foundation, Inc.,\n\
				51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.\n\
				\n\
				For more information visit http://www.quinapalus.com or\n\
				e-mail qxw@quinapalus.com\n\n\
				",RELEASE);
		return 0;
	}


	a_filenew(0); // reset grid
	loadprefs(); // load preferences file (silently failing to defaults)
	nd = 1;
	strcpy(dfnames[0], "all_dict");
	loaddicts(0);

	read_grid(stdin);

	bldstructs();
	filler_init(1);
	filler_search();
	accept_hints();
	print_grid();
	filler_destroy();
	freedicts();
	return 0;
}




// INTERFACE TO FILLER

// (re-)start the filler when an edit has been made
// Return non-zero if cannot start filler
int compute(int mode) {
  if(bldstructs()) return 1; // failed?
  return 0;
  }

// comparison function for sorting feasible word list by score
static int cmpscores(const void*p,const void*q) {double f,g;
  f=ansp[lts[*(int*)p].ans]->score; // negative ans values cannot occur here
  g=ansp[lts[*(int*)q].ans]->score;
  if(f<g) return  1;
  if(f>g) return -1;
  return (char*)p-(char*)q; // stabilise sort
  }

// called by filler when a list of feasible words through the cursor has been found
void mkfeas(void) {int l,x,y; int*p; struct word*w=0;
  llistp=NULL;llistn=0; // default answer: no list
  if(dir<ndir[gtype]) {if(getstartoflight(&x,&y,curx,cury,dir)>0) w=gsq[x][y].w[dir];}
  else if(dir>=100&&dir<100+NVL) w=vls[dir-100].w;
  DEB2 printf("mkfeas: %d,%d (d=%d) ->%d,%d, w=%p\n",curx,cury,dir,x,y,w);
  if(w==0||w->flist==0) {llistp=NULL;llistn=0;return;} // no list
  p=w->flist;
  l=w->flistlen;
  if(llist) free(llist);
  llist=(int*)malloc(l*sizeof(int)); // space for feasible word list
  if(llist==NULL) return;
  memcpy(llist,p,l*sizeof(int));
  llistp=llist;
  llistn=l;
  llistwlen=w->wlen;
  llistdm=w->lp->dmask; // copy list across
  llistem=w->lp->emask;
  DEB1 printf("llist=%p p=%p l=%d dm=%08x llistwlen=%d llistdm=%08x\n",llist,p,l,w->lp->dmask,llistwlen,llistdm);
  qsort(llistp,llistn,sizeof(int),&cmpscores);
//  DEB1 printf("mkfeas: %d matches; dm=%08x\n",llistn,llistdm);
  }

// provide progress info to display
void updategrid(void) {int i;
  for(i=0;i<ne;i++) entries[i].flbmh=entries[i].flbm; // make back-up copy of hints
  }

int read_grid(FILE *fp)
{
	char *line = NULL;
	size_t len = 0;
	size_t grid_w = 0, grid_h = 0;
	ssize_t read;
	char *grid;
	int x, y;

	resetstate();

	while ((read = getline(&line, &len, fp)) > 0) {
		grid = line;

		/* trim whitespace */
		while (*grid && isspace(*grid))
			grid++;
		while (strlen(grid) && isspace(grid[strlen(grid)-1]))
			grid[strlen(grid)-1] = '\0';

		if (!strlen(grid))
			continue;

		if (!grid_w)
			grid_w = strlen(grid);

		if (strlen(grid) != grid_w)
			continue;

		y = grid_h;
		grid_h++;

		if (grid_w >= MXSZ || grid_h >= MXSZ)
			goto err;

		/*
		 * ok, grid_h / grid_w are valid, now set letters
		 * and blocks based on grid values
		 */
		for (x = 0; x < grid_w; x++) {
			if (grid[x] == '#') {
				gsq[x][y].fl |= 1;
				gsq[x][y].merge = 0;
			} else if (grid[x] != '.') {
				gsq[x][y].ctbm[0][0] = chartoabm[(int)grid[x]];
			}
		}
	}
	width = grid_w;
	height = grid_h;
	printf("width: %d height: %d\n", width, height);
	return 0;
err:
	width = height = 0;
	printf("E width: %d height: %d\n", width, height);
	return -EIO;
}

void accept_hints()
{
	int de,nd,f,i,j,x,y;
	ABM m;
	struct entry *e;
	ABM b;

	for(x=0;x<width;x++) {
		for(y=0;y<height;y++) {
			e = gsq[x][y].e0;
			if (!e)
				continue;
			de=getdech(x,y);
			if(de==0) nd=1,de=1; else nd=ndir[gtype];
			for(i=0;i<nd;i++) {
				for(j=0;j<gsq[x][y].ctlen[i];j++) {
					m=e->flbmh; // get hints bitmap
					if(onebit(m)) gsq[x][y].ctbm[i][j]=m; // could consider removing the onebit() test but potentially confusing?
					e++;
				}
			}
		}
	}
}

void print_grid()
{
	char ch;
	int m, k;

	int i, j;
	for (j = 0; j < height; j++) {
		for(i = 0; i < width; i++) {
			if (!isingrid(i, j) || !isownmergerep(i, j))
				continue;

			ch = getechar(i, j);
			if (ch == ' ')
				ch = gsq[i][j].fl ? '#' : '?';

			printf("%c", ch);
		}
		printf("\n");
	}

#if 0
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
		}
	}
#endif
}
