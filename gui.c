// $Id: gui.c 552 2014-03-04 15:16:01Z mo $

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



// GTK

#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#include <cairo.h>
#include <errno.h>

#include "common.h"
#include "qxw.h"
#include "filler.h"
#include "dicts.h"
#include "gui.h"
#include "draw.h"

int selmode=0;
int nsel=0; // number of things selected
int pxsq;
int zoomf=2;
int zoompx[]={18,26,36,52,72};
int curmf=1; // cursor has moved since last redraw

char filename[SLEN+50]; // result from filedia()
char filenamebase[SLEN]; // base to use for constructing default filenames
int havesavefn; // do we have a validated filename to save to?

static char*gtypedesc[NGTYPE]={
  "Plain rectangular",
  "Hex with vertical lights",
  "Hex with horizontal lights",
  "Circular",
  "Circular with half-cell offset",
  "Join left and right edges",
  "Join top and bottom edges",
  "Join left and right edges with flip",
  "Join top and bottom edges with flip",
  "Torus",
  };

static int spropdia(int g);
static int lpropdia(int g);
static int mvldia(int v);
static int dictldia(void);
static int treatdia(void);
static int gpropdia(void);
static int ccontdia(void);
static int prefsdia(void);
static int statsdia(void);
static int filedia(char*res,char*but,char*ext,char*filetype,int write);
static void gridchangen(void);
static void syncselmenu(void);


static GtkWidget *grid_da;
static GtkItemFactory *item_factory; // for menus
static GtkWidget *mainw,*grid_sw,*poss_label,*clist; // main window and content
static GtkWidget*stats=NULL; // statistics window (if visible)
static GtkWidget *hist_da;
static GtkWidget*(st_te[MXLE+2][5]),*(st_r[10]); // statistics table
static GtkWidget*currentdia=0; // for `Filling' dialogue box (so that we can close it automatically when filling completes)

static char*treat_lab[NATREAT][NMSG]={
  {0,                     0,                    },
  {"Keyword: ",           0,                    },
  {"Encode ABC...Z as: ", 0,                    },
  {"Key letter/word: ",   0,                    },
  {"Encodings of A: ",    0,                    },
  {"Correct letters: ",   "Incorrect letters: " },
  {"Letters to delete: ", 0,                    },
  {"Letters to delete: ", 0,                    },
  {"Letters to insert: ", 0,                    },
  {"Message 1: ",         "Message 2: "         },
  {"Correct letters: ",   0,                    },
  {"Incorrect letters: ", 0                     },
  };
static int treatpermok[NATREAT][NMSG]={
  {0,0},
  {0,0},
  {0,0},
  {0,0},
  {1,0},
  {0,0},
  {1,0},
  {1,0},
  {1,0},
  {1,1},
  {1,0},
  {1,0},
  };
static char*tnames[NATREAT]={
  " None",
  " Playfair cipher",
  " Substitution cipher",
  " Fixed Caesar/Vigenère cipher",
  " Variable Caesar cipher",
  " Misprint (general, clue order)",
  " Delete single occurrence of character",
  " Letters latent: delete all occurrences of character",
  " Insert single character",
  " Custom plug-in",
  " Misprint (correct letters specified)",
  " Misprint (incorrect letters specified)",
  };

static char*mklabel[NGTYPE][MAXNDIR*2]={
  {"NW mark: ","NE mark: ","SE mark: ","SW mark: "},
  {"NW mark: ","NE mark: ","E mark: ", "SE mark: ","SW mark: ","W mark: "},
  {"NW mark: ","N mark: ","NE mark: ", "SE mark: ","S mark: ","SW mark: "},
  {"First mark: ","Second mark: ","Third mark: ","Fourth mark: "},
  {"First mark: ","Second mark: ","Third mark: ","Fourth mark: "},
  {"NW mark: ","NE mark: ","SE mark: ","SW mark: "},
  {"NW mark: ","NE mark: ","SE mark: ","SW mark: "},
  {"NW mark: ","NE mark: ","SE mark: ","SW mark: "},
  {"NW mark: ","NE mark: ","SE mark: ","SW mark: "},
  {"NW mark: ","NE mark: ","SE mark: ","SW mark: "},
  };



// set main window title
static void setwintitle() {char t[SLEN*3];
  sprintf(t,"Qxw: %s",titlebyauthor());
  gtk_window_set_title(GTK_WINDOW(mainw),t);
  }

// set filenamebase from given string, which conventionally ends ".qxw"
void setfilenamebase(char*s) {
  strncpy(filenamebase,s,SLEN-1);filenamebase[SLEN-1]='\0';
  if(strlen(filenamebase)>=4&&!strcmp(filenamebase+strlen(filenamebase)-4,".qxw")) filenamebase[strlen(filenamebase)-4]='\0';
  }

// general question-and-answer box: title, question, yes-text, no-text
static int box(int type,const char*t,const char*u,const char*v) {
  GtkWidget*dia;
  int i;

  dia=gtk_message_dialog_new(
    GTK_WINDOW(mainw),
    GTK_DIALOG_DESTROY_WITH_PARENT,
    type,
    GTK_BUTTONS_NONE,
    "%s",t
    );
  gtk_dialog_add_buttons(GTK_DIALOG(dia),u,GTK_RESPONSE_ACCEPT,v,GTK_RESPONSE_REJECT,NULL);
  currentdia=dia;
  i=gtk_dialog_run(GTK_DIALOG(dia));
  currentdia=0;
  gtk_widget_destroy(dia);
  return i==GTK_RESPONSE_ACCEPT;
  }

// box for information purposes only
static void okbox(const char*t) {box(GTK_MESSAGE_INFO,t,GTK_STOCK_CLOSE,0);}

// box for errors, with `details' option
void reperr(const char*s) {box(GTK_MESSAGE_ERROR,s,GTK_STOCK_CLOSE,0);}
void fsgerr() {box(GTK_MESSAGE_ERROR,"Filing system error",GTK_STOCK_CLOSE,0);}
void fserror() {char s[SLEN],t[SLEN*2];
  #ifdef _WIN32
    if(strerror_s(s,SLEN,errno)) strcpy(s,"general error");  // Windows version of threadsafe strerror()
  #else
    if(strerror_r(errno,s,SLEN)) strcpy(s,"general error");
  #endif
  sprintf(t,"Filing system error: %s",s);
  reperr(t);
  }

static int areyousure(char*action) { // general-purpose are-you-sure dialogue
  char s[1000];
  sprintf(s,"\n  Your work is not saved.  \n  Are you sure you want to %s?  \n",action);
  return box(GTK_MESSAGE_QUESTION,s,"  Proceed  ",GTK_STOCK_CANCEL);
  }


// GTK MENU HANDLERS

// simple menu handlers

static int checkoverwrite() {FILE*fp;
  fp=fopen(filename,"r");
  if(!fp) return 1;
  fclose(fp);
  return box(GTK_MESSAGE_QUESTION,"\n  That file already exists.  \n  Overwrite it?  \n","  Overwrite  ",GTK_STOCK_CANCEL);
  }

static void m_filenew(GtkWidget*w,gpointer data)      {
  if(!unsaved||areyousure("start again")) {
    a_filenew((intptr_t)data);
    strcpy(filenamebase,"");
    setwintitle();
    syncgui();
    compute(0);
    }
  }

static void m_fileopen(GtkWidget*w,gpointer data)     {
  if(!unsaved||areyousure("proceed")) if(filedia(0,"Open",".qxw","Qxw",0)) {
    a_load();
    setwintitle();
    syncgui();
    compute(0);
    }
  }

static void m_filesaveas(GtkWidget*w,gpointer data)   {
  if(filedia(0,"Save",".qxw","Qxw",1)&&checkoverwrite()) {
    a_save();
    setwintitle();
    }
  }

static void m_filesave(GtkWidget*w,gpointer data)     {
//  printf("m_filesave: filenamebase=%s havesavefn=%d\n",filenamebase,havesavefn);
  if(havesavefn&&strcmp(filenamebase,"")) {
    strcpy(filename,filenamebase);
    strcat(filename,".qxw");
    a_save();
    }
  else                        m_filesaveas(w,data);
  }

static void m_exportvls(GtkWidget*w,gpointer data)    {char t[SLEN+50]; if(filedia(t,"Export free light paths",".fl.txt","Plain text",1)) a_exportvls(t);}
static void m_importvls(GtkWidget*w,gpointer data)    {char t[SLEN+50]; if(filedia(t,"Import free light paths",".fl.txt","Plain text",0)) a_importvls(t);}

static void m_filequit(void)                          {if(!unsaved||areyousure("quit")) gtk_main_quit();}
static void m_undo(GtkWidget*w,gpointer data)         {if((uhead+UNDOS-1)%UNDOS==utail) return;undo_pop();gridchangen();syncgui();}
static void m_redo(GtkWidget*w,gpointer data)         {if(uhead==uhwm) return;uhead=(uhead+2)%UNDOS;undo_pop();gridchangen();syncgui();}
static void m_editgprop(GtkWidget*w,gpointer data)    {gpropdia();}
static void m_dsprop(GtkWidget*w,gpointer data)       {spropdia(1);}
static void m_sprop(GtkWidget*w,gpointer data)        {spropdia(0);}
static void m_dlprop(GtkWidget*w,gpointer data)       {lpropdia(1);}
static void m_lprop(GtkWidget*w,gpointer data)        {lpropdia(0);}
static void m_cellcont(GtkWidget*w,gpointer data)     {ccontdia();}
static void m_afctreat(GtkWidget*w,gpointer data)     {treatdia();}
static void m_editprefs(GtkWidget*w,gpointer data)    {prefsdia();}
static void m_showstats(GtkWidget*w,gpointer data)    {statsdia();}
static void m_symm0(GtkWidget*w,gpointer data)        {symmr=(intptr_t)data&0xff;}
static void m_symm1(GtkWidget*w,gpointer data)        {symmm=(intptr_t)data&0xff;}
static void m_symm2(GtkWidget*w,gpointer data)        {symmd=(intptr_t)data&0xff;}

static void m_zoom(GtkWidget*w,gpointer data) {int u;
  u=(intptr_t)data;
  if(u==-1) zoomf++;
  else if(u==-2) zoomf--;
  else zoomf=u;
  if(zoomf<0) zoomf=0;
  if(zoomf>4) zoomf=4;
  pxsq=zoompx[zoomf];
  syncgui();
  }

static void m_fileexport(GtkWidget*w,gpointer data)  {
  switch((intptr_t)data) {
  case 0x401: if(filedia(0,"Export blank grid as EPS",".blank.eps","Encapsulated PostScript",1)) a_exportg(filename,4,0);break;
  case 0x402: if(filedia(0,"Export blank grid as SVG",".blank.svg","Scalable Vector Graphics",1)) a_exportg(filename,4,1);break;
  case 0x403: if(filedia(0,"Export blank grid as PNG",".blank.png","Portable Network Graphics",1)) a_exportg(filename,4,2);break;
  case 0x404:
    if(gshape[gtype]!=0) break;
    if(filedia(0,"Export blank grid as HTML",".blank.html","HyperText Markup Language",1)) a_exportgh(0x05,"");
    break;
  case 0x411: if(filedia(0,"Export filled grid as EPS",".eps","Encapsulated PostScript",1)) a_exportg(filename,lnis?6:2,0);break;
  case 0x412: if(filedia(0,"Export filled grid as SVG",".svg","Scalable Vector Graphics",1)) a_exportg(filename,lnis?6:2,1);break;
  case 0x413: if(filedia(0,"Export filled grid as PNG",".png","Portable Network Graphics",1)) a_exportg(filename,lnis?6:2,2);break;
  case 0x414:
    if(gshape[gtype]!=0) break;
    if(filedia(0,"Export filled grid as HTML",".html","HyperText Markup Language",1)) a_exportgh(lnis?7:3,"");
    break;
  case 0x420: if(filedia(0,"Export answers as plain text",".ans.txt","Plain text",1)) a_exporta(0);break;
  case 0x423: if(filedia(0,"Export answers as HTML",".ans.html","HyperText Markup Language",1)) a_exporta(1);break;
  case 0x433:
    if(gshape[gtype]!=0) break;
    if(filedia(0,"Export puzzle as HTML",".html","HyperText Markup Language",1)) a_exportgh(0x0d,"");
    break;
  case 0x434:if(filedia(0,"Export puzzle as HTML+SVG",".html","HyperText Markup Language",1)) a_exporthp(0,4,1);break;
  case 0x435:if(filedia(0,"Export puzzle as HTML+PNG",".html","HyperText Markup Language",1)) a_exporthp(0,4,2);break;
  case 0x443:
    if(gshape[gtype]!=0) break;
    if(filedia(0,"Export solution as HTML",".html","HyperText Markup Language",1)) a_exportgh(0x13,"");
    break;
  case 0x444:if(filedia(0,"Export solution as HTML+SVG",".html","HyperText Markup Language",1)) a_exporthp(1,lnis?6:2,1);break;
  case 0x445:if(filedia(0,"Export solution as HTML+PNG",".html","HyperText Markup Language",1)) a_exporthp(1,lnis?6:2,2);break;
  default: assert(0);
    }
  }

static void selchange(void) {int i,j,d;
  nsel=0;
  if     (selmode==0) for(i=0;i<width;i++) for(j=0;j<height;j++) {if(gsq[i][j].fl&16) nsel++;}
  else if(selmode==1) for(i=0;i<width;i++) for(j=0;j<height;j++) for(d=0;d<ndir[gtype];d++) {if(isstartoflight(i,j,d)&&(gsq[i][j].dsel&(1<<d))) nsel++;}
  else if(selmode==2) for(i=0;i<nvl;i++) {if(vls[i].sel) nsel++;}
  syncselmenu();
  refreshsel();
  }

static void selcell(int x,int y,int f) {int i,l,gx[MXCL],gy[MXCL];
  l=getmergegroup(gx,gy,x,y);
  if(f) f=16;
  for(i=0;i<l;i++) gsq[gx[i]][gy[i]].fl=(gsq[gx[i]][gy[i]].fl&~16)|f;
  }

static void selnocells() {int i,j;
  for(i=0;i<width;i++) for(j=0;j<height;j++) gsq[i][j].fl&=~16;
  }

static void selnolights() {int i,j;
  for(i=0;i<width;i++) for(j=0;j<height;j++) gsq[i][j].dsel=0;
  }

static void selnovls() {int i;
  for(i=0;i<nvl;i++) vls[i].sel=0;
  }

// clear selection
static void m_selnone(GtkWidget*w,gpointer data) {
  selnolights();
  selnocells();
  selnovls();
  selchange();
  }

// invert selection
static void m_selinv(GtkWidget*w,gpointer data) {int d,i,j;
  for(i=0;i<width;i++) for(j=0;j<height;j++) {
    if     (selmode==0) {if(isclear(i,j)) gsq[i][j].fl^=16;}
    else if(selmode==1) for(d=0;d<ndir[gtype];d++) if(isstartoflight(i,j,d)) gsq[i][j].dsel^=1<<d;
    else if(selmode==2) for(i=0;i<nvl;i++) vls[i].sel=!vls[i].sel;
    }
  selchange();
  }

// select everything
static void m_selall(GtkWidget*w,gpointer data) {int d,i,j;
  for(i=0;i<width;i++) for(j=0;j<height;j++) {
    if     (selmode==0) {if(isclear(i,j)) gsq[i][j].fl|=16;}
    else if(selmode==1) {for(d=0;d<ndir[gtype];d++) if(isstartoflight(i,j,d)) gsq[i][j].dsel|=1<<d;}
    }
  if(selmode==2) for(i=0;i<nvl;i++) vls[i].sel=1;
  selchange();
  }

// switch to light mode
static void sel_tol(void) {int d,i,j;
  if(selmode==1||selmode==2) return;
  DEB1 printf("seltol()\n");
  selnolights();
  for(i=0;i<width;i++) for(j=0;j<height;j++) if(gsq[i][j].fl&16) for(d=0;d<ndir[gtype];d++) {
    DEB1 printf("  %d %d %d\n",i,j,d);
    sellight(i,j,d,1);
    }
  selmode=1;
  }

// switch to cell mode
static void sel_toc(void) {int i,j,d,k,l,m,n,gx[MXCL],gy[MXCL],lx[MXCL],ly[MXCL];
  DEB1 printf("seltoc()\n");
  if(selmode==0) return;
  selnocells();
  if(selmode==1) {
    for(i=0;i<width;i++) for(j=0;j<height;j++) for(d=0;d<ndir[gtype];d++)
      if(isstartoflight(i,j,d)&&issellight(i,j,d)) {
      DEB1 printf("  %d %d %d\n",i,j,d);
      l=getlight(lx,ly,i,j,d);
      for(k=0;k<l;k++) {
        m=getmergegroup(gx,gy,lx[k],ly[k]);
        for(n=0;n<m;n++) gsq[gx[n]][gy[n]].fl|=16;
        }
      }
    }
  else if(selmode==2) {
    for(i=0;i<nvl;i++) for(j=0;j<vls[i].l;j++) {
      m=getmergegroup(gx,gy,vls[i].x[j],vls[i].y[j]);
      for(n=0;n<m;n++) gsq[gx[n]][gy[n]].fl|=16;
      }
    }
  selmode=0;
  }

// selection commands: single light
static void m_sellight(GtkWidget*w,gpointer data) {
  if(dir>=100) {
    if(dir>=100+nvl) return;
    if(selmode!=2) m_selnone(w,data);
    selmode=2;
    vls[dir-100].sel=!vls[dir-100].sel;
    }
  else {
    if(selmode!=1) m_selnone(w,data);
    selmode=1;
    sellight(curx,cury,dir,!issellight(curx,cury,dir));
    }
  selchange();
  }

// all lights parallel
static void m_sellpar(GtkWidget*w,gpointer data) {int f,i,j;
  if(dir>=100) return;
  if(selmode!=1) m_selnone(w,data);
  selmode=1;
  f=issellight(curx,cury,dir);
  for(i=0;i<width;i++) for(j=0;j<height;j++) if(isstartoflight(i,j,dir)) sellight(i,j,dir,!f);
  selchange();
  }

// single cell
static void m_selcell(GtkWidget*w,gpointer data) {int f;
  if(selmode!=0) m_selnone(w,data);
  selmode=0;
  if(!isclear(curx,cury)) return;
  f=(~gsq[curx][cury].fl)&16;
  selcell(curx,cury,f);
  selchange();
  }

static void m_selcover(GtkWidget*w,gpointer data) {int i,j;
  if(selmode!=0) m_selnone(w,data);
  selmode=0;
  for(i=0;i<width;i++) for(j=0;j<height;j++)
    if(isownmergerep(i,j)) selcell(i,j,gsq[i][j].sp.spor);
  selchange();
  }

static void m_selcunch(GtkWidget*w,gpointer data) {int i,j;
  if(selmode!=0) m_selnone(w,data);
  selmode=0;
  for(i=0;i<width;i++) for(j=0;j<height;j++)
    if(isownmergerep(i,j)) {
      if(gsq[i][j].e0) selcell(i,j,gsq[i][j].e0->checking==1);
      }
  selchange();
  }

static void m_selctreat(GtkWidget*w,gpointer data) {int i,j;
  if(selmode!=0) m_selnone(w,data);
  selmode=0;
  for(i=0;i<width;i++) for(j=0;j<height;j++)
    if(isownmergerep(i,j)) selcell(i,j,gsq[i][j].sp.spor?gsq[i][j].sp.ten:dsp.ten);
  selchange();
  }

static void m_sellover(GtkWidget*w,gpointer data) {int d,i,j;
  if(selmode!=1) m_selnone(w,data);
  selmode=1;
  for(i=0;i<width;i++) for(j=0;j<height;j++) for(d=0;d<ndir[gtype];d++)
    if(isstartoflight(i,j,d)) sellight(i,j,d,gsq[i][j].lp[d].lpor);
  selchange();
  }

static void m_selltreat(GtkWidget*w,gpointer data) {int d,i,j;
  if(selmode!=1) m_selnone(w,data);
  selmode=1;
  for(i=0;i<width;i++) for(j=0;j<height;j++) for(d=0;d<ndir[gtype];d++)
    if(isstartoflight(i,j,d)) sellight(i,j,d,gsq[i][j].lp[d].lpor?gsq[i][j].lp[d].ten:dlp.ten);
  selchange();
  }

// select violating lights
static void m_selviol(GtkWidget*w,gpointer data) {int d,i,j,v;
  if(selmode!=1) m_selnone(w,data);
  selmode=1;
  v=(intptr_t)data;
  DEB1 printf("selviol(%d)\n",v);
  for(i=0;i<width;i++) for(j=0;j<height;j++) for(d=0;d<ndir[gtype];d++)
    if(isstartoflight(i,j,d)) sellight(i,j,d,!!(gsq[i][j].vflags[d]&v));
  selchange();
  }

static int issqinvl(int x,int y,int v) {int cx,cy,gx,gy,j;
  getmergerep(&cx,&cy,x,y);
  for(j=0;j<vls[v].l;j++) {
    getmergerep(&gx,&gy,vls[v].x[j],vls[v].y[j]);
    if(cx==gx&&cy==gy) return 1;
    }
  return 0;
  }

static void m_selfvl(GtkWidget*w,gpointer data) {int i,j;
  if(nvl==0) {reperr("No free lights\nhave been defined");return;}
  if(selmode!=2) {
    m_selnone(w,data);
    for(i=0;i<nvl;i++) if(issqinvl(curx,cury,i)) break;
    j=i%nvl; // otherwise, select the first one
    }
  else {
    j=-1;
    for(i=0;i<nvl;i++) if(vls[i].sel) j=i; // find last selected vl
    j=(j+1)%nvl; // one after
    }
  for(i=0;i<nvl;i++) vls[i].sel=0;
  vls[j].sel=1;
  selmode=2;
  selchange();
  }

static void m_selmode(GtkWidget*w,gpointer data) {
  if(selmode==0) sel_tol();
  else           sel_toc();
  selchange();
  }

static void curmoved(void) {
  static int odir=0;
  refreshcur();
  if(odir>=100||dir>=100) refreshsel();
  odir=dir;
  curmf=1;
  }

static void gridchangen(void) {
  int d,i,j;
  // only squares at start of lights can have dsel information
  for(i=0;i<width;i++) for(j=0;j<height;j++) for(d=0;d<ndir[gtype];d++) if(!isstartoflight(i,j,d)) gsq[i][j].dsel&=~(1<<d);
  selchange();
  curmoved();
  donumbers();
  refreshnum();
  compute(0);
  }

static void gridchange(void) {
  gridchangen();
  undo_push();
  }

static void chkresetat() {int i,j;
  for(i=0;i<NMSG;i++) if(treatorder[i]>0&&treat_lab[treatmode][i]) for(j=0;j<MXFL;j++) if(treatcstr[i][j]!=ABM_ALL) goto ew0;
  return; // nothing to reset
ew0:
  if(box(GTK_MESSAGE_QUESTION,"\n  Reset answer  \n  treatment constraints?  \n",GTK_STOCK_YES,GTK_STOCK_NO)==0) return;
  for(i=0;i<NMSG;i++) if(treatorder[i]>0&&treat_lab[treatmode][i]) for(j=0;j<MXFL;j++) treatcstr[i][j]=ABM_ALL;
  }

static void m_eraseall(void) {int i,j;
  for(i=0;i<width;i++) for(j=0;j<height;j++) {clrcont(i,j); refreshsqmg(i,j);}
  chkresetat();
  gridchange();
  }

static void m_erasesel(void) {int i,j;
  if(selmode!=0) {
    sel_toc();
    selchange();
    }
  for(i=0;i<width;i++) for(j=0;j<height;j++) {if(gsq[i][j].fl&16) {clrcont(i,j);refreshsqmg(i,j);}}
  chkresetat();
  gridchange();
  }

static void m_dictionaries(GtkWidget*w,gpointer data) {dictldia();}

// convert tentative entries (where feasible letter bitmap has exactly one bit set) to firm entries
static void m_accept(GtkWidget*w,gpointer data) {
  int de,nd,f,i,j,x,y;
  ABM m;
  struct entry*e;
  ABM b;

  DEB1 printf("m_accept\n");
  for(x=0;x<width;x++) for(y=0;y<height;y++) {
    e=gsq[x][y].e0;
    if(e) {
      de=getdech(x,y);
      if(de==0) nd=1,de=1; else nd=ndir[gtype];
      for(i=0;i<nd;i++) {
        for(j=0;j<gsq[x][y].ctlen[i];j++) {
          m=e->flbmh; // get hints bitmap
//          printf("%d %d %16llx\n",x,y,m);
          if(onebit(m)) gsq[x][y].ctbm[i][j]=m; // could consider removing the onebit() test but potentially confusing?
          e++;
          }
        }
      refreshsqmg(x,y);
      }
    }
  f=0;
  for(i=0;i<NMSG;i++) if(treatorder[i]>0&&treat_lab[treatmode][i]) for(j=0;j<MXFL;j++) {
    e=treatmsge0[i][j];
    if(e) {
      b=treatcstr[i][j]&e->flbmh;
      if(b!=treatcstr[i][j]) treatcstr[i][j]=b,f=1;
      }
    }
  if(f) {okbox("\n  Answer treatment  \n  constraints updated  \n");}
  gridchange();
  }

// run filler
static void m_autofill(GtkWidget*w,gpointer data) {
  int mode;
  if(filler_status==0) return; // already running?
  mode=(intptr_t)data; // selected mode (1=all, 2=selection)
  if(mode==2&&selmode!=0) {
    sel_toc();
    selchange();
    }
  if(compute(mode)) { reperr("Could not start filler"); return; }
  box(GTK_MESSAGE_INFO,"\n  Filling in progress  \n","  Stop  ",0); // this box is closed manually or by the filler when it completes
  DEB1 printf("stopping filler...\n");
  filler_stop();
  DEB1 printf("stopped filler, status=%d\n",filler_status);
  setposslabel("");
  switch(filler_status) {
  case -4:
  case -3: reperr("Error generating lists of feasible lights");break;
  case -2: reperr("Out of stack space");break;
  case -1: reperr("Out of memory");break;
  case 1: reperr("No fill found");break;
    }
  }

// grid edit operations

static void editblock (int x,int y) {             symmdo(a_editblock ,0,x,y,0);gridchange();}
static void editempty (int x,int y) {clrcont(x,y);symmdo(a_editempty ,0,x,y,0);gridchange();}
static void editcutout(int x,int y) {             symmdo(a_editcutout,0,x,y,0);gridchange();}

// called from menu
static void m_editblock (GtkWidget*w,gpointer data) {editblock (curx,cury);}
static void m_editempty (GtkWidget*w,gpointer data) {editempty (curx,cury);}
static void m_editcutout(GtkWidget*w,gpointer data) {editcutout(curx,cury);}

// bar behind cursor
static void m_editbarb(GtkWidget*w,gpointer data) {
  if(dir>=100) return;
  symmdo(a_editbar,!isbar(curx,cury,dir+ndir[gtype]),curx,cury,dir+ndir[gtype]);
  gridchange();
  }

// merge/join ahead
static void m_editmerge(GtkWidget*w,gpointer data) {
  if(dir>=100) return;
  symmdo(a_editmerge,!ismerge(curx,cury,dir),curx,cury,dir);
  gridchange();
  }



// flip grid in main diagonal
static void m_editflip(GtkWidget*w,gpointer data) {int i,j,k,t;struct square s;struct lprop l;ABM p[MXCT];
  if(gshape[gtype]>2) return;
  for(i=0;i<MXSZ;i++) for(j=i+1;j<MXSZ;j++) {s=gsq[j][i];gsq[j][i]=gsq[i][j];gsq[i][j]=s;}
  for(i=0;i<MXSZ;i++) for(j=0;j<MXSZ;j++) {
    t=gsq[i][j].bars;
    if(gshape[gtype]==0) t=((t&1)<<1)|((t&2)>>1); // swap bars around
    else         t=((t&1)<<2)|( t&2    )|((t&4)>>2);
    gsq[i][j].bars=t;
    t=gsq[i][j].merge;
    if(gshape[gtype]==0) t=((t&1)<<1)|((t&2)>>1); // swap merges around
    else         t=((t&1)<<2)|( t&2    )|((t&4)>>2);
    gsq[i][j].merge=t;
    t=gsq[i][j].dsel;
    if(gshape[gtype]==0) t=((t&1)<<1)|((t&2)>>1); // swap directional selects around
    else         t=((t&1)<<2)|( t&2    )|((t&4)>>2);
    gsq[i][j].dsel=t;
    k=(gshape[gtype]==0)?1:2;
    l=gsq[i][j].lp[0];gsq[i][j].lp[0]=gsq[i][j].lp[k];gsq[i][j].lp[k]=l;
    // if dechecked we want to swap the across and down contents
    if(getdech(i,j)) {
      memcpy(p,gsq[i][j].ctbm[0],sizeof(p));
      memcpy(gsq[i][j].ctbm[0],gsq[i][j].ctbm[k],sizeof(p));
      memcpy(gsq[i][j].ctbm[k],p,sizeof(p));
      t=gsq[i][j].ctlen[0]; gsq[i][j].ctlen[0]=gsq[i][j].ctlen[k]; gsq[i][j].ctlen[k]=t;
      }
    }
  for(i=0;i<nvl;i++) for(j=0;j<vls[i].l;j++) t=vls[i].x[j],vls[i].x[j]=vls[i].y[j],vls[i].y[j]=t;
  t=height;height=width;width=t; // exchange width and height
  if(symmm==1||symmm==2) symmm=3-symmm; // flip symmetries
  if(symmd==1||symmd==2) symmd=3-symmd;
  if     (gtype==1) gtype=2;
  else if(gtype==2) gtype=1;
  else if(gtype==5) gtype=6;
  else if(gtype==6) gtype=5;
  else if(gtype==7) gtype=8;
  else if(gtype==8) gtype=7;
  gridchange();
  syncgui();
  }

// rotate (circular) grid
static void m_editrot(GtkWidget*w,gpointer data) {int i,j; struct square t,u;
  if(gshape[gtype]<3) return;
  if((intptr_t)data==0xf001)
    for(j=0;j<height;j++) {t=gsq[width-1][j];for(i=      0;i<width;i++) u=gsq[i][j],gsq[i][j]=t,t=u;}
  else
    for(j=0;j<height;j++) {t=gsq[      0][j];for(i=width-1;i>=0   ;i--) u=gsq[i][j],gsq[i][j]=t,t=u;}
  gridchange();
  syncgui();
  }

static void m_vlnew(GtkWidget*w,gpointer data) {
  if(nvl>=NVL) {reperr("Limit on number of\nfree lights reached");return;}
  m_selnone(w,data);
  selmode=2;
  vls[nvl].x[0]=curx;
  vls[nvl].y[0]=cury;
  vls[nvl].l=1;
  vls[nvl].sel=1;
  resetlp(&vls[nvl].lp);
  nvl++;
  gridchange();
  }

static int getselvl() {int i;
  if(selmode!=2) return -1;
  if(nsel!=1) return -1;
  for(i=0;i<nvl;i++) if(vls[i].sel) return i;
  return -1;
  }

static void m_vlextend(GtkWidget*w,gpointer data) {int i;
  i=getselvl();
  if(i==-1) return;
  if(vls[i].l>=MXCL) {reperr("Limit on length of\nfree light reached");return;}
  vls[i].x[vls[i].l]=curx;
  vls[i].y[vls[i].l]=cury;
  vls[i].l++;
  DEB1 printf("VL %d l=%d\n",i,vls[i].l);
  gridchange();
  }

static void m_vldelete(GtkWidget*w,gpointer data) {int i,j,d;
  if(selmode!=2) return;
  d=-1;
  for(i=0,j=0;i<nvl;i++)
    if(!vls[i].sel) {
      if(i+100==dir) d=j; // keep track of where current vl (if any) ends up
      memmove(vls+j,vls+i,sizeof(struct vl)),j++;
      }
  if(dir>=100) {
    if(d>=0) dir=d+100;
    else     dir=0;
    }
  nvl=j;
  gridchange();
  }

static void m_vlcurtail(GtkWidget*w,gpointer data) {int i;
  i=getselvl();
  if(i==-1) return;
  if(vls[i].l<2) {m_vldelete(w,data);return;}
  vls[i].l--;
  DEB1 printf("VL %d l=%d\n",i,vls[i].l);
  gridchange();
  }

static void m_vlmodify(GtkWidget*w,gpointer data) {int i;
  i=getselvl();
  if(i==-1) return;
  mvldia(i);
  }




static void m_helpabout(GtkWidget*w,gpointer data) {char s[2560];
  sprintf(s,
  "%s\n\n"
  "Copyright 2011-2014 Mark Owen; Windows port by Peter Flippant\n"
  "\n"
  "This program is free software; you can redistribute it and/or modify "
  "it under the terms of version 2 of the GNU General Public License as "
  "published by the Free Software Foundation.\n"
  "\n"
  "This program is distributed in the hope that it will be useful, "
  "but WITHOUT ANY WARRANTY; without even the implied warranty of "
  "MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the "
  "GNU General Public License for more details.\n"
  "\n"
  "You should have received a copy of the GNU General Public License along "
  "with this program; if not, write to the Free Software Foundation, Inc., "
  "51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.\n"
  "\n"
  "For more information visit http://www.quinapalus.com/ or "
  "contact qxw@quinapalus.com\n"
  ,RELEASE);
  okbox(s);}

static int w_destroy(void) {gtk_main_quit();return 0;}
static int w_delete(void) {return !(!unsaved||areyousure("quit"));}






static void moveleft(int*x,int*y) {
  switch(gtype) {
    case 0:case 1:case 2:case 6:case 8:(*x)--;break;
    case 7:
      if(*x==0) *y=height-1-*y;
    case 3:case 4:case 5:case 9:
      *x=(*x+width-1)%width;
      break;
    }
  }

static void moveright(int*x,int*y) {
  switch(gtype) {
    case 0:case 1:case 2:case 6:case 8:(*x)++;break;
    case 7:
      if(*x==width-1) *y=height-1-*y;
    case 3:case 4:case 5:case 9:
      *x=(*x+1)%width;
      break;
    }
  }

static void moveup(int*x,int*y) {
  switch(gtype) {
    case 0:case 1:case 2:case 3:case 4:case 5:case 7:(*y)--;break;
    case 8:
      if(*y==0) *x=width-1-*x;
    case 6:case 9:
      *y=(*y+height-1)%height;
      break;
    }
  }

static void movedown(int*x,int*y) {
  switch(gtype) {
    case 0:case 1:case 2:case 3:case 4:case 5:case 7:(*y)++;break;
    case 8:
      if(*y==height-1) *x=width-1-*x;
    case 6:case 9:
      *y=(*y+1)%height;
      break;
    }
  }

static void movehome(int*x,int*y) {int l,lx[MXCL],ly[MXCL];
  l=getlight(lx,ly,*x,*y,dir);
  if(l<1) return;
  *x=lx[0];*y=ly[0];
  }

static void moveend(int*x,int*y) {int l,lx[MXCL],ly[MXCL];
  l=getlight(lx,ly,*x,*y,dir);
  if(l<1) return;
  *x=lx[l-1];*y=ly[l-1];
  }

static void nextdir() {
  do{
    dir++;
    if(dir==ndir[gtype]) dir=100;
    if(dir>=100+nvl) dir=0;
    if(dir<ndir[gtype]) return;
    } while(!issqinvl(curx,cury,dir-100));
  }

static void prevdir() {
  do{
    dir--;
    if(dir<0) dir=100+nvl-1;
    if(dir==99) dir=ndir[gtype]-1;
    if(dir<ndir[gtype]) return;
    } while(!issqinvl(curx,cury,dir-100));
  }



// GTK EVENT HANDLERS

// process keyboard
static int keypress(GtkWidget *widget, GdkEventKey *event) {int k,f,r,x,y;
  r=0; // flag whether we have processed this key
  k=event->keyval; f=event->state;
  if(k>='a'&&k<='z') k+='A'-'a';
  DEB1 printf("keypress event: %x %x\n",k,f);
  f&=12; // mask off flags except CTRL, ALT
  if((k==GDK_Tab&&f==0)||(((k>='A'&&k<='Z')||(k>='0'&&k<='9'))&&f==0)) { // tab and letters, digits
    r=1;
    if(setechar(curx,cury,dir,(k==GDK_Tab)?' ':k)) ccontdia(); // open dialogue if not a simple case
    refreshsqmg(curx,cury);
    refreshnum();
    if(dir<100) stepforwmifingrid(&curx,&cury,dir);
    undo_push();
    }
  if(k==' '&&f==0)       {r=1; if(dir<100) stepforwmifingrid(&curx,&cury,dir);}
  if(k==GDK_Left &&f==0) {r=1; if(dir>=100) dir=0; x=curx;y=cury;moveleft (&x,&y);if(isingrid(x,y)) curx=x,cury=y;}
  if(k==GDK_Right&&f==0) {r=1; if(dir>=100) dir=0; x=curx;y=cury;moveright(&x,&y);if(isingrid(x,y)) curx=x,cury=y;}
  if(k==GDK_Up   &&f==0) {r=1; if(dir>=100) dir=0; x=curx;y=cury;moveup   (&x,&y);if(isingrid(x,y)) curx=x,cury=y;}
  if(k==GDK_Down &&f==0) {r=1; if(dir>=100) dir=0; x=curx;y=cury;movedown (&x,&y);if(isingrid(x,y)) curx=x,cury=y;}

  if(k==GDK_Home &&f==0) {r=1; x=curx;y=cury;movehome (&x,&y);if(isingrid(x,y)) curx=x,cury=y;}
  if(k==GDK_End  &&f==0) {r=1; x=curx;y=cury;moveend  (&x,&y);if(isingrid(x,y)) curx=x,cury=y;}

  if((k==GDK_Page_Up          )&&f==0) {r=1; prevdir();}
  if((k==GDK_Page_Down||k=='/')&&f==0) {r=1; nextdir();}

  if(k=='.'&&f==0)       {r=1; editempty (curx,cury);}
  if(k==','&&f==0)       {r=1; editblock (curx,cury);}

  if(k==GDK_BackSpace&&f==0) {r=1; if(dir<100) stepbackifingrid(&curx,&cury,dir);}
  if(r) { // if we have processed the key, update both old and new cursor positions
    curmoved();
    compute(0);
    }
  return r;
  }

// convert screen coords to internal square coords; k is bitmap of sufficiently nearby edges
static void ptrtosq(int*x,int*y,int*k,int x0,int y0) {double u,v,u0,v0,r=0,t=0,xa=0,ya=0,xb=0,yb=0;int i,j;
  u0=((double)x0-bawdpx)/pxsq;v0=((double)y0-bawdpx)/pxsq;
  *k=0;
  switch(gshape[gtype]) {
  case 0:
    i=(int)floor(u0);u=u0-i;
    j=(int)floor(v0);v=v0-j;
    *x=i;*y=j;
    break;
  case 1:
    i=(int)floor(u0/1.2);u=u0-i*1.2;
    if((i&1)==0) {
      j=(int)floor(v0/1.4);v=v0-j*1.4;
      if(u>=0.4) {*x=i;*y=j;break;}
      if( 0.7*u+0.4*v<0.28) {*x=i-1;*y=j-1;break;}
      if(-0.7*u+0.4*v>0.28) {*x=i-1;*y=j;  break;}
      *x=i;*y=j;break;
    } else {
      j=(int)floor((v0-0.7)/1.4);v=v0-0.7-j*1.4;
      if(u>=0.4) {*x=i;*y=j;break;}
      if( 0.7*u+0.4*v<0.28) {*x=i-1;*y=j;  break;}
      if(-0.7*u+0.4*v>0.28) {*x=i-1;*y=j+1;break;}
      *x=i;*y=j;break;
      }
  case 2:
    j=(int)floor(v0/1.2);v=v0-j*1.2;
    if((j&1)==0) {
      i=(int)floor(u0/1.4);u=u0-i*1.4;
      if(v>=0.4) {*y=j;*x=i;break;}
      if( 0.7*v+0.4*u<0.28) {*y=j-1;*x=i-1;break;}
      if(-0.7*v+0.4*u>0.28) {*y=j-1;*x=i;  break;}
      *y=j;*x=i;break;
    } else {
      i=(int)floor((u0-0.7)/1.4);u=u0-0.7-i*1.4;
      if(v>=0.4) {*y=j;*x=i;break;}
      if( 0.7*v+0.4*u<0.28) {*y=j-1;*x=i;  break;}
      if(-0.7*v+0.4*u>0.28) {*y=j-1;*x=i+1;break;}
      *y=j;*x=i;break;
      }
  case 3:case 4:
    u=u0-height;v=v0-height;
    r=sqrt(u*u+v*v);
    if(r<1e-3) {*x=-1;*y=-1;return;}
    r=height-r;
    t=atan2(u,-v)*width/2/PI;
    if(gtype==4) t+=.5;
    if(t<0) t+=width;
    *x=(int)floor(t);
    *y=(int)floor(r);
    break;
    }
  switch(gshape[gtype]) { // click-near-edge bitmap
  case 0:case 1:case 2:
    for(i=0;i<ndir[gtype]*2;i++) {
      edgecoords(&xa,&ya,&xb,&yb,*x,*y,i);
      xb-=xa;yb-=ya;
      r=sqrt(xb*xb+yb*yb);xb/=r;yb/=r;
      xa=u0-xa;ya=v0-ya;
      if(fabs(xb*ya-xa*yb)<PXEDGE) *k|=1<<i;
      }
    break;
  case 3:case 4:
    if(r-*y>1-PXEDGE            ) *k|=2;
    if(r-*y<  PXEDGE            ) *k|=8;
    r=height-r;
    if(t-*x>1-PXEDGE/((r<2)?1:(r-1))) *k|=1;
    if(t-*x<  PXEDGE/((r<2)?1:(r-1))) *k|=4;
    break;
    }
  }

int dragflag=-1; // store selectedness state of square where shift-drag starts, or -1 if none in progress

static void mousel(int x,int y) { // left button
  if(selmode!=0) m_selnone(0,0);
  selmode=0;
  if(dragflag==-1) dragflag=gsq[x][y].fl&16; // set f if at beginning of shift-drag
  if((gsq[x][y].fl&16)==dragflag) {
    selcell(x,y,!dragflag);
    selchange();
    }
  }

static void mouser(int x,int y) { // right button
  if(dir>=100) return;
  if(selmode!=1) m_selnone(0,0);
  selmode=1;
  if(dragflag==-1) dragflag=issellight(x,y,dir);
  if(issellight(x,y,dir)==dragflag) {
    sellight(x,y,dir,!dragflag);
    selchange();
    }
  }

// pointer motion
static gint mousemove(GtkWidget*widget,GdkEventMotion*event) {
  int e,k,x,y;
  ptrtosq(&x,&y,&e,(int)floor(event->x+.5),(int)floor(event->y));
  k=(int)(event->state); // buttons and modifiers
//  DEB2 printf("mouse move event (%d,%d) %d e=%02x\n",x,y,k,e);
  if(!isingrid(x,y)) return 0;
  if((k&GDK_SHIFT_MASK)==0) {dragflag=-1;return 0;} // shift not held down: reset f
  if     (k&GDK_BUTTON3_MASK) mouser(x,y);
  else if(k&GDK_BUTTON1_MASK) mousel(x,y);
  else dragflag=-1; // button not held down: reset dragflag
  return 0;
  }

// click in grid area
static gint button_press_event(GtkWidget*widget,GdkEventButton*event) {
  int b,e,ee,k,x,y;
  ptrtosq(&x,&y,&e,(int)floor(event->x+.5),(int)floor(event->y));
  k=event->state;
  DEB2 printf("button press event (%f,%f) -> (%d,%d) e=%02x button=%08x type=%08x state=%08x\n",event->x,event->y,x,y,e,(int)event->button,(int)event->type,k);
  if(event->type==GDK_BUTTON_RELEASE) dragflag=-1;
  if(event->type!=GDK_BUTTON_PRESS) goto ew0;
  if(k&GDK_SHIFT_MASK) {
    if     (event->button==3) mouser(x,y);
    else if(event->button==1) mousel(x,y);
    return 0;
    }
  if(event->button!=1) goto ew0; // only left clicks do anything now
  if(!isingrid(x,y)) goto ew0;
  ee=!!(e&(e-1)); // more than one bit set in e?

  if(clickblock&&ee) { // flip between block and space when a square is clicked near a corner
    if((gsq[x][y].fl&1)==0) {editblock(x,y);gridchange();goto ew1;}
    else                    {editempty(x,y);gridchange();goto ew1;}
    }
  else if(clickbar&&e&&!ee) { // flip bar if clicked in middle of edge
    b=logbase2(e);
    DEB2 printf("  x=%d y=%d e=%d b=%d isbar(x,y,b)=%d\n",x,y,e,b,isbar(x,y,b));
    symmdo(a_editbar,!isbar(x,y,b),x,y,b);
    gridchange();
    goto ew1;
    }

  if(x==curx&&y==cury) nextdir(); // flip direction for click in square of cursor
  else {
    curx=x; cury=y; // not trapped as producing bar or block, so move cursor
    if(dir>=100) dir=0;
    }
  curmoved();
  gridchangen();

  ew1:
  gtk_window_set_focus(GTK_WINDOW(mainw),grid_da);
  ew0:
  DEB2 printf("Exiting button_press_event()\n");
  return FALSE;
  }

// word list entry selected
static void selrow(GtkWidget*widget,gint row,gint column, GdkEventButton*event,gpointer data) {
  int i,l,l0,lx[MXCL],ly[MXCL],nc;
  ABM*abmp[MXLE];
  DEB1 printf("row select event\n");
  if(llistem&EM_JUM) return; // don't allow click-to-enter for jumbles
  l=getlight(lx,ly,curx,cury,dir);
  if(l<2) return;
  nc=getlightbmp(abmp,curx,cury,dir);
  if(nc<2) return;
  if(!llistp) return;
  if(row<0||row>=llistn) return;
  if(llistp[row]>=ltotal) return; // light building has not caught up yet, so ignore click
  DEB1 printf("lts[llistp[row]].s=<%s>\n",lts[llistp[row]].s);
  l0=strlen(lts[llistp[row]].s);
  if(lts[llistp[row]].tagged) l0-=NMSG;
  if(l0!=nc) return;
  for(i=0;i<nc;i++) *abmp[i]=chartoabm[(int)(lts[llistp[row]].s[i])];
  for(i=0;i<l;i++) refreshsqmg(lx[i],ly[i]);
  gridchange();
  gtk_window_set_focus(GTK_WINDOW(mainw),grid_da);
  }

// grid update
static gint configure_event(GtkWidget*widget,GdkEventConfigure*event) {
  DEB4 printf("config event: new w=%d h=%d\n",widget->allocation.width,widget->allocation.height);
  return TRUE;
  }

// redraw the grid area
static gint expose_event(GtkWidget*widget,GdkEventExpose*event) {double u,v;
  cairo_t*cr;
  DEB4 printf("expose event x=%d y=%d w=%d h=%d\n",event->area.x,event->area.y,event->area.width,event->area.height),fflush(stdout);
  if(widget==grid_da) {
    cr=gdk_cairo_create(widget->window);
    cairo_rectangle(cr,event->area.x,event->area.y,event->area.width,event->area.height);
    cairo_clip(cr);
    repaint(cr);
    cairo_destroy(cr);
    if(curmf) {
      mgcentre(&u,&v,curx,cury,0,1);
      DEB1 printf("curmoved: %f %f %d %d\n",u,v,pxsq,bawdpx);
      gtk_adjustment_clamp_page(gtk_scrolled_window_get_hadjustment(GTK_SCROLLED_WINDOW(grid_sw)),(u-.5)*pxsq+bawdpx-3,(u+.5)*pxsq+bawdpx+3); // scroll window to follow cursor
      gtk_adjustment_clamp_page(gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(grid_sw)),(v-.5)*pxsq+bawdpx-3,(v+.5)*pxsq+bawdpx+3);
      curmf=0;
      }
    }
  return FALSE;
  }

void invaldarect(int x0,int y0,int x1,int y1) {GdkRectangle r;
  r.x=x0; r.y=y0; r.width=x1-x0; r.height=y1-y0;
  DEB4 printf("invalidate(%d,%d - %d,%d)\n",x0,y0,x1,y1);
  gdk_window_invalidate_rect(grid_da->window,&r,0);
  }

void invaldaall() {
  DEB4 printf("invalidate all\n");
  gdk_window_invalidate_rect(grid_da->window,0,0);
  }





// GTK DIALOGUES

// general filename dialogue: button text in but, default file extension in ext
// result copied to res if non-NULL, "filename" otherwise
static int filedia(char*res,char*but,char*ext,char*filetype,int write) {
  GtkWidget*dia;
  int i;
  char*p,t[SLEN+50];
  GtkFileFilter*filt0,*filt1;
  
  if(!res) res=filename;
  filt0=gtk_file_filter_new();
  if(!filt0) return 0;
  gtk_file_filter_add_pattern(filt0,"*");
  gtk_file_filter_set_name(filt0,"All files");
  filt1=gtk_file_filter_new();
  if(!filt1) return 0;
  strcpy(t,"*");
  strcat(t,strrchr(ext,'.'));
  // printf("<%s>\n",t);
  gtk_file_filter_add_pattern(filt1,t);
  for(i=1;t[i];i++) t[i]=toupper(t[i]);
  // printf("<%s>\n",t);
  gtk_file_filter_add_pattern(filt1,t);
  strcpy(t,filetype);
  strcat(t," files");
  gtk_file_filter_set_name(filt1,t);

  dia=gtk_file_chooser_dialog_new(but,0,
    write?GTK_FILE_CHOOSER_ACTION_SAVE:GTK_FILE_CHOOSER_ACTION_OPEN,
    GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
    GTK_STOCK_OK, GTK_RESPONSE_OK,
    NULL);
  gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dia),filt1);
  gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dia),filt0);
  if(strcmp(filenamebase,"")) strcpy(res,filenamebase);
  else                        strcpy(res,"untitled");
  strcat(res,ext);
  strcpy(t,res);
  if(write) gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(dia),t);
ew0:
  i=gtk_dialog_run(GTK_DIALOG(dia));
  i=i==GTK_RESPONSE_OK;
  if(i) {
    p=gtk_file_chooser_get_filename((GtkFileChooser*)dia);
    if(strlen(p)>SLEN) {reperr("Filename too long");goto ew0;}
    strcpy(res,p);
    }
  else strcpy(res,"");
  gtk_widget_destroy(dia);
  return i; // return 1 if got name successfully (although may be empty string)
  }

// cell contents dialogue
static int ccontdia() {
  GtkWidget*dia,*vb,*l0,*m[MAXNDIR],*lm[MAXNDIR],*e[MAXNDIR];
  int i,u,x,y;
  char s[100],t[MXCT*(NL+4)];

  x=curx; y=cury;
  getmergerep(&x,&y,x,y);
  if(!isclear(x,y)) {reperr("Please place the cursor on a cell\nthat can contain characters");return 1;}

  dia=gtk_dialog_new_with_buttons("Cell contents",
    GTK_WINDOW(mainw),GTK_DIALOG_DESTROY_WITH_PARENT,
    GTK_STOCK_CANCEL,GTK_RESPONSE_CANCEL,GTK_STOCK_OK,GTK_RESPONSE_OK,NULL);
  gtk_dialog_set_default_response(GTK_DIALOG(dia),GTK_RESPONSE_OK);
  vb=gtk_vbox_new(0,2); // box to hold everything
  gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dia)->vbox),vb,TRUE,TRUE,0);

  u=getdech(x,y);
  if(u) {
    l0=gtk_label_new("Contribution from cell");
    gtk_misc_set_alignment(GTK_MISC(l0),0,0.5);
    gtk_box_pack_start(GTK_BOX(vb),l0,TRUE,TRUE,0);
    }
  for(i=0;i<ndir[gtype];i++) {
    m[i]=gtk_hbox_new(0,2);
    if(u) sprintf(s,"to %s lights: ",dname[gtype][i]);
    else  sprintf(s,"Contents of cell: ");
    lm[i]=gtk_label_new(s);
    gtk_label_set_width_chars(GTK_LABEL(lm[i]),20);
    gtk_misc_set_alignment(GTK_MISC(lm[i]),1,0.5);
    gtk_box_pack_start(GTK_BOX(m[i]),lm[i],FALSE,FALSE,0);
    e[i]=gtk_entry_new();
    gtk_entry_set_width_chars(GTK_ENTRY(e[i]),30);
    gtk_entry_set_max_length(GTK_ENTRY(e[i]),sizeof(t));
    abmstostr(t,gsq[x][y].ctbm[i],gsq[x][y].ctlen[i],0);
    gtk_entry_set_text(GTK_ENTRY(e[i]),t);
    gtk_box_pack_start(GTK_BOX(m[i]),e[i],FALSE,FALSE,0);
    gtk_box_pack_start(GTK_BOX(vb),m[i],TRUE,TRUE,0);
    if(i==dir) gtk_window_set_focus(GTK_WINDOW(dia),e[i]);
    gtk_entry_set_activates_default(GTK_ENTRY(e[i]),1);
    if(!u) break;
    }
  l0=gtk_label_new("(use '.' for any characters yet to be filled or enter a pattern)");
  gtk_box_pack_start(GTK_BOX(vb),l0,TRUE,TRUE,0);

  gtk_widget_show_all(dia);
  i=gtk_dialog_run(GTK_DIALOG(dia));
  if(i==GTK_RESPONSE_OK) {
    for(i=0;i<ndir[gtype];i++) {
      gsq[x][y].ctlen[i]=strtoabms(gsq[x][y].ctbm[i],MXCT,(char*)gtk_entry_get_text(GTK_ENTRY(e[i])),0);
      if(!u) break;
      }
    gridchange();
    }
  gtk_widget_destroy(dia);
  refreshsqmg(x,y);
  return 1;
  }

// square properties dialogue

static GtkWidget*sprop_cbg,*sprop_cfg,*sprop_cmk,*sprop_l0,*sprop_l1,*sprop_l2,*sprop_l3,*sprop_l4,*sprop_tr,*sprop_w20,*sprop_w21,*sprop_or;
static GtkWidget*sprop_mkl[MAXNDIR*2],*sprop_mke[MAXNDIR*2];
static int sprop_g;

int sprop_setactive() {int i,k;
  k=sprop_g||gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(sprop_or));
  gtk_widget_set_sensitive(sprop_cbg,k);
  gtk_widget_set_sensitive(sprop_cfg,k);
  gtk_widget_set_sensitive(sprop_cmk,k);
  gtk_widget_set_sensitive(sprop_l0,k);
  gtk_widget_set_sensitive(sprop_l1,k);
  gtk_widget_set_sensitive(sprop_l2,k);
  gtk_widget_set_sensitive(sprop_l3,k);
  gtk_widget_set_sensitive(sprop_w20,k);
  gtk_widget_set_sensitive(sprop_tr,k);
  gtk_widget_set_sensitive(sprop_w21,k);
  for(i=0;i<ndir[gtype]*2;i++) {
    gtk_widget_set_sensitive(sprop_mkl[i],k);
    gtk_widget_set_sensitive(sprop_mke[i],k);
    }
  return 1;
  }

static int spropdia(int g) {
  GtkWidget*dia,*w,*vb;
  GdkColor gcbg,gcfg,gcmk;
  struct sprop*sps[MXSZ*MXSZ];
  int nsps;
  int d,i,j;

  sprop_g=g;
  nsps=0;
  if(g) {sps[0]=&dsp;nsps=1;}
  else {
    for(j=0;j<height;j++) for(i=0;i<width;i++) if(gsq[i][j].fl&16) sps[nsps++]=&gsq[i][j].sp;
    if(selmode!=0||nsps==0) {reperr("Please select one or more cells\nwhose properties you wish to change");return 1;}
    }
  i=sps[0]->bgcol;
  gcbg.red  =((i>>16)&255)*257;
  gcbg.green=((i>> 8)&255)*257;
  gcbg.blue =((i    )&255)*257;
  i=sps[0]->fgcol;
  gcfg.red  =((i>>16)&255)*257;
  gcfg.green=((i>> 8)&255)*257;
  gcfg.blue =((i    )&255)*257;
  i=sps[0]->mkcol;
  gcmk.red  =((i>>16)&255)*257;
  gcmk.green=((i>> 8)&255)*257;
  gcmk.blue =((i    )&255)*257;

  dia=gtk_dialog_new_with_buttons(g?"Default cell properties":"Selected cell properties",
    GTK_WINDOW(mainw),GTK_DIALOG_DESTROY_WITH_PARENT,
    GTK_STOCK_CANCEL,GTK_RESPONSE_CANCEL,GTK_STOCK_APPLY,GTK_RESPONSE_OK,NULL);
  vb=gtk_vbox_new(0,2); // box to hold everything
  gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dia)->vbox),vb,TRUE,TRUE,0);

  if(!g) {
    sprop_or=gtk_check_button_new_with_mnemonic("_Override default cell properties");
    gtk_box_pack_start(GTK_BOX(vb),sprop_or,TRUE,TRUE,0);
    gtk_signal_connect(GTK_OBJECT(sprop_or),"clicked",GTK_SIGNAL_FUNC(sprop_setactive),0);
    w=gtk_hseparator_new();
    gtk_box_pack_start(GTK_BOX(vb),w,TRUE,TRUE,0);
    }

  w=gtk_hbox_new(0,2);
  sprop_l0=gtk_label_new("Background colour: ");
  gtk_label_set_width_chars(GTK_LABEL(sprop_l0),18);
  gtk_misc_set_alignment(GTK_MISC(sprop_l0),1,0.5);
  gtk_box_pack_start(GTK_BOX(w),sprop_l0,FALSE,FALSE,0);
  sprop_cbg=gtk_color_button_new_with_color(&gcbg);
  gtk_box_pack_start(GTK_BOX(w),sprop_cbg,FALSE,FALSE,0);
  gtk_box_pack_start(GTK_BOX(vb),w,TRUE,TRUE,0);

  w=gtk_hbox_new(0,2);
  sprop_l1=gtk_label_new("Foreground colour: ");
  gtk_label_set_width_chars(GTK_LABEL(sprop_l1),18);
  gtk_misc_set_alignment(GTK_MISC(sprop_l1),1,0.5);
  gtk_box_pack_start(GTK_BOX(w),sprop_l1,FALSE,FALSE,0);
  sprop_cfg=gtk_color_button_new_with_color(&gcfg);
  gtk_box_pack_start(GTK_BOX(w),sprop_cfg,FALSE,FALSE,0);
  gtk_box_pack_start(GTK_BOX(vb),w,TRUE,TRUE,0);

  w=gtk_hbox_new(0,2);                                                              gtk_box_pack_start(GTK_BOX(vb),w,TRUE,TRUE,0);
  sprop_l2=gtk_label_new("Font style: ");                                                 gtk_box_pack_start(GTK_BOX(w),sprop_l2,FALSE,FALSE,0);
  gtk_label_set_width_chars(GTK_LABEL(sprop_l2),18);
  gtk_misc_set_alignment(GTK_MISC(sprop_l2),1,0.5);
  sprop_w20=gtk_combo_box_new_text();                                                     gtk_box_pack_start(GTK_BOX(w),sprop_w20,FALSE,FALSE,0);
  gtk_combo_box_append_text(GTK_COMBO_BOX(sprop_w20),"Normal");
  gtk_combo_box_append_text(GTK_COMBO_BOX(sprop_w20),"Bold");
  gtk_combo_box_append_text(GTK_COMBO_BOX(sprop_w20),"Italic");
  gtk_combo_box_append_text(GTK_COMBO_BOX(sprop_w20),"Bold italic");
  i=sps[0]->fstyle;
  if(i<0) i=0;
  if(i>3) i=3;
  gtk_combo_box_set_active(GTK_COMBO_BOX(sprop_w20),i);

  w=gtk_hseparator_new();
  gtk_box_pack_start(GTK_BOX(vb),w,TRUE,TRUE,0);

  w=gtk_hbox_new(0,2);
  sprop_l4=gtk_label_new("Mark colour: ");
  gtk_label_set_width_chars(GTK_LABEL(sprop_l4),18);
  gtk_misc_set_alignment(GTK_MISC(sprop_l4),1,0.5);
  gtk_box_pack_start(GTK_BOX(w),sprop_l4,FALSE,FALSE,0);
  sprop_cmk=gtk_color_button_new_with_color(&gcmk);
  gtk_box_pack_start(GTK_BOX(w),sprop_cmk,FALSE,FALSE,0);
  gtk_box_pack_start(GTK_BOX(vb),w,TRUE,TRUE,0);

  for(i=0;i<2;i++) {
    w=gtk_hbox_new(0,2);
    for(j=0;j<ndir[gtype];j++) {
      if(i==0) d=j;
      else     d=ndir[gtype]*2-j-1; // "clockwise"
      sprop_mkl[d]=gtk_label_new(mklabel[gtype][d]);
      gtk_label_set_width_chars(GTK_LABEL(sprop_mkl[d]),12);
      gtk_misc_set_alignment(GTK_MISC(sprop_mkl[d]),1,0.5);
      gtk_box_pack_start(GTK_BOX(w),sprop_mkl[d],FALSE,FALSE,0);
      sprop_mke[d]=gtk_entry_new();
      gtk_entry_set_max_length(GTK_ENTRY(sprop_mke[d]),5);
      gtk_entry_set_width_chars(GTK_ENTRY(sprop_mke[d]),5);
      gtk_entry_set_text(GTK_ENTRY(sprop_mke[d]),sps[0]->mk[d]);
      gtk_box_pack_start(GTK_BOX(w),sprop_mke[d],FALSE,FALSE,0);
      }
    gtk_box_pack_start(GTK_BOX(vb),w,TRUE,TRUE,0);
    }

  w=gtk_hseparator_new();
  gtk_box_pack_start(GTK_BOX(vb),w,TRUE,TRUE,0);

  sprop_tr=gtk_check_button_new_with_mnemonic("_Flag for answer treatment");
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(sprop_tr),sps[0]->ten);
  gtk_box_pack_start(GTK_BOX(vb),sprop_tr,TRUE,TRUE,0);

  w=gtk_hseparator_new();
  gtk_box_pack_start(GTK_BOX(vb),w,TRUE,TRUE,0);

  w=gtk_hbox_new(0,2);                                                                    gtk_box_pack_start(GTK_BOX(vb),w,TRUE,TRUE,0);
  sprop_l3=gtk_label_new("Lights intersecting here ");                                    gtk_box_pack_start(GTK_BOX(w),sprop_l3,FALSE,FALSE,0);
  gtk_misc_set_alignment(GTK_MISC(sprop_l3),1,0.5);
  sprop_w21=gtk_combo_box_new_text();                                                     gtk_box_pack_start(GTK_BOX(w),sprop_w21,FALSE,FALSE,0);
  gtk_combo_box_append_text(GTK_COMBO_BOX(sprop_w21),"must agree");
  gtk_combo_box_append_text(GTK_COMBO_BOX(sprop_w21),"need not agree: horizontal display");
  gtk_combo_box_append_text(GTK_COMBO_BOX(sprop_w21),"need not agree: vertical display");
  i=sps[0]->dech;
  if(i<0) i=0;
  if(i>2) i=2;
  gtk_combo_box_set_active(GTK_COMBO_BOX(sprop_w21),i);

  if(!g) gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(sprop_or),(sps[0]->spor)&1);
  sprop_setactive();

  gtk_widget_show_all(dia);
  i=gtk_dialog_run(GTK_DIALOG(dia));
  if(i==GTK_RESPONSE_OK) {
    gtk_color_button_get_color(GTK_COLOR_BUTTON(sprop_cbg),&gcbg);
    gtk_color_button_get_color(GTK_COLOR_BUTTON(sprop_cfg),&gcfg);
    gtk_color_button_get_color(GTK_COLOR_BUTTON(sprop_cmk),&gcmk);
    for(j=0;j<nsps;j++) {
      if(!g) sps[j]->spor=gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(sprop_or));
      sps[j]->bgcol=
        (((gcbg.red  >>8)&255)<<16)+
        (((gcbg.green>>8)&255)<< 8)+
        (((gcbg.blue >>8)&255)    );
      sps[j]->fgcol=
        (((gcfg.red  >>8)&255)<<16)+
        (((gcfg.green>>8)&255)<< 8)+
        (((gcfg.blue >>8)&255)    );
      sps[j]->mkcol=
        (((gcmk.red  >>8)&255)<<16)+
        (((gcmk.green>>8)&255)<< 8)+
        (((gcmk.blue >>8)&255)    );
      i=gtk_combo_box_get_active(GTK_COMBO_BOX(sprop_w20)); if(i>=0&&i<4) sps[j]->fstyle=i;
      sps[j]->ten=!!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(sprop_tr));
      i=gtk_combo_box_get_active(GTK_COMBO_BOX(sprop_w21)); if(i>=0&&i<3) sps[j]->dech=i;
      for(d=0;d<ndir[gtype]*2;d++) {
        strncpy(sps[j]->mk[d],gtk_entry_get_text(GTK_ENTRY(sprop_mke[d])),MXMK);
        sps[j]->mk[d][MXMK]=0;
        make7bitclean(sps[j]->mk[d]);
        }
      }
    gridchange();
    }
  gtk_widget_destroy(dia);
  refreshall();
  return 1;
  }


// light properties dialogue

static GtkWidget*lprop_e[MAXNDICTS],*lprop_f[NLEM],*lprop_or,*lprop_tr,*lprop_nn;
static int lprop_g;

static int lprop_setactive() {int i,k;
  k=lprop_g||gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(lprop_or));
  for(i=0;i<MAXNDICTS;i++) gtk_widget_set_sensitive(lprop_e[i],k);
  for(i=0;i<NLEM;i++)   gtk_widget_set_sensitive(lprop_f[i],k);
  gtk_widget_set_sensitive(lprop_tr,k);
  gtk_widget_set_sensitive(lprop_nn,k);
  return 1;
  }

static int lpropdia(int g) {
  GtkWidget*dia,*w,*vb;
  struct lprop*lps[MXLT];
  int nlps;
  int d,i,j;
  char s[SLEN];

  lprop_g=g;
  nlps=0;
  if(g) {lps[0]=&dlp;nlps=1;}
  else {
    if(selmode==1)
      for(d=0;d<ndir[gtype];d++) for(j=0;j<height;j++) for(i=0;i<width;i++) if(isstartoflight(i,j,d)&&issellight(i,j,d)) lps[nlps++]=&gsq[i][j].lp[d];
    if(selmode==2)
      for(i=0;i<nvl;i++) if(vls[i].sel) lps[nlps++]=&vls[i].lp;
    if(selmode==0||nlps==0) {reperr("Please select one or more lights\nwhose properties you wish to change");return 1;}
    }
  dia=gtk_dialog_new_with_buttons(g?"Default light properties":"Selected light properties",
    GTK_WINDOW(mainw),GTK_DIALOG_DESTROY_WITH_PARENT,
    GTK_STOCK_CANCEL,GTK_RESPONSE_CANCEL,GTK_STOCK_APPLY,GTK_RESPONSE_OK,NULL);
  vb=gtk_vbox_new(0,2); // box to hold everything
  gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dia)->vbox),vb,TRUE,TRUE,0);

  if(!g) {
    lprop_or=gtk_check_button_new_with_mnemonic("_Override default light properties");
    gtk_box_pack_start(GTK_BOX(vb),lprop_or,TRUE,TRUE,0);
    gtk_signal_connect(GTK_OBJECT(lprop_or),"clicked",GTK_SIGNAL_FUNC(lprop_setactive),0);
    w=gtk_hseparator_new();
    gtk_box_pack_start(GTK_BOX(vb),w,TRUE,TRUE,0);
    }
  for(i=0;i<MAXNDICTS;i++) {
    sprintf(s,"Use dictionary _%d (",i+1);
    if(strlen(dfnames[i])==0) {
      if(strlen(dafilters[i])==0) strcat(s,"<empty>");
      else {
        strcat(s,"\"");
        if(strlen(dafilters[i])>25) {strncat(s,dafilters[i],22); strcat(s,"...");}
        else strcat(s,dafilters[i]);
        strcat(s,"\"");
        }
      }
    else {
      if(strlen(dfnames[i])>25) {strcat(s,"...");strcat(s,dfnames[i]+strlen(dfnames[i])-22);}
      else strcat(s,dfnames[i]);
      }
    strcat(s,")");
    lprop_e[i]=gtk_check_button_new_with_mnemonic(s);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(lprop_e[i]),((lps[0]->dmask)>>i)&1);
    gtk_box_pack_start(GTK_BOX(vb),lprop_e[i],TRUE,TRUE,0);
    }
  w=gtk_hseparator_new();
  gtk_box_pack_start(GTK_BOX(vb),w,TRUE,TRUE,0);
  lprop_tr=gtk_check_button_new_with_mnemonic("_Enable answer treatment");
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(lprop_tr),lps[0]->ten);
  gtk_box_pack_start(GTK_BOX(vb),lprop_tr,TRUE,TRUE,0);

  w=gtk_hseparator_new();
  gtk_box_pack_start(GTK_BOX(vb),w,TRUE,TRUE,0);
  for(i=0;i<NLEM;i++) {
    sprintf(s,"Allow light to be entered _%s",lemdescADVP[i]);
    lprop_f[i]=gtk_check_button_new_with_mnemonic(s);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(lprop_f[i]),((lps[0]->emask)>>i)&1);
    gtk_box_pack_start(GTK_BOX(vb),lprop_f[i],TRUE,TRUE,0);
    }
  w=gtk_hseparator_new();
  gtk_box_pack_start(GTK_BOX(vb),w,TRUE,TRUE,0);
  lprop_nn=gtk_check_button_new_with_mnemonic("Light _does not receive a number");
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(lprop_nn),lps[0]->dnran);
  gtk_box_pack_start(GTK_BOX(vb),lprop_nn,TRUE,TRUE,0);

  if(!g) gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(lprop_or),(lps[0]->lpor)&1);
  lprop_setactive();
  gtk_widget_show_all(dia);
  i=gtk_dialog_run(GTK_DIALOG(dia));
  if(i==GTK_RESPONSE_OK) {
    for(j=0;j<nlps;j++) {
      if(!g) lps[j]->lpor=gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(lprop_or));
      lps[j]->dmask=0;
      for(i=0;i<MAXNDICTS;i++) if(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(lprop_e[i]))) lps[j]->dmask|=1<<i;
      lps[j]->emask=0;
      for(i=0;i<NLEM;i++) if(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(lprop_f[i]))) lps[j]->emask|=1<<i;
      lps[j]->ten=!!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(lprop_tr));
      lps[j]->dnran=!!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(lprop_nn));
      if(lps[j]->emask==0) {
        reperr("No entry methods selected:\nthis would make fill impossible.\nAllowing lights to be entered normally.");
        lps[j]->emask=EM_FWD;
        }
      }
    gridchange();
    }
  gtk_widget_destroy(dia);
  return 1;
  }

// treatment dialogue

static GtkWidget*treat_a[NMSG],*treat_f,*treat_c,*treat_m[NMSG],*treat_lm[NMSG],*treat_b0,*treat_b1[NMSG],*treat_b2[NMSG],*treat_b3;

static int treatcomboorder[NATREAT]={0,1,2,3,4,10,11,5,6,7,8,9}; // put misprints in the right place

static int treat_browse(GtkWidget*w,gpointer p) {
  GtkWidget*dia;
  DEB1 printf("w=%p p=%p\n",w,p);fflush(stdout);
  dia=gtk_file_chooser_dialog_new("Choose a treatment plug-in",0,
    GTK_FILE_CHOOSER_ACTION_OPEN,
    GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
    GTK_STOCK_OPEN, GTK_RESPONSE_OK,
    NULL);
  if(gtk_dialog_run(GTK_DIALOG(dia))==GTK_RESPONSE_OK)
    gtk_entry_set_text(GTK_ENTRY(treat_f),gtk_file_chooser_get_filename((GtkFileChooser*)dia));
  gtk_widget_destroy(dia);
  return 1;
  }

static int treat_setactive() {int i,j,k,l;
  k=treatcomboorder[gtk_combo_box_get_active(GTK_COMBO_BOX(treat_c))];
  for(i=0;i<NMSG;i++) {
    j=treat_lab[k][i]!=NULL;
    l=gtk_combo_box_get_active(GTK_COMBO_BOX(treat_a[i]));
    gtk_widget_set_sensitive(treat_m[i],j);
    gtk_widget_set_sensitive(treat_b1[i],treatpermok[k][i]);
    gtk_widget_set_sensitive(treat_b2[i],l>0&&treatpermok[k][i]);
    gtk_label_set_text(GTK_LABEL(treat_lm[i]),treat_lab[j?k:(TREAT_PLUGIN)][i]);
    }
  gtk_widget_set_sensitive(treat_b0,k==TREAT_PLUGIN);
  gtk_widget_set_sensitive(treat_b3,k!=0);
  return 1;
  }

static int treatdia(void) {
  GtkWidget*dia,*e[NMSG],*c[NMSG],*l,*b,*vb;
  int i,j;
  char s[(NL+4)*MXFL+1],*p;

  filler_stop();
  dia=gtk_dialog_new_with_buttons("Answer treatment",GTK_WINDOW(mainw),GTK_DIALOG_DESTROY_WITH_PARENT,GTK_STOCK_CANCEL,GTK_RESPONSE_CANCEL,GTK_STOCK_APPLY,GTK_RESPONSE_OK,NULL);
  vb=gtk_vbox_new(0,2); // box to hold everything
  gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dia)->vbox),vb,TRUE,TRUE,0);

  treat_c=gtk_combo_box_new_text();
  for(i=0;i<NATREAT;i++) gtk_combo_box_append_text(GTK_COMBO_BOX(treat_c),tnames[treatcomboorder[i]]);

  gtk_box_pack_start(GTK_BOX(vb),treat_c,FALSE,FALSE,0);

  for(i=0;i<NMSG;i++) {
    treat_m[i]=gtk_hbox_new(0,2);
    treat_lm[i]=gtk_label_new(" ");
    gtk_label_set_width_chars(GTK_LABEL(treat_lm[i]),25);
    gtk_misc_set_alignment(GTK_MISC(treat_lm[i]),1,0.5);
    gtk_box_pack_start(GTK_BOX(treat_m[i]),treat_lm[i],FALSE,FALSE,0);
    e[i]=gtk_entry_new();
    gtk_entry_set_width_chars(GTK_ENTRY(e[i]),30);
    gtk_entry_set_max_length(GTK_ENTRY(e[i]),SLEN-1);
    gtk_entry_set_text(GTK_ENTRY(e[i]),treatmsg[i]);
    gtk_box_pack_start(GTK_BOX(treat_m[i]),e[i],FALSE,FALSE,0);
    gtk_box_pack_start(GTK_BOX(vb),treat_m[i],TRUE,TRUE,0);

    treat_b1[i]=gtk_hbox_new(0,2);
    l=gtk_label_new("Allocate letters ");
    gtk_label_set_width_chars(GTK_LABEL(l),25);
    gtk_misc_set_alignment(GTK_MISC(l),1,0.5);
    gtk_box_pack_start(GTK_BOX(treat_b1[i]),l,FALSE,FALSE,0);
    treat_a[i]=gtk_combo_box_new_text();
    gtk_combo_box_append_text(GTK_COMBO_BOX(treat_a[i]),"in clue order, first come first served");
    gtk_combo_box_append_text(GTK_COMBO_BOX(treat_a[i]),"in clue order, at discretion of filler");
    gtk_combo_box_append_text(GTK_COMBO_BOX(treat_a[i]),"in any order, at discretion of filler");
    gtk_box_pack_start(GTK_BOX(treat_b1[i]),treat_a[i],FALSE,FALSE,0);
    gtk_box_pack_start(GTK_BOX(vb),treat_b1[i],TRUE,TRUE,0);

    treat_b2[i]=gtk_hbox_new(0,2);
    l=gtk_label_new("subject to constraints ");
    gtk_label_set_width_chars(GTK_LABEL(l),25);
    gtk_misc_set_alignment(GTK_MISC(l),1,0.5);
    gtk_box_pack_start(GTK_BOX(treat_b2[i]),l,FALSE,FALSE,0);
    c[i]=gtk_entry_new();
    gtk_entry_set_width_chars(GTK_ENTRY(c[i]),30);
    gtk_entry_set_max_length(GTK_ENTRY(c[i]),(NL+4)*MXFL);
    abmstostr(s,treatcstr[i],MXFL,1);
    j=strlen(s);
    while(j>0&&s[j-1]=='?') j--;
    s[j]=0; // strip trailing "?"s
    gtk_entry_set_text(GTK_ENTRY(c[i]),s);
    gtk_box_pack_start(GTK_BOX(treat_b2[i]),c[i],FALSE,FALSE,0);

    gtk_box_pack_start(GTK_BOX(vb),treat_b2[i],TRUE,TRUE,0);
    }

  treat_b0=gtk_hbox_new(0,2);
  l=gtk_label_new("Treatment plug-in: ");
  gtk_label_set_width_chars(GTK_LABEL(l),25);
  gtk_misc_set_alignment(GTK_MISC(l),1,0.5);
  gtk_box_pack_start(GTK_BOX(treat_b0),l,FALSE,FALSE,0);
  treat_f=gtk_entry_new();
  gtk_entry_set_max_length(GTK_ENTRY(treat_f),SLEN-1);
  gtk_entry_set_text(GTK_ENTRY(treat_f),tpifname);
  gtk_box_pack_start(GTK_BOX(treat_b0),treat_f,FALSE,FALSE,0);
  b=gtk_button_new_with_label("Browse...");
  gtk_box_pack_start(GTK_BOX(treat_b0),b,FALSE,FALSE,0);
  gtk_signal_connect(GTK_OBJECT(b),"clicked",GTK_SIGNAL_FUNC(treat_browse),0);
  gtk_box_pack_start(GTK_BOX(vb),treat_b0,TRUE,TRUE,0);
  treat_b3=gtk_check_button_new_with_mnemonic("Treated answer _must be a word");
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(treat_b3),tambaw);
  gtk_box_pack_start(GTK_BOX(vb),treat_b3,TRUE,TRUE,0);

  gtk_signal_connect(GTK_OBJECT(treat_c),"changed",GTK_SIGNAL_FUNC(treat_setactive),0);
  for(i=0;i<NATREAT;i++) if(treatcomboorder[i]==treatmode) gtk_combo_box_set_active(GTK_COMBO_BOX(treat_c),i);
  for(i=0;i<NMSG;i++) {
    gtk_signal_connect(GTK_OBJECT(treat_a[i]),"changed",GTK_SIGNAL_FUNC(treat_setactive),0);
    gtk_combo_box_set_active(GTK_COMBO_BOX(treat_a[i]),treatorder[i]);
    }
  gtk_widget_show_all(dia);
  i=gtk_dialog_run(GTK_DIALOG(dia));
  if(i==GTK_RESPONSE_OK) {
    strncpy(tpifname,gtk_entry_get_text(GTK_ENTRY(treat_f)),SLEN-1);
    tpifname[SLEN-1]=0;
    treatmode=treatcomboorder[gtk_combo_box_get_active(GTK_COMBO_BOX(treat_c))];
    for(i=0;i<NMSG;i++) {
      strncpy(treatmsg[i],gtk_entry_get_text(GTK_ENTRY(e[i])),SLEN-1);
      treatmsg[i][SLEN-1]=0;
      treatorder[i]=gtk_combo_box_get_active(GTK_COMBO_BOX(treat_a[i]));
      if(treatorder[i]<0) treatorder[i]=0;
      if(treatorder[i]>2) treatorder[i]=2;
      if(!treatpermok[treatmode][i]) treatorder[i]=0;
      j=strtoabms(treatcstr[i],MXFL,(char*)gtk_entry_get_text(GTK_ENTRY(c[i])),1);
      for(;j<MXFL;j++) treatcstr[i][j]=ABM_ALL;
      }
    tambaw=!!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(treat_b3));
    if(treatmode<0||treatmode>=NATREAT) treatmode=0;
    if(treatmode==TREAT_PLUGIN) {
      if((p=loadtpi())) {
        sprintf(s,"Error loading custom plug-in\n%.200s",p);
        reperr(s);
        }
      }
    else unloadtpi();
    }
  gridchange();
  gtk_widget_destroy(dia);
  return 1;
  }



// dictionary list dialogue

static GtkWidget*dictl_e[MAXNDICTS];

static int dictl_browse(GtkWidget*w,gpointer p) {
  GtkWidget*dia;
  DEB1 printf("w=%p p=%p\n",w,p);fflush(stdout);
  dia=gtk_file_chooser_dialog_new("Choose a dictionary",0,
    GTK_FILE_CHOOSER_ACTION_OPEN,
    GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
    GTK_STOCK_OPEN, GTK_RESPONSE_OK,
    NULL);
  if(gtk_dialog_run(GTK_DIALOG(dia))==GTK_RESPONSE_OK)
    gtk_entry_set_text(GTK_ENTRY(dictl_e[(intptr_t)p]),gtk_file_chooser_get_filename((GtkFileChooser*)dia));
  gtk_widget_destroy(dia);
  return 1;
}

static int dictldia(void) {
  GtkWidget*dia,*l,*b,*t,*f0[MAXNDICTS],*f1[MAXNDICTS];
  int i,j;
  char s[SLEN];
  char tdfnames[MAXNDICTS][SLEN];     // Temporary versions of dictionary names, etc
  char tdsfilters[MAXNDICTS][SLEN];
  char tdafilters[MAXNDICTS][SLEN];

  filler_stop();
  dia=gtk_dialog_new_with_buttons("Dictionaries",GTK_WINDOW(mainw),GTK_DIALOG_DESTROY_WITH_PARENT,GTK_STOCK_CANCEL,GTK_RESPONSE_CANCEL,GTK_STOCK_APPLY,GTK_RESPONSE_OK,NULL);
  t=gtk_table_new(MAXNDICTS+1,5,0);
  gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dia)->vbox),t,TRUE,TRUE,0);
  l=gtk_label_new(NULL);gtk_label_set_markup(GTK_LABEL(l),"<b>File</b>"         ); gtk_table_attach(GTK_TABLE(t),l,1,2,0,1,0,0,0,0);
  l=gtk_label_new("");                                                             gtk_table_attach(GTK_TABLE(t),l,3,4,0,1,0,0,10,0);
  l=gtk_label_new(NULL);gtk_label_set_markup(GTK_LABEL(l),"<b>File filter</b>"  ); gtk_table_attach(GTK_TABLE(t),l,4,5,0,1,0,0,0,0);
  l=gtk_label_new(NULL);gtk_label_set_markup(GTK_LABEL(l),"<b>Answer filter</b>"); gtk_table_attach(GTK_TABLE(t),l,5,6,0,1,0,0,0,0);
  for(i=0;i<MAXNDICTS;i++) {
    sprintf(s,"<b>%d</b>",i+1);
    l=gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(l),s);
    gtk_table_attach(GTK_TABLE(t),l,0,1,i+1,i+2,0,0,5,0);
    dictl_e[i]=gtk_entry_new();
    gtk_entry_set_max_length(GTK_ENTRY(dictl_e[i]),SLEN-1);
    gtk_entry_set_text(GTK_ENTRY(dictl_e[i]),dfnames[i]);
    gtk_table_attach(GTK_TABLE(t),dictl_e[i],1,2,i+1,i+2,0,0,0,0);
    b=gtk_button_new_with_label("Browse...");
    gtk_table_attach(GTK_TABLE(t),b,2,3,i+1,i+2,0,0,0,0);
    f0[i]=gtk_entry_new();
    gtk_entry_set_max_length(GTK_ENTRY(f0[i]),SLEN-1);
    gtk_entry_set_text(GTK_ENTRY(f0[i]),dsfilters[i]);
    gtk_table_attach(GTK_TABLE(t),f0[i],4,5,i+1,i+2,0,0,5,0);
    f1[i]=gtk_entry_new();
    gtk_entry_set_max_length(GTK_ENTRY(f1[i]),SLEN-1);
    gtk_entry_set_text(GTK_ENTRY(f1[i]),dafilters[i]);
    gtk_table_attach(GTK_TABLE(t),f1[i],5,6,i+1,i+2,0,0,5,0);
    gtk_signal_connect(GTK_OBJECT(b),"clicked",GTK_SIGNAL_FUNC(dictl_browse),(void*)(intptr_t)i);
    }
  gtk_widget_show_all(dia);
  for(;;) {
    j=gtk_dialog_run(GTK_DIALOG(dia));
    if(j==GTK_RESPONSE_CANCEL||j==GTK_RESPONSE_DELETE_EVENT) goto ew0;
    for(j=0;j<MAXNDICTS;j++) {    // Use names etc from dialog, but keep originals 
      strcpy(tdfnames[j],dfnames[j]);
      strncpy(dfnames[j],gtk_entry_get_text(GTK_ENTRY(dictl_e[j])),SLEN-1);
      dfnames[j][SLEN-1]=0;
      strcpy(tdsfilters[j],dsfilters[j]);
      strncpy(dsfilters[j],gtk_entry_get_text(GTK_ENTRY(f0[j])),SLEN-1);
      dsfilters[j][SLEN-1]=0;
      strcpy(tdafilters[j],dafilters[j]);
      strncpy(dafilters[j],gtk_entry_get_text(GTK_ENTRY(f1[j])),SLEN-1);
      dafilters[j][SLEN-1]=0;
      }
    if(loaddicts(0)==0) break;    // Successful load so close dialog
    for(j=0;j<MAXNDICTS;j++) {    // Else restore original names etc and reload original dictionaries
      strcpy(dfnames[j],tdfnames[j]);
      strcpy(dsfilters[j],tdsfilters[j]);
      strcpy(dafilters[j],tdafilters[j]);
      }
      loaddicts(1);
    }
ew0:
  gridchange();
  gtk_widget_destroy(dia);
  return 1;
  }


// grid properties dialogue

static GtkWidget*gprop_lx,*gprop_ly,*gprop_w20;

static int gtchanged(GtkWidget*w,gpointer p) {
  int i;

  i=gtk_combo_box_get_active(GTK_COMBO_BOX(gprop_w20));
  switch(gshape[i]) { // gtype
  case 0: case 1: case 2:
    gtk_label_set_text(GTK_LABEL(gprop_lx)," columns");
    gtk_label_set_text(GTK_LABEL(gprop_ly)," rows");
    break;
  case 3: case 4:
    gtk_label_set_text(GTK_LABEL(gprop_lx)," radii");
    gtk_label_set_text(GTK_LABEL(gprop_ly)," annuli");
    break;
    }
  return 1;
  }

static int gpropdia(void) {
  GtkWidget*dia,*l,*w,*w30,*w31,*vb,*t,*ttl,*aut;
  int i,m;
  dia=gtk_dialog_new_with_buttons("Grid properties",GTK_WINDOW(mainw),GTK_DIALOG_DESTROY_WITH_PARENT,
    GTK_STOCK_CANCEL,GTK_RESPONSE_CANCEL,GTK_STOCK_APPLY,GTK_RESPONSE_OK,NULL);
  vb=gtk_vbox_new(0,3); // box to hold all the options
  gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dia)->vbox),vb,TRUE,TRUE,0);

  t=gtk_table_new(2,2,0);                                                           gtk_box_pack_start(GTK_BOX(vb),t,TRUE,TRUE,0);
  l=gtk_label_new("Title: ");                                                       gtk_table_attach(GTK_TABLE(t),l,0,1,0,1,0,0,0,0);
  gtk_label_set_width_chars(GTK_LABEL(l),10);
  gtk_misc_set_alignment(GTK_MISC(l),1,0.5);
  ttl=gtk_entry_new();                                                              gtk_table_attach(GTK_TABLE(t),ttl,1,2,0,1,0,0,0,0);
  gtk_entry_set_max_length(GTK_ENTRY(ttl),SLEN-1);
  gtk_entry_set_text(GTK_ENTRY(ttl),gtitle);
  l=gtk_label_new("Author: ");                                                      gtk_table_attach(GTK_TABLE(t),l,0,1,1,2,0,0,0,0);
  gtk_label_set_width_chars(GTK_LABEL(l),10);
  gtk_misc_set_alignment(GTK_MISC(l),1,0.5);
  aut=gtk_entry_new();                                                              gtk_table_attach(GTK_TABLE(t),aut,1,2,1,2,0,0,0,0);
  gtk_entry_set_max_length(GTK_ENTRY(aut),SLEN-1);
  gtk_entry_set_text(GTK_ENTRY(aut),gauthor);


  w=gtk_hseparator_new();                                                           gtk_box_pack_start(GTK_BOX(vb),w,TRUE,TRUE,0);
  w=gtk_hbox_new(0,2);                                                              gtk_box_pack_start(GTK_BOX(vb),w,TRUE,TRUE,0);
  l=gtk_label_new("Grid type: ");                                                   gtk_box_pack_start(GTK_BOX(w),l,FALSE,FALSE,0);
  gtk_label_set_width_chars(GTK_LABEL(l),10);
  gtk_misc_set_alignment(GTK_MISC(l),1,0.5);
  gprop_w20=gtk_combo_box_new_text();                                               gtk_box_pack_start(GTK_BOX(w),gprop_w20,FALSE,FALSE,0);
  for(i=0;i<NGTYPE;i++) gtk_combo_box_append_text(GTK_COMBO_BOX(gprop_w20),gtypedesc[i]);

  w=gtk_hseparator_new();                                                           gtk_box_pack_start(GTK_BOX(vb),w,TRUE,TRUE,0);
  w=gtk_hbox_new(0,2);                                                              gtk_box_pack_start(GTK_BOX(vb),w,TRUE,TRUE,0);
  l=gtk_label_new("Size: ");                                                        gtk_box_pack_start(GTK_BOX(w),l,FALSE,FALSE,0);
  gtk_label_set_width_chars(GTK_LABEL(l),10);
  gtk_misc_set_alignment(GTK_MISC(l),1,0.5);
  w30=gtk_spin_button_new_with_range(1,MXSZ,1);                                     gtk_box_pack_start(GTK_BOX(w),w30,FALSE,FALSE,0);
  gprop_lx=gtk_label_new(" ");                                                      gtk_box_pack_start(GTK_BOX(w),gprop_lx,FALSE,FALSE,0);
  w=gtk_hbox_new(0,2);                                                              gtk_box_pack_start(GTK_BOX(vb),w,TRUE,TRUE,0);
  l=gtk_label_new("by ");                                                           gtk_box_pack_start(GTK_BOX(w),l,FALSE,FALSE,0);
  gtk_label_set_width_chars(GTK_LABEL(l),10);
  gtk_misc_set_alignment(GTK_MISC(l),1,0.5);
  w31=gtk_spin_button_new_with_range(1,MXSZ,1);                                     gtk_box_pack_start(GTK_BOX(w),w31,FALSE,FALSE,0);
  gprop_ly=gtk_label_new(" ");                                                      gtk_box_pack_start(GTK_BOX(w),gprop_ly,FALSE,FALSE,0);
  w=gtk_hseparator_new();                                                           gtk_box_pack_start(GTK_BOX(vb),w,TRUE,TRUE,0);

  gtk_signal_connect(GTK_OBJECT(gprop_w20),"changed",GTK_SIGNAL_FUNC(gtchanged),NULL); // for update of labels

  // set widgets from current values
  gtk_combo_box_set_active(GTK_COMBO_BOX(gprop_w20),gtype);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(w30),width);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(w31),height);

  gtk_widget_show_all(dia);

  i=gtk_dialog_run(GTK_DIALOG(dia));
  if(i==GTK_RESPONSE_OK) {
    // resync everything
    i=gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(w30)); if(i>=1&&i<=MXSZ) width=i;
    i=gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(w31)); if(i>=1&&i<=MXSZ) height=i;
    i=gtk_combo_box_get_active(GTK_COMBO_BOX(gprop_w20)); if(i>=0&&i<NGTYPE) gtype=i;
    strncpy(gtitle ,gtk_entry_get_text(GTK_ENTRY(ttl)),SLEN-1); gtitle [SLEN-1]=0;
    make7bitclean(gtitle);
    strncpy(gauthor,gtk_entry_get_text(GTK_ENTRY(aut)),SLEN-1); gauthor[SLEN-1]=0;
    make7bitclean(gauthor);
    gridchange();
    }

  gtk_widget_destroy(dia);
  draw_init();
  m=symmrmask(); if(((1<<symmr)&m)==0) symmr=1;
  m=symmmmask(); if(((1<<symmm)&m)==0) symmm=0;
  m=symmdmask(); if(((1<<symmd)&m)==0) symmd=0;
  syncgui();
  return 1;
  }


static int mvldia(int v) {
  GtkWidget*dia,*vb,*v0,*w0,*l;
  GtkTextBuffer*tbuf;
  char*p,*q,s[MXLE*10+1];
  int x[MXCL],y[MXCL];
  int i;

  dia=gtk_dialog_new_with_buttons("Modify free light path",GTK_WINDOW(mainw),GTK_DIALOG_DESTROY_WITH_PARENT,
    GTK_STOCK_CANCEL,GTK_RESPONSE_CANCEL,GTK_STOCK_APPLY,GTK_RESPONSE_OK,NULL);
  vb=gtk_vbox_new(0,3);
  gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dia)->vbox),vb,TRUE,TRUE,0);
  l=gtk_label_new("  Enter a coordinate pair for each cell in the path on a  ");   gtk_box_pack_start(GTK_BOX(vb),l,FALSE,TRUE,0);
  gtk_misc_set_alignment(GTK_MISC(l),0,0.5);
  l=gtk_label_new("  separate line. Coordinates are counted from zero.  ");        gtk_box_pack_start(GTK_BOX(vb),l,FALSE,TRUE,0);
  gtk_misc_set_alignment(GTK_MISC(l),0,0.5);
  for(i=0,p=s;i<vls[v].l;i++) sprintf(p,"%d,%d\n",vls[v].x[i],vls[v].y[i]),p+=strlen(p);
  w0=gtk_scrolled_window_new(0,0);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(w0),GTK_POLICY_AUTOMATIC,GTK_POLICY_AUTOMATIC);
  gtk_box_pack_start(GTK_BOX(vb),w0,TRUE,TRUE,0);
  v0=gtk_text_view_new();
  gtk_widget_set_size_request(v0,-1,200);
  tbuf=gtk_text_view_get_buffer(GTK_TEXT_VIEW(v0));
  gtk_text_buffer_set_text(tbuf,s,-1);
  gtk_container_add(GTK_CONTAINER(w0),v0);

  gtk_widget_show_all(dia);

ew0:
  i=gtk_dialog_run(GTK_DIALOG(dia));
  if(i==GTK_RESPONSE_OK) {
    GtkTextIter i0,i1;
    gtk_text_buffer_get_start_iter(tbuf,&i0);
    gtk_text_buffer_get_end_iter  (tbuf,&i1);
    p=gtk_text_buffer_get_text(tbuf,&i0,&i1,FALSE);
    for(i=0;i<=MXCL;i++) {
      while(isspace(*p)) p++; // skips blank lines
      if(*p==0) break;
      if(!isdigit(*p)) goto err0;
      if(i==MXCL) goto err1;
      x[i]=strtol(p,&q,10);
      if(x[i]<0) x[i]=0;
      if(x[i]>=MXSZ) x[i]=MXSZ-1;
      if(q==p) goto err0;
      p=q;
      while(isspace(*p)||*p==',') p++;
      y[i]=strtol(p,&q,10);
      if(y[i]<0) y[i]=0;
      if(y[i]>=MXSZ) y[i]=MXSZ-1;
      if(q==p) goto err0;
      p=q;
      }
    if(i>0) {
      vls[v].l=i;
      memcpy(vls[v].x,x,i*sizeof(int));
      memcpy(vls[v].y,y,i*sizeof(int));
      }
    gridchange();
    }
  gtk_widget_destroy(dia);
  return 1;

err0: reperr("Each line must contain\ntwo numeric coordinate values\nseparated by a comma or space"); goto ew0;
err1: reperr("Free light length limit reached"); goto ew0;
  }


// preferences dialogue
static int prefsdia(void) {
  GtkWidget*dia,*l,*w,*w30,*w31,*w32,*w00,*w01,*w02,*w20,*w21,*w10,*w11,*w12,*w14,*vb;
  int i;
  dia=gtk_dialog_new_with_buttons("Preferences",GTK_WINDOW(mainw),GTK_DIALOG_DESTROY_WITH_PARENT,
    GTK_STOCK_CANCEL,GTK_RESPONSE_CANCEL,GTK_STOCK_APPLY,GTK_RESPONSE_OK,NULL);
  vb=gtk_vbox_new(0,3); // box to hold all the options
  gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dia)->vbox),vb,TRUE,TRUE,0);

  l=gtk_label_new(NULL);gtk_label_set_markup(GTK_LABEL(l),"<b>Export preferences</b>");         gtk_box_pack_start(GTK_BOX(vb),l,TRUE,TRUE,0);
  w=gtk_hbox_new(0,3);                                                                          gtk_box_pack_start(GTK_BOX(vb),w,TRUE,TRUE,0);
  l=gtk_label_new("EPS/SVG export square size: ");                                              gtk_box_pack_start(GTK_BOX(w),l,FALSE,FALSE,0);
  w30=gtk_spin_button_new_with_range(10,72,1);                                                  gtk_box_pack_start(GTK_BOX(w),w30,FALSE,FALSE,0);
  l=gtk_label_new(" points");                                                                   gtk_box_pack_start(GTK_BOX(w),l,FALSE,FALSE,0);
  w=gtk_hseparator_new();                                                                       gtk_box_pack_start(GTK_BOX(vb),w,TRUE,TRUE,0);
  w=gtk_hbox_new(0,3);                                                                          gtk_box_pack_start(GTK_BOX(vb),w,TRUE,TRUE,0);
  l=gtk_label_new("HTML/PNG export square size: ");                                             gtk_box_pack_start(GTK_BOX(w),l,FALSE,FALSE,0);
  w31=gtk_spin_button_new_with_range(10,72,1);                                                  gtk_box_pack_start(GTK_BOX(w),w31,FALSE,FALSE,0);
  l=gtk_label_new(" pixels");                                                                   gtk_box_pack_start(GTK_BOX(w),l,FALSE,FALSE,0);
  w=gtk_hseparator_new();                                                                       gtk_box_pack_start(GTK_BOX(vb),w,TRUE,TRUE,0);
  w32=gtk_check_button_new_with_label("Include numbers in filled grids");                       gtk_box_pack_start(GTK_BOX(vb),w32,TRUE,TRUE,0);
  w=gtk_hseparator_new();                                                                       gtk_box_pack_start(GTK_BOX(vb),w,TRUE,TRUE,0);


  l=gtk_label_new(NULL);gtk_label_set_markup(GTK_LABEL(l),"<b>Editing preferences</b>");        gtk_box_pack_start(GTK_BOX(vb),l,TRUE,TRUE,0);
  w00=gtk_check_button_new_with_label("Clicking corners makes blocks");                         gtk_box_pack_start(GTK_BOX(vb),w00,TRUE,TRUE,0);
  w=gtk_hseparator_new();                                                                       gtk_box_pack_start(GTK_BOX(vb),w,TRUE,TRUE,0);
  w01=gtk_check_button_new_with_label("Clicking edges makes bars");                             gtk_box_pack_start(GTK_BOX(vb),w01,TRUE,TRUE,0);
  w=gtk_hseparator_new();                                                                       gtk_box_pack_start(GTK_BOX(vb),w,TRUE,TRUE,0);
  w02=gtk_check_button_new_with_label("Show numbers and marks while editing");                  gtk_box_pack_start(GTK_BOX(vb),w02,TRUE,TRUE,0);
  w=gtk_hseparator_new();                                                                       gtk_box_pack_start(GTK_BOX(vb),w,TRUE,TRUE,0);

  l=gtk_label_new(NULL);gtk_label_set_markup(GTK_LABEL(l),"<b>Statistics preferences</b>");     gtk_box_pack_start(GTK_BOX(vb),l,TRUE,TRUE,0);
  l=gtk_label_new("Desirable checking ratios");gtk_misc_set_alignment(GTK_MISC(l),0,0.5);       gtk_box_pack_start(GTK_BOX(vb),l,TRUE,TRUE,0);
  w=gtk_hbox_new(0,3);                                                                          gtk_box_pack_start(GTK_BOX(vb),w,TRUE,TRUE,0);
  l=gtk_label_new("Minimum: ");                                                                 gtk_box_pack_start(GTK_BOX(w),l,FALSE,FALSE,0);
  w20=gtk_spin_button_new_with_range(0,100,1);                                                  gtk_box_pack_start(GTK_BOX(w),w20,FALSE,FALSE,0);
  l=gtk_label_new("%   Maximum: ");                                                             gtk_box_pack_start(GTK_BOX(w),l,FALSE,FALSE,0);
  w21=gtk_spin_button_new_with_range(0,100,1);                                                  gtk_box_pack_start(GTK_BOX(w),w21,FALSE,FALSE,0);
  l=gtk_label_new("% plus one cell  ");                                                         gtk_box_pack_start(GTK_BOX(w),l,FALSE,FALSE,0);
  w=gtk_hseparator_new();                                                                       gtk_box_pack_start(GTK_BOX(vb),w,TRUE,TRUE,0);

  l=gtk_label_new(NULL);gtk_label_set_markup(GTK_LABEL(l),"<b>Autofill preferences</b>");       gtk_box_pack_start(GTK_BOX(vb),l,TRUE,TRUE,0);
  w10=gtk_radio_button_new_with_label_from_widget(NULL,"Deterministic");                        gtk_box_pack_start(GTK_BOX(vb),w10,TRUE,TRUE,0);
  w11=gtk_radio_button_new_with_label_from_widget(GTK_RADIO_BUTTON(w10),"Slightly randomised"); gtk_box_pack_start(GTK_BOX(vb),w11,TRUE,TRUE,0);
  w12=gtk_radio_button_new_with_label_from_widget(GTK_RADIO_BUTTON(w11),"Highly randomised");   gtk_box_pack_start(GTK_BOX(vb),w12,TRUE,TRUE,0);
  w=gtk_hseparator_new();                                                                       gtk_box_pack_start(GTK_BOX(vb),w,TRUE,TRUE,0);

  w14=gtk_check_button_new_with_label("Prevent duplicate answers and lights");                  gtk_box_pack_start(GTK_BOX(vb),w14,TRUE,TRUE,0);
  w=gtk_hseparator_new();                                                                       gtk_box_pack_start(GTK_BOX(vb),w,TRUE,TRUE,0);

  // set widgets from current preferences values
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(w30),eptsq);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(w31),hpxsq);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w32),lnis);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w00),clickblock);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w01),clickbar);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w02),shownums);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w14),afunique);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(w20),mincheck);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(w21),maxcheck);
  if(afrandom==0) gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w10),1);
  if(afrandom==1) gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w11),1);
  if(afrandom==2) gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w12),1);

  gtk_widget_show_all(dia);
  i=gtk_dialog_run(GTK_DIALOG(dia));
  if(i==GTK_RESPONSE_OK) {
    // set preferences values back from values (with bounds checking)
    eptsq=gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(w30));
    if(eptsq<10) eptsq=10;
    if(eptsq>72) eptsq=72;
    hpxsq=gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(w31));
    if(hpxsq<10) hpxsq=10;
    if(hpxsq>72) hpxsq=72;
    lnis=gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(w32))&1;
    clickblock=gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(w00))&1;
    clickbar=gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(w01))&1;
    shownums=gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(w02))&1;
    mincheck=gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(w20));
    if(mincheck<  0) mincheck=  0;
    if(mincheck>100) mincheck=100;
    maxcheck=gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(w21));
    if(maxcheck<  0) maxcheck=  0;
    if(maxcheck>100) maxcheck=100;
    afunique=gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(w14));
    if(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(w10))) afrandom=0;
    if(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(w11))) afrandom=1;
    if(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(w12))) afrandom=2;
    saveprefs(); // save to preferences file (failing silently)
    }
  gtk_widget_destroy(dia);
  compute(0); // because of new checking ratios
  invaldaall();
  return 1;
  }

// update statistics window if in view
// main widget table is st_te[][], rows below in st_r[]
void stats_upd(void) {
  int i,j;
  char s[SLEN];

  if(!stats) return;
  for(i=2;i<=MXLE;i++) { // one row for each word length
    if(st_lc[i]>0) {
      sprintf(s,"%d",i);                                                                                      gtk_label_set_text(GTK_LABEL(st_te[i][0]),s);
      sprintf(s,"%d",st_lc[i]);                                                                               gtk_label_set_text(GTK_LABEL(st_te[i][1]),s);
      sprintf(s,st_lucc[i]?"%d (%.1f%%)":" ",st_lucc[i],100.0*st_lucc[i]/st_lc[i]);                           gtk_label_set_text(GTK_LABEL(st_te[i][2]),s);
      sprintf(s,st_locc[i]?"%d (%.1f%%)":" ",st_locc[i],100.0*st_locc[i]/st_lc[i]);                           gtk_label_set_text(GTK_LABEL(st_te[i][3]),s);
      sprintf(s,st_lc[i]?"%.2f:%.2f:%.2f":" "   ,1.0*st_lmnc[i]/i,1.0*st_lsc[i]/st_lc[i]/i,1.0*st_lmxc[i]/i); gtk_label_set_text(GTK_LABEL(st_te[i][4]),s);
      for(j=0;j<5;j++) gtk_widget_show(st_te[i][j]); // show row if non-empty...
      }
    else for(j=0;j<5;j++) {gtk_label_set_text(GTK_LABEL(st_te[i][j])," ");gtk_widget_hide(st_te[i][j]);} // ... and hide row if empty
    }
  sprintf(s,"  Total lights: %d",nw);                                                        gtk_label_set_text(GTK_LABEL(st_r[0]),s);
  if(nw>0) sprintf(s,"  Mean length: %.1f",(double)nc/nw);
  else strcpy (s,"  Mean length: -");                                                        gtk_label_set_text(GTK_LABEL(st_r[1]),s);
  if(nc>0 ) sprintf(s,"  Checked light letters: %d/%d (%.1f%%)",st_sc,nc,100.0*st_sc/nc);
  else      strcpy (s,"  Checked light letters: -");                                          gtk_label_set_text(GTK_LABEL(st_r[2]),s);
  if(ne0>0) sprintf(s,"  Checked grid cells: %d/%d (%.1f%%)",st_ce,ne0,100.0*st_ce/ne0);
  else      strcpy (s,"  Checked grid cells: -");                                             gtk_label_set_text(GTK_LABEL(st_r[3]),s);
  sprintf(s,"  Lights with double unches: %d",st_2u-st_3u);                                  gtk_label_set_text(GTK_LABEL(st_r[4]),s);
  sprintf(s,"  Lights with triple, quadruple etc. unches: %d",st_3u);                        gtk_label_set_text(GTK_LABEL(st_r[5]),s);
  sprintf(s,"  Lights too long for filler: %d",st_tlf);                                      gtk_label_set_text(GTK_LABEL(st_r[6]),s);
  sprintf(s,"  Total free lights: %d",nvl);                                                  gtk_label_set_text(GTK_LABEL(st_r[7]),s);
  sprintf(s,"  Free lights too long for filler: %d",st_vltlf);                               gtk_label_set_text(GTK_LABEL(st_r[8]),s);
  if(st_tmtlf) sprintf(s,"  There are too many treated lights to use filler discretionary modes");
  else         sprintf(s,"  ");
                                                                                             gtk_label_set_text(GTK_LABEL(st_r[9]),s);
  if(hist_da->window) gdk_window_invalidate_rect(hist_da->window,0,0);
  }

// histogram update
static gint hist_configure_event(GtkWidget*widget,GdkEventConfigure*event) {
  DEB4 printf("hist config event: new w=%d h=%d\n",widget->allocation.width,widget->allocation.height);
  return TRUE;
  }

// draw one block of histogram (if non-empty), returning new x coordinate
static int drawhblock(cairo_t*cr,int x,int l,int n,double r,double g,double b) {
  int y0=230,w=12;
  int h,i,m,u,v;
  char s[SLEN];

  u=0; for(i=0;i<n;i++) u+=st_hist[l+i];
  if(u==0) return x; // empty so skip
  u=0; for(i=0;i<NL-1;i++) u+=st_hist[i]; // total in histogram
  m=1; for(i=0;i<NL-1;i++) if(st_hist[i]>m) m=st_hist[i]; // max in histogram
  for(i=0;i<n;i++) {
    v=st_hist[l+i];
    h=(v*150)/m;
    cairo_rectangle(cr,x,y0,w,-h);
    cairo_set_source_rgb(cr,0,0,0);
    cairo_stroke_preserve(cr);
    cairo_set_source_rgb(cr,r,g,b);
    cairo_fill(cr);
    if(v) {
      cairo_set_source_rgb(cr,0,0,0);
      gsave(cr);
      cairo_translate(cr,x+w/2+4,y0-h-4);
      cairo_rotate(cr,-PI/2);
      sprintf(s,"%d (%.1f%%)",v,(v*100.0)/u);
      ltext(cr,s,10,0);
      grestore(cr);
      }
    else  cairo_set_source_rgb(cr,1,0,0);
    s[0]=ltochar[l+i]; s[1]=0;
    ctext(cr,s,x+w/2,y0+14,16,0,0);
    x+=w+4;
    }
  return x+5;
  }

static void drawhist(cairo_t*cr) {
  int x;
  x=drawhblock(cr,5, 0,26,0.0,1.0,0.0);
  x=drawhblock(cr,x,26,10,0.0,1.0,1.0);
  }

// redraw histogram
static gint hist_expose_event(GtkWidget*widget,GdkEventExpose*event) {
  cairo_t*cr;
  DEB4 printf("hist expose event x=%d y=%d w=%d h=%d\n",event->area.x,event->area.y,event->area.width,event->area.height),fflush(stdout);
  if(widget==hist_da) {
    cr=gdk_cairo_create(widget->window);
    cairo_rectangle(cr,event->area.x,event->area.y,event->area.width,event->area.height);
    cairo_clip(cr);
    drawhist(cr);
    cairo_destroy(cr);
    }
  return FALSE;
  }

// create statistics dialogue
static void stats_init(void) {
  int i,j;
  GtkWidget*w0,*w1,*nb,*vb0,*vb1;

  for(i=0;i<MXLE+2;i++) for(j=0;j<5;j++) st_te[i][j]=0;
  stats=gtk_dialog_new_with_buttons("Statistics",GTK_WINDOW(mainw),GTK_DIALOG_DESTROY_WITH_PARENT,GTK_STOCK_CLOSE,GTK_RESPONSE_ACCEPT,NULL);
  gtk_window_set_policy(GTK_WINDOW(stats),FALSE,FALSE,TRUE);
  nb=gtk_notebook_new();
  gtk_box_pack_start(GTK_BOX(GTK_DIALOG(stats)->vbox),nb,FALSE,TRUE,0);

  vb0=gtk_vbox_new(0,3);
  gtk_notebook_append_page(GTK_NOTEBOOK(nb),vb0,gtk_label_new("General"));
  w0=gtk_table_new(MXLE+2,5,FALSE);
  st_te[0][0]=gtk_label_new("  Length  ");
  st_te[0][1]=gtk_label_new("  Count  ");
  st_te[0][2]=gtk_label_new("  Underchecked  ");
  st_te[0][3]=gtk_label_new("  Overchecked  ");
  st_te[0][4]=gtk_label_new("  Check ratio min:mean:max  ");
  for(j=0;j<5;j++) gtk_widget_show(st_te[0][j]);
  w1=gtk_hseparator_new();gtk_table_attach_defaults(GTK_TABLE(w0),w1,0,5,1,2);gtk_widget_show(w1);
  for(i=2;i<=MXLE;i++) for(j=0;j<5;j++) st_te[i][j]=gtk_label_new(""); // initialise all table entries to empty strings
  for(i=0;i<=MXLE;i++) for(j=0;j<5;j++) if(st_te[i][j]) gtk_table_attach_defaults(GTK_TABLE(w0),st_te[i][j],j,j+1,i,i+1);
  w1=gtk_hseparator_new();gtk_table_attach_defaults(GTK_TABLE(w0),w1,0,5,MXLE+1,MXLE+2);gtk_widget_show(w1);
  gtk_box_pack_start(GTK_BOX(vb0),w0,FALSE,TRUE,0);gtk_widget_show(w0);
  for(j=0;j<10;j++) {
    st_r[j]=gtk_label_new(" "); // blank rows at the bottom for now
    gtk_misc_set_alignment(GTK_MISC(st_r[j]),0,0.5);
    gtk_box_pack_start(GTK_BOX(vb0),st_r[j],FALSE,TRUE,0);
    gtk_widget_show(st_r[j]);
    }

  vb1=gtk_vbox_new(0,3);
  gtk_notebook_append_page(GTK_NOTEBOOK(nb),vb1,gtk_label_new("Entry histogram"));

  // drawing area for histogram and events it captures
  hist_da=gtk_drawing_area_new();
  gtk_drawing_area_size(GTK_DRAWING_AREA(hist_da),588,250);
  gtk_widget_set_events(hist_da,GDK_EXPOSURE_MASK);
  gtk_signal_connect(GTK_OBJECT(hist_da),"expose_event",GTK_SIGNAL_FUNC(hist_expose_event),NULL);
  gtk_signal_connect(GTK_OBJECT(hist_da),"configure_event",GTK_SIGNAL_FUNC(hist_configure_event),NULL);
  gtk_box_pack_start(GTK_BOX(vb1),hist_da,FALSE,TRUE,0);gtk_widget_show(hist_da);

  gtk_widget_show(hist_da);
  gtk_widget_show(vb1);
  gtk_widget_show(vb0);
  gtk_widget_show(nb);
  }

// remove statistics window (if not already gone or in the process of going)
static void stats_quit(GtkDialog*w,int i0,void*p0) {
  DEB4 printf("stats_quit()\n"),fflush(stdout);
  if(i0!=GTK_RESPONSE_DELETE_EVENT&&stats!=NULL) gtk_widget_destroy(stats);
  stats=NULL;
  DEB4 printf("stats_quit()\n"),fflush(stdout);
  }

// open stats window if not already open, and update it
static int statsdia(void) {
  if(!stats) stats_init();
  stats_upd();
  gtk_widget_show(stats);
  gtk_signal_connect(GTK_OBJECT(stats),"response",GTK_SIGNAL_FUNC(stats_quit),NULL); // does repeating this matter?
  return 0;
  }

void killcurrdia(void) {
  if(currentdia) gtk_dialog_response(GTK_DIALOG(currentdia),GTK_RESPONSE_CANCEL); // kill the current dialogue box
  }

void setposslabel(char*s) {
  gtk_label_set_text(GTK_LABEL(poss_label),s);
  }

// update feasible list to screen
void updatefeas(void) {int i; char t0[MXFL*3+100],t1[MXLE+50],*u[2],p0[SLEN],p1[SLEN];
  DEB1 printf("updatefeas()\n");
  u[0]=t0;
  u[1]=t1;
  p1[0]='\0';
  gtk_clist_freeze(GTK_CLIST(clist)); // avoid glitchy-looking updates
  gtk_clist_clear(GTK_CLIST(clist));
  if(!isclear(curx,cury)) goto ew0;
  for(i=0;i<llistn;i++) { // add in list entries
    ansform(t0,sizeof(t0),llistp[i],llistwlen,llistdm);
    sprintf(t1,"%+.1f",log10(ansp[lts[llistp[i]].ans]->score)); // negative ans values cannot occur here
    gtk_clist_append(GTK_CLIST(clist),u);
    }
  if(getechar(curx,cury)==' ') {
    if(gsq[curx][cury].e0->flbm==0) strcpy(p0,"");
    else getposs(gsq[curx][cury].e0,p0,0,0); // get feasible letter list with dash suppressed
    if(strlen(p0)==0) sprintf(p1," No feasible characters");
    else sprintf(p1," Feasible character%s: %s",(strlen(p0)==1)?"":"s",p0);
  } else p1[0]=0;
ew0:
  gtk_clist_thaw(GTK_CLIST(clist)); // make list visible
  DEB1 printf("Setting poss label to >%s<\n",p1);
  setposslabel(p1);
  return;
  }

static void syncselmenu() {
  DEB1 printf("syncselmenu mode=%d,n=%d\n",selmode,nsel);
  gtk_widget_set_sensitive((GtkWidget*)gtk_item_factory_get_widget_by_action(item_factory,0xf100),selmode==0&&nsel>0);
  gtk_widget_set_sensitive((GtkWidget*)gtk_item_factory_get_widget_by_action(item_factory,0xf201),selmode==2&&nsel==1);
  gtk_widget_set_sensitive((GtkWidget*)gtk_item_factory_get_widget_by_action(item_factory,0xf202),selmode==2&&nsel==1);
  gtk_widget_set_sensitive((GtkWidget*)gtk_item_factory_get_widget_by_action(item_factory,0xf203),selmode==2&&nsel==1);
  gtk_widget_set_sensitive((GtkWidget*)gtk_item_factory_get_widget_by_action(item_factory,0xf204),selmode==2&&nsel>0);
  gtk_widget_set_sensitive((GtkWidget*)gtk_item_factory_get_widget_by_action(item_factory,0xf300),selmode==0&&nsel>0);
  gtk_widget_set_sensitive((GtkWidget*)gtk_item_factory_get_widget_by_action(item_factory,0xf301),(selmode==1||selmode==2)&&nsel>0);
  }

static void syncsymmmenu() {int i,m;
  gtk_menu_item_activate((GtkMenuItem*)gtk_item_factory_get_widget_by_action(item_factory,0x100+symmr));
  gtk_menu_item_activate((GtkMenuItem*)gtk_item_factory_get_widget_by_action(item_factory,0x200+symmm));
  gtk_menu_item_activate((GtkMenuItem*)gtk_item_factory_get_widget_by_action(item_factory,0x300+symmd));
  m=symmrmask(); for(i=1;i<=12;i++) gtk_widget_set_sensitive((GtkWidget*)gtk_item_factory_get_widget_by_action(item_factory,0x100+i),(m>>i)&1);
  m=symmmmask(); for(i=0;i<= 3;i++) gtk_widget_set_sensitive((GtkWidget*)gtk_item_factory_get_widget_by_action(item_factory,0x200+i),(m>>i)&1);
  m=symmdmask(); for(i=0;i<= 3;i++) gtk_widget_set_sensitive((GtkWidget*)gtk_item_factory_get_widget_by_action(item_factory,0x300+i),(m>>i)&1);
  i=(gshape[gtype]==3||gshape[gtype]==4);
  gtk_widget_set_sensitive((GtkWidget*)gtk_item_factory_get_widget_by_action(item_factory,0xf000),!i);
  gtk_widget_set_sensitive((GtkWidget*)gtk_item_factory_get_widget_by_action(item_factory,0xf001), i);
  gtk_widget_set_sensitive((GtkWidget*)gtk_item_factory_get_widget_by_action(item_factory,0xf002), i);
  i=(gshape[gtype]>0);
  gtk_widget_set_sensitive((GtkWidget*)gtk_item_factory_get_widget_by_action(item_factory,0x404),!i);
  gtk_widget_set_sensitive((GtkWidget*)gtk_item_factory_get_widget_by_action(item_factory,0x414),!i);
  gtk_widget_set_sensitive((GtkWidget*)gtk_item_factory_get_widget_by_action(item_factory,0x433),!i);
  gtk_widget_set_sensitive((GtkWidget*)gtk_item_factory_get_widget_by_action(item_factory,0x443),!i);
  }

// sync GUI with possibly new grid properties, flags, symmetry, width, height
void syncgui(void) {
  if(!isingrid(curx,cury)) {curx=0,cury=0;} // keep cursor in grid
  if(dir>=ndir[gtype]&&dir<100) dir=0; // make sure direction is feasible
  if(dir>=100+nvl) dir=0;
  gtk_drawing_area_size(GTK_DRAWING_AREA(grid_da),dawidth()+3,daheight()+3);
  gtk_widget_show(grid_da);
  syncsymmmenu();
  syncselmenu();
  setwintitle();
  draw_init();
  refreshall(0,shownums?7:3);
  curmoved();
  }

// menus
static GtkItemFactoryEntry menu_items[] = {
 { "/_File",                                          0,                   0,              0,     "<Branch>"},
 { "/File/_New",                                      0,                   0,              0,     "<Branch>"},
 { "/File/New/Current shape and size",                "<control>N",        m_filenew,      0,     "<StockItem>",GTK_STOCK_NEW},

 { "/File/New/Blank 9x9",                             0,                   m_filenew,      0x090980},
 { "/File/New/Blank 10x10",                           0,                   m_filenew,      0x0a0a80},
 { "/File/New/Blank 11x11",                           0,                   m_filenew,      0x0b0b80},
 { "/File/New/Blank 11x13",                           0,                   m_filenew,      0x0b0d80},
 { "/File/New/Blank 12x12",                           0,                   m_filenew,      0x0c0c80},
 { "/File/New/Blank 13x11",                           0,                   m_filenew,      0x0d0b80},
 { "/File/New/Blank 13x13",                           0,                   m_filenew,      0x0d0d80},
 { "/File/New/Blank 14x14",                           0,                   m_filenew,      0x0e0e80},
 { "/File/New/Blank 15x15",                           0,                   m_filenew,      0x0f0f80},

 { "/File/New/Blocked 13x13 template",                         0,                   0,              0,     "<Branch>"},
 { "/File/New/Blocked 13x13 template/No unches on edges",      0,                   m_filenew,      0x0d0d00},
 { "/File/New/Blocked 13x13 template/Unches left and right",   0,                   m_filenew,      0x0d0d01},
 { "/File/New/Blocked 13x13 template/Unches top and bottom",   0,                   m_filenew,      0x0d0d02},
 { "/File/New/Blocked 13x13 template/Unches on all edges",     0,                   m_filenew,      0x0d0d03},

 { "/File/New/Blocked 15x15 template",                         0,                   0,              0,       "<Branch>"},
 { "/File/New/Blocked 15x15 template/No unches on edges",      0,                   m_filenew,      0x0f0f00},
 { "/File/New/Blocked 15x15 template/Unches left and right",   0,                   m_filenew,      0x0f0f01},
 { "/File/New/Blocked 15x15 template/Unches top and bottom",   0,                   m_filenew,      0x0f0f02},
 { "/File/New/Blocked 15x15 template/Unches on all edges",     0,                   m_filenew,      0x0f0f03},

 { "/File/New/Blocked 17x17 template",                         0,                   0,              0,       "<Branch>"},
 { "/File/New/Blocked 17x17 template/No unches on edges",      0,                   m_filenew,      0x111100},
 { "/File/New/Blocked 17x17 template/Unches left and right",   0,                   m_filenew,      0x111101},
 { "/File/New/Blocked 17x17 template/Unches top and bottom",   0,                   m_filenew,      0x111102},
 { "/File/New/Blocked 17x17 template/Unches on all edges",     0,                   m_filenew,      0x111103},

 { "/File/New/Blocked 19x19 template",                         0,                   0,              0,       "<Branch>"},
 { "/File/New/Blocked 19x19 template/No unches on edges",      0,                   m_filenew,      0x131300},
 { "/File/New/Blocked 19x19 template/Unches left and right",   0,                   m_filenew,      0x131301},
 { "/File/New/Blocked 19x19 template/Unches top and bottom",   0,                   m_filenew,      0x131302},
 { "/File/New/Blocked 19x19 template/Unches on all edges",     0,                   m_filenew,      0x131303},

 { "/File/New/Blocked 21x21 template",                         0,                   0,              0,       "<Branch>"},
 { "/File/New/Blocked 21x21 template/No unches on edges",      0,                   m_filenew,      0x151500},
 { "/File/New/Blocked 21x21 template/Unches left and right",   0,                   m_filenew,      0x151501},
 { "/File/New/Blocked 21x21 template/Unches top and bottom",   0,                   m_filenew,      0x151502},
 { "/File/New/Blocked 21x21 template/Unches on all edges",     0,                   m_filenew,      0x151503},

 { "/File/New/Blocked 23x23 template",                         0,                   0,              0,       "<Branch>"},
 { "/File/New/Blocked 23x23 template/No unches on edges",      0,                   m_filenew,      0x171700},
 { "/File/New/Blocked 23x23 template/Unches left and right",   0,                   m_filenew,      0x171701},
 { "/File/New/Blocked 23x23 template/Unches top and bottom",   0,                   m_filenew,      0x171702},
 { "/File/New/Blocked 23x23 template/Unches on all edges",     0,                   m_filenew,      0x171703},

 { "/File/New/Blocked 25x25 template",                         0,                   0,              0,       "<Branch>"},
 { "/File/New/Blocked 25x25 template/No unches on edges",      0,                   m_filenew,      0x191900},
 { "/File/New/Blocked 25x25 template/Unches left and right",   0,                   m_filenew,      0x191901},
 { "/File/New/Blocked 25x25 template/Unches top and bottom",   0,                   m_filenew,      0x191902},
 { "/File/New/Blocked 25x25 template/Unches on all edges",     0,                   m_filenew,      0x191903},

 { "/File/sep0",                                      0,                   0,              0,     "<Separator>"},
 { "/File/_Open...",                                  "<control>O",        m_fileopen,     0,     "<StockItem>",GTK_STOCK_OPEN},
 { "/File/_Save",                                     "<control>S",        m_filesave,     0,     "<StockItem>",GTK_STOCK_SAVE},
 { "/File/Save _as...",                               0,                   m_filesaveas,   0,     "<StockItem>",GTK_STOCK_SAVE_AS},
 { "/File/sep1",                                      0,                   0,              0,     "<Separator>"},
 { "/File/Export _blank grid image",                  0,                   0,              0,     "<Branch>"},
 { "/File/Export blank grid image/as _EPS...",        0,                   m_fileexport,   0x401},
 { "/File/Export blank grid image/as _SVG...",        0,                   m_fileexport,   0x402},
 { "/File/Export blank grid image/as _PNG...",        0,                   m_fileexport,   0x403},
 { "/File/Export blank grid image/as _HTML...",       0,                   m_fileexport,   0x404},
 { "/File/Export _filled grid image",                 0,                   0,              0,     "<Branch>"},
 { "/File/Export filled grid image/as _EPS...",       0,                   m_fileexport,   0x411},
 { "/File/Export filled grid image/as _SVG...",       0,                   m_fileexport,   0x412},
 { "/File/Export filled grid image/as _PNG...",       0,                   m_fileexport,   0x413},
 { "/File/Export filled grid image/as _HTML...",      0,                   m_fileexport,   0x414},
 { "/File/Export ans_wers",                           0,                   0,              0,     "<Branch>"},
 { "/File/Export answers/As _text...",                0,                   m_fileexport,   0x420},
 { "/File/Export answers/As _HTML...",                0,                   m_fileexport,   0x423},
 { "/File/Export _puzzle",                            0,                   0,              0,     "<Branch>"},
 { "/File/Export puzzle/As _HTML...",                 0,                   m_fileexport,   0x433},
 { "/File/Export puzzle/As HTML+_SVG...",             0,                   m_fileexport,   0x434},
 { "/File/Export puzzle/As HTML+_PNG...",             0,                   m_fileexport,   0x435},
 { "/File/Export so_lution",                          0,                   0,              0,     "<Branch>"},
 { "/File/Export solution/As _HTML...",               0,                   m_fileexport,   0x443},
 { "/File/Export solution/As HTML+_SVG...",           0,                   m_fileexport,   0x444},
 { "/File/Export solution/As HTML+_PNG...",           0,                   m_fileexport,   0x445},
 { "/File/sep2",                                      0,                   0,              0,     "<Separator>"},
 { "/File/I_mport free light paths",                  0,                   m_importvls},
 { "/File/E_xport free light paths",                  0,                   m_exportvls},
 { "/File/sep3",                                      0,                   0,              0,     "<Separator>"},
 { "/File/_Quit",                                     "<control>Q",        m_filequit,     0,     "<StockItem>",GTK_STOCK_QUIT},
 { "/_Edit",                                          0,                   0,              0,     "<Branch>"},
 { "/Edit/_Undo",                                     "<control>Z",        m_undo,         0,     "<StockItem>",GTK_STOCK_UNDO},
 { "/Edit/_Redo",                                     "<control>Y",        m_redo,         0,     "<StockItem>",GTK_STOCK_REDO},
 { "/Edit/sep1",                                      0,                   0,              0,     "<Separator>"},
 { "/Edit/_Solid block",                              "Insert",            m_editblock},
 { "/Edit/_Bar before",                               "Return",            m_editbarb},
 { "/Edit/_Empty",                                    "Delete",            m_editempty},
 { "/Edit/_Cutout",                                   "<control>C",        m_editcutout},
 { "/Edit/_Merge with next",                          "<control>M",        m_editmerge},
 { "/Edit/sep2",                                      0,                   0,              0,     "<Separator>"},
 { "/Edit/Cell c_ontents...",                         "<control>I",        m_cellcont,     0},
 { "/Edit/Clear _all cells",                          "<control>X",        m_eraseall,     0,     "<StockItem>",GTK_STOCK_CLEAR},
 { "/Edit/C_lear selected cells",                     "<shift><control>X", m_erasesel,     0xf100,"<StockItem>",GTK_STOCK_CLEAR},
 { "/Edit/sep3",                                      0,                   0,              0,     "<Separator>"},
 { "/Edit/_Free light",                               0,                   0,              0,     "<Branch>"},
 { "/Edit/Free light/_Start new",                     0,                   m_vlnew,        0xf200},
 { "/Edit/Free light/_Extend selected",               "<control>E",        m_vlextend,     0xf201},
 { "/Edit/Free light/_Shorten selected",              "<control>D",        m_vlcurtail,    0xf202},
 { "/Edit/Free light/_Modify selected",               0,                   m_vlmodify,     0xf203},
 { "/Edit/Free light/_Delete selected",               0,                   m_vldelete,     0xf204},
 { "/Edit/sep4",                                      0,                   0,              0,     "<Separator>"},
 { "/Edit/Flip in main _diagonal",                    0,                   m_editflip,     0xf000},
 { "/Edit/Rotate cloc_kwise",                         "greater",           m_editrot,      0xf001},
 { "/Edit/Rotate a_nticlockwise",                     "less",              m_editrot,      0xf002},
 { "/Edit/sep5",                                      0,                   0,              0,     "<Separator>"},
 { "/Edit/_Zoom",                                     0,                   0,              0,     "<Branch>"},
 { "/Edit/Zoom/_Out",                                 "<control>minus",    m_zoom,         -2},
 { "/Edit/Zoom/_1 50%",                               "<control>8",        m_zoom,         0},
 { "/Edit/Zoom/_2 71%",                               "<control>9",        m_zoom,         1},
 { "/Edit/Zoom/_3 100%",                              "<control>0",        m_zoom,         2},
 { "/Edit/Zoom/_4 141%",                              "<control>1",        m_zoom,         3},
 { "/Edit/Zoom/_5 200%",                              "<control>2",        m_zoom,         4},
 { "/Edit/Zoom/_In",                                  "<control>plus",     m_zoom,         -1},
 { "/Edit/Show s_tatistics",                          0,                   m_showstats},
 { "/Edit/_Preferences...",                           0,                   m_editprefs,    0,     "<StockItem>", GTK_STOCK_PREFERENCES},
 { "/_Properties",                                    0,                   0,              0,     "<Branch>"},
 { "/Properties/_Grid properties...",                 0,                   m_editgprop,    0,     "<StockItem>",GTK_STOCK_PROPERTIES},
 { "/Properties/Default _cell properties...",         0,                   m_dsprop,       0},
 { "/Properties/Selected c_ell properties...",        0,                   m_sprop,        0xf300},
 { "/Properties/Default _light properties...",        0,                   m_dlprop,       0},
 { "/Properties/Selected l_ight properties...",       0,                   m_lprop,        0xf301},
 { "/_Select",                                        0,                   0,              0,     "<Branch>"},
 { "/Select/Current _cell",                           "<shift>C",          m_selcell},
 { "/Select/Current _light",                          "<shift>L",          m_sellight},
 { "/Select/Cell _mode <> light mode",                "<shift>M",          m_selmode},
 { "/Select/_Free light",                             "<shift>F",          m_selfvl},
 { "/Select/sep0",                                    0,                   0,              0,     "<Separator>"},
 { "/Select/_All",                                    "<shift>A",          m_selall},
 { "/Select/_Invert",                                 "<shift>I",          m_selinv},
 { "/Select/_Nothing",                                "<shift>N",          m_selnone},
 { "/Select/sep1",                                    0,                   0,              0,     "<Separator>"},
 { "/Select/Cell_s",                                  0,                   0,              0,     "<Branch>"},
 { "/Select/Cells/overriding default _properties",    0,                   m_selcover},
 { "/Select/Cells/flagged for _answer treatment",     0,                   m_selctreat},
 { "/Select/Cells/that are _unchecked",               0,                   m_selcunch},
 { "/Select/Li_ghts",                                 0,                   0,              0,     "<Branch>"},
 { "/Select/Lights/_in current direction",            0,                   m_sellpar},
 { "/Select/Lights/overriding default _properties",   0,                   m_sellover},
 { "/Select/Lights/with answer treatment _enabled",   0,                   m_selltreat},
 { "/Select/Lights/with _double or more unches",      0,                   m_selviol,      1},
 { "/Select/Lights/with _triple or more unches",      0,                   m_selviol,      2},
 { "/Select/Lights/that are _underchecked",           0,                   m_selviol,      4},
 { "/Select/Lights/that are _overchecked",            0,                   m_selviol,      8},
 { "/Sy_mmetry",                                      0,                   0,              0,     "<Branch>"},
 { "/Symmetry/No rotational",                         0,                   m_symm0,        0x0101,"<RadioItem>"},
 { "/Symmetry/Twofold rotational",                    0,                   m_symm0,        0x0102,"/Symmetry/No rotational"         },
 { "/Symmetry/Threefold rotational",                  0,                   m_symm0,        0x0103,"/Symmetry/Twofold rotational"    },
 { "/Symmetry/Fourfold rotational",                   0,                   m_symm0,        0x0104,"/Symmetry/Threefold rotational"  },
 { "/Symmetry/Fivefold rotational",                   0,                   m_symm0,        0x0105,"/Symmetry/Fourfold rotational"   },
 { "/Symmetry/Sixfold rotational",                    0,                   m_symm0,        0x0106,"/Symmetry/Fivefold rotational"   },
 { "/Symmetry/Sevenfold rotational",                  0,                   m_symm0,        0x0107,"/Symmetry/Sixfold rotational"    },
 { "/Symmetry/Eightfold rotational",                  0,                   m_symm0,        0x0108,"/Symmetry/Sevenfold rotational"  },
 { "/Symmetry/Ninefold rotational",                   0,                   m_symm0,        0x0109,"/Symmetry/Eightfold rotational"  },
 { "/Symmetry/Tenfold rotational",                    0,                   m_symm0,        0x010a,"/Symmetry/Ninefold rotational"   },
 { "/Symmetry/Elevenfold rotational",                 0,                   m_symm0,        0x010b,"/Symmetry/Tenfold rotational"    },
 { "/Symmetry/Twelvefold rotational",                 0,                   m_symm0,        0x010c,"/Symmetry/Elevenfold rotational" },
 { "/Symmetry/sep1",                                  0,                   0,              0,     "<Separator>"},
 { "/Symmetry/No mirror",                             0,                   m_symm1,        0x0200,"<RadioItem>"},
 { "/Symmetry/Left-right mirror",                     0,                   m_symm1,        0x0201,"/Symmetry/No mirror"},
 { "/Symmetry/Up-down mirror",                        0,                   m_symm1,        0x0202,"/Symmetry/Left-right mirror"},
 { "/Symmetry/Both",                                  0,                   m_symm1,        0x0203,"/Symmetry/Up-down mirror"},
 { "/Symmetry/sep2",                                  0,                   0,              0,     "<Separator>"},
 { "/Symmetry/No duplication",                        0,                   m_symm2,        0x0300,"<RadioItem>"},
 { "/Symmetry/Left-right duplication",                0,                   m_symm2,        0x0301,"/Symmetry/No duplication"},
 { "/Symmetry/Up-down duplication",                   0,                   m_symm2,        0x0302,"/Symmetry/Left-right duplication"},
 { "/Symmetry/Both",                                  0,                   m_symm2,        0x0303,"/Symmetry/Up-down duplication"},
 { "/_Autofill",                                      0,                   0,              0,     "<Branch>"},
 { "/Autofill/_Dictionaries...",                      0,                   m_dictionaries},
 { "/Autofill/Answer _treatment...",                  0,                   m_afctreat},
 { "/Autofill/sep1",                                  0,                   0,              0,     "<Separator>"},
 { "/Autofill/Auto_fill",                             "<control>G",        m_autofill,     1,     "<StockItem>",GTK_STOCK_EXECUTE},
 { "/Autofill/Autofill _selected cells",              "<shift><control>G", m_autofill,     2,     "<StockItem>",GTK_STOCK_EXECUTE},
 { "/Autofill/Accept _hints",                         "<control>A",        m_accept},
 { "/Help",                                           0,                   0,              0,     "<LastBranch>"},
 { "/Help/About",                                     0,                   m_helpabout,    0,     "<StockItem>",GTK_STOCK_ABOUT},
};

// build main window and other initialisation
void startgtk(void) {
  GtkAccelGroup*accel_group;
  GtkWidget *list_sw,*vbox,*hbox,*paned,*menubar,*zmin,*zmout; // main window and content

  pxsq=zoompx[zoomf];

  mainw=gtk_window_new(GTK_WINDOW_TOPLEVEL); // main window
  gtk_widget_set_name(mainw,"Qxw");
  gtk_window_set_default_size(GTK_WINDOW(mainw),780,560);
  gtk_window_set_title(GTK_WINDOW(mainw),"Qxw");
  gtk_window_set_position(GTK_WINDOW(mainw),GTK_WIN_POS_CENTER);

  // box in the window
  vbox=gtk_vbox_new(FALSE,0);
  gtk_container_add(GTK_CONTAINER(mainw),vbox);

  // menu in the vbox
  accel_group=gtk_accel_group_new();
  item_factory=gtk_item_factory_new(GTK_TYPE_MENU_BAR,"<main>",accel_group);
  gtk_item_factory_create_items(item_factory,sizeof(menu_items)/sizeof(menu_items[0]),menu_items,NULL);
  gtk_window_add_accel_group(GTK_WINDOW(mainw),accel_group);
  menubar=gtk_item_factory_get_widget(item_factory,"<main>");
  gtk_box_pack_start(GTK_BOX(vbox),menubar,FALSE,TRUE,0);

  // window is divided into two parts, or `panes'
  paned=gtk_hpaned_new();
  gtk_box_pack_start(GTK_BOX(vbox),paned,TRUE,TRUE,0);

  // scrolled windows in the panes
  grid_sw=gtk_scrolled_window_new(NULL,NULL);
  gtk_container_set_border_width(GTK_CONTAINER(grid_sw),10);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(grid_sw),GTK_POLICY_AUTOMATIC,GTK_POLICY_AUTOMATIC);
  gtk_paned_pack1(GTK_PANED(paned),grid_sw,1,1);

  list_sw=gtk_scrolled_window_new(NULL,NULL);
  gtk_container_set_border_width(GTK_CONTAINER(list_sw),10);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(list_sw),GTK_POLICY_AUTOMATIC,GTK_POLICY_AUTOMATIC);
  gtk_paned_pack2(GTK_PANED(paned),list_sw,0,1);
  gtk_paned_set_position(GTK_PANED(paned),560);

  // drawing area for grid and events it captures
  grid_da=gtk_drawing_area_new();
  gtk_drawing_area_size(GTK_DRAWING_AREA(grid_da),100,100);
  gtk_scrolled_window_add_with_viewport(GTK_SCROLLED_WINDOW(grid_sw),grid_da);
  GTK_WIDGET_SET_FLAGS(grid_da,GTK_CAN_FOCUS);
  gtk_widget_set_events(grid_da,GDK_EXPOSURE_MASK|GDK_BUTTON_PRESS_MASK|GDK_BUTTON_RELEASE_MASK|GDK_KEY_PRESS_MASK|GDK_POINTER_MOTION_MASK);

  // list of feasible words
//  clist=gtk_clist_new(2);
  clist=gtk_clist_new(1);
  gtk_clist_set_column_width(GTK_CLIST(clist),0,180);
//   gtk_clist_set_column_width(GTK_CLIST(clist),1,80);
  gtk_clist_set_column_title(GTK_CLIST(clist),0,"Feasible words");
//  gtk_clist_set_column_title(GTK_CLIST(clist),1,"Scores");
  gtk_clist_column_titles_passive(GTK_CLIST(clist));
  gtk_clist_column_titles_show(GTK_CLIST(clist));
  gtk_clist_set_selection_mode(GTK_CLIST(clist),GTK_SELECTION_SINGLE);
  gtk_container_add(GTK_CONTAINER(list_sw),clist);

  // box for widgets across the bottom of the window
  hbox=gtk_hbox_new(FALSE,0);
  zmout=gtk_button_new();  gtk_button_set_image(GTK_BUTTON(zmout),gtk_image_new_from_stock(GTK_STOCK_ZOOM_OUT,GTK_ICON_SIZE_MENU)); gtk_box_pack_start(GTK_BOX(hbox),zmout,FALSE,FALSE,0);
  gtk_signal_connect(GTK_OBJECT(zmout),"clicked",GTK_SIGNAL_FUNC(m_zoom),(gpointer)-2);
  zmin =gtk_button_new();  gtk_button_set_image(GTK_BUTTON(zmin ),gtk_image_new_from_stock(GTK_STOCK_ZOOM_IN ,GTK_ICON_SIZE_MENU)); gtk_box_pack_start(GTK_BOX(hbox),zmin ,FALSE,FALSE,0);
  gtk_signal_connect(GTK_OBJECT(zmin ),"clicked",GTK_SIGNAL_FUNC(m_zoom),(gpointer)-1);
  poss_label=gtk_label_new(" Feasible characters:");
  gtk_box_pack_start(GTK_BOX(hbox),poss_label,FALSE,FALSE,0);
  gtk_box_pack_end(GTK_BOX(vbox),hbox,FALSE,FALSE,0);

  gtk_signal_connect(GTK_OBJECT(grid_da),"expose_event",GTK_SIGNAL_FUNC(expose_event),NULL);
  gtk_signal_connect(GTK_OBJECT(grid_da),"configure_event",GTK_SIGNAL_FUNC(configure_event),NULL);
  gtk_signal_connect(GTK_OBJECT(clist),"select_row",GTK_SIGNAL_FUNC(selrow),NULL);

  gtk_signal_connect(GTK_OBJECT(grid_da),"button_press_event",GTK_SIGNAL_FUNC(button_press_event),NULL);
  gtk_signal_connect(GTK_OBJECT(grid_da),"button_release_event",GTK_SIGNAL_FUNC(button_press_event),NULL);
  gtk_signal_connect_after(GTK_OBJECT(grid_da),"key_press_event",GTK_SIGNAL_FUNC(keypress),NULL);
  gtk_signal_connect(GTK_OBJECT(grid_da),"motion_notify_event",GTK_SIGNAL_FUNC(mousemove),NULL);

  gtk_signal_connect(GTK_OBJECT(mainw),"delete_event",GTK_SIGNAL_FUNC(w_delete),NULL);
  gtk_signal_connect(GTK_OBJECT(mainw),"destroy",GTK_SIGNAL_FUNC(w_destroy),NULL);

  gtk_widget_show_all(mainw);
  gtk_window_set_focus(GTK_WINDOW(mainw),grid_da);
  }

void stopgtk(void) {
  }


