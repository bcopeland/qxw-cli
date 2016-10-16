// $Id: common.h 556 2014-04-01 14:12:56Z mo $

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


#ifndef __COMMON_H__
#define __COMMON_H__

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <ctype.h>
#include <time.h>

#define DIR_SEP_STR "/"
#define DIR_SEP_CHAR '/'
#include <math.h>

#define RELEASE "20140331"

#define NGTYPE 10
#define MAXNDIR 3
#define MXSZ 63                      // max grid size X and Y
#define MXCL 250                     // max number of cells in light; must be >=MXSZ*2; allow more for VL:s
#define MXCT 31                      // max contents in one cell per direction
#define NMSG 2                       // number of messages passed to treatment code
#define MXLE 250                     // max entries in a light before tags added
#define MXFL 255                     // max entries in a word that filler can cope with: light plus tags, >=MXLE+NMSG; <256 to allow poscnt[] not to overflow
#define MXLT (MAXNDIR*MXSZ*MXSZ+NVL) // max number of lights (conservative estimate)
#define NVL (MXSZ*4)                 // max number of virtual lights
#define MAXNMK (MAXNDIR*2)           // max number of corner marks
#define MXMK 30                      // max length of mark string

// note that a light (in a Mobius grid with all squares full) can contain up to MXSZ*2*MXCT entries, which is >MXLE; and that
// if filler discretion is enabled, the number of treated lights must be <=MXFL, otherwise only limited by MXLT
// MXFL<=255 to prevent unsigned char histograms overflowing

#define NLEM 5 // number of light entry methods
#define NATREAT 12 // number of treatments
#define TREAT_PLUGIN 9

#define PI (M_PI)
#define ODD(x) ((x)&1)
#define EVEN(x) (!((x)&1))
#define FREEX(p) if(p) {free(p);p=0;}
#define SLEN 1000 // maximum length for filenames, strings etc.; Windows has MAX_PATH around 260; should be >>MXFL
#define LEMDESCLEN 20

#define MX(a,b) ((a)>(b)?(a):(b))

extern int debug;
#define DEB1 if(debug&1)
#define DEB2 if(debug&2)
#define DEB4 if(debug&4)
#define DEB8 if(debug&8)
#define DEB16 if(debug&16)

// Terminology:
// A `square' is the quantum of area in the grid; one or more squares (a `merge group') form
// an `entry', which is a single enclosed white area in the grid where a letter (or group of letters) will appear.
// bldstructs() constructs the words and entries from the square data produced
// by the editor.

#define NL 37          // number of letters in alphabet (A..Z, 0..9, -); '-' is only used internally for "spread" words
typedef unsigned long long int ABM; // alphabet bitmap type
#define ABM_ALL ((1ULL<<NL)-1)
#define ABM_ALNUM ((1ULL<<36)-1)
#define ABM_LET ((1ULL<<26)-1)
#define ABM_NUM (ABM_ALNUM^ABM_LET)
#define ABM_VOW 0x00104111
#define ABM_CON (ABM_LET^ABM_VOW)
#define ABM_DASH (1ULL<<(NL-1))

struct sprop { // square properties
  unsigned int bgcol; // background colour
  unsigned int fgcol; // foreground colour
  unsigned int mkcol; // mark colour
  unsigned char ten;  // treatment enable: flag to plug-in
  unsigned char spor; // global square property override (not used in dsp)
  unsigned char fstyle; // font style: b0=bold, b1=italic
  unsigned char dech; // dechecked: 0=normal, 1=contributions stacked atop one another, 2=contributions side-by-side
  char mk[MAXNMK][MXMK+1]; // square corner mark strings in each direction
  };

// entry methods
#define EM_FWD 1
#define EM_REV 2
#define EM_CYC 4
#define EM_RCY 8
#define EM_JUM 16
#define EM_ALL 31
#define EM_SPR 32 // for internal use only

struct lprop { // light properties
  unsigned int dmask; // mask of allowed dictionaries; special values 1<<MAXNDICTS and above for "implicit" words
  unsigned int emask; // mask of allowed entry methods
  unsigned char ten;  // treatment enable
  unsigned char lpor; // global light property override (not used in dlp)
  unsigned char dnran; // does not receive a number
  };

struct jdata { // per-feasible-dictionary-entry information relating to words with jumbled entry mode
  int nuf; // total number of unforced letters
  unsigned char ufhist[NL]; // histogram of unforced letters, in word's historder
  unsigned char poscnt[NL]; // number of positions where each unforced letter can go, in word's historder
  };
struct sdata { // per-feasible-dictionary-entry information relating to words with spread entry mode
  ABM flbm[MXFL]; // feasible letter bitmaps // could economise on storage here as done for jdata, but spreading only used with one-word dictionaries for now
  double ct[MXFL][MXFL]; // [i][j] is the number of ways char i in string goes through entry slot j in word // must economise on storage if normal dictionaries are ever used
  double ctd[MXFL]; // [j] is number of ways '-' goes through entry slot j in word
  };

struct word {
  int nent; // number of entries in word, <=MXFL
  int wlen; // length of strings in feasible list (<=nent in spread entry case)
  int jlen; // number of entries subject to jumble (<nent if tagged)
  int gx0,gy0; // start position (not necessarily mergerep)
  int ldir;
  int*flist; // start of feasible list
  int flistlen; // length of feasible list
  struct jdata*jdata;
  ABM*jflbm;
  struct sdata*sdata;
  int commitdep; // depth at which this word committed during fill, or -1 if not committed
  struct entry*e[MXFL]; // list of nent entries making up this word
  struct lprop*lp; // applicable properties for this word
  unsigned char upd; // updated flag
  int fe; // fully-entered flag
  int goi[MXLE]; // grid order indices for each entry
  };
struct entry {
  ABM flbm; // feasible letter bitmap
  ABM flbmh; // copy of flbm provided by solver to running display
  int gx,gy; // corresponding grid position (indices to gsq) of representative
  int checking; // count of intersecting words
  double score[NL];
  double crux; // priority
  unsigned char sel; // selected flag
  unsigned char upd; // updated flag
  unsigned char fl; // flags copied from square
  };
struct square {
// source grid information (saved in undo buffers)
  unsigned int bars; // bar presence in each direction
  unsigned int merge; // merge flags in each direction
  unsigned char fl; // flags: b0=blocked, b3=not part of grid (for irregular shapes); b4=cell selected
  unsigned char dsel; // one light-selected flag for each direction
  int ctlen[MAXNDIR]; // length of contents in each direction
  ABM ctbm[MAXNDIR][MXCT]; // square contents bitmap strings in each direction
  struct sprop sp;
  struct lprop lp[MAXNDIR];
// derived information from here on
  struct entry*e0; // first entry
  int ne; // number of entries (consecutive)
  struct word*w[MAXNDIR];
  unsigned int vflags[MAXNDIR]; // violation flags: b0=double unch, b1=triple+ unch, b2=underchecked, b3=overchecked
  int number; // number in square (-1 for no number)
  int goi; // grid order index of treated squares (-1 in untreated squares)
  };
struct vl { // virtual light
  int l; // number of constituent entries, <=MXCL
  int x[MXCL],y[MXCL]; // constituent entry coords
  struct lprop lp; // properties
  unsigned char sel; // is selected?
  struct word*w;
  };

extern struct word*words;
extern struct entry*entries;
extern struct entry*treatmsge0[NMSG][MXFL]; // to help passing info from filler to constraints
extern struct square gsq[MXSZ][MXSZ];
extern struct vl vls[NVL];
extern int nc,nw,ntw,ne,ne0,ns,nvl;

extern int cwperm[NL-1]; // "codeword" permutation

// DICTIONARY

extern int chartol[256];
extern ABM chartoabm[256];
extern char ltochar[NL];

struct answer { // a word found in one or more dictionaries
  int ahlink; // hash table link for isword()
  unsigned int dmask; // mask of dictionaries where word found
  unsigned int cfdmask; // mask of dictionaries where word found with this citation form
//  int light[NLEM]; // light indices of treated versions
  double score;
  char*cf; // citation form of the word in dstrings
  struct answer*acf; // alternative citation form (linked list)
  char*ul; // untreated light in dstrings: ansp is uniquified by this
  };

struct light { // a string that can appear in the grid, the result of treating an answer; not uniquified
  int hashslink; // hash table (s only) linked list
  int hashaeslink; // hash table (a,e,s) linked list
  int ans; // answer giving rise to this light
  int em; // mode of entry giving rise to this light
  char*s; // the light in dstrings, containing only chars in alphabet
  int uniq; // uniquifying number (by string s only), used as index into lused[]
  int tagged; // does s include NMSG tag characters?
  ABM lbm; // bitmap of letters used
  unsigned char hist[NL]; // letter histogram
  unsigned char historder[NL]; // letters in descending order of histogram frequency
  int nhistorder; // number of entries in historder
  };

extern int*llist;               // buffer for word index list
extern int*llistp;              // ptr to matching word indices
extern int llistn;              // number of matching words
extern int llistwlen;           // word length applicable to matching lights
extern unsigned int llistdm;    // dictionary mask applicable to matching lights
extern int llistem;             // entry method mask applicable to matching lights

extern volatile int abort_flag; // abort word list building?

extern struct answer**ansp;
extern struct light*lts;

extern int atotal;              // total answers in dict
extern int ultotal;             // total unique lights in dict

extern char tpifname[SLEN];
extern int treatmode; // 0=none, TREAT_PLUGIN=custom plug-in
extern int treatorder[NMSG]; // 0=first come first served, 1=spread, 2=jumble
extern char treatmsg[NMSG][MXLT+1];
extern ABM treatcstr[NMSG][MXFL];
extern int tambaw; // treated answer must be a word

extern char treatmsgAZ[NMSG][MXLT+1];
extern char treatmsgAZ09[NMSG][MXLT+1];

extern char msgword[NMSG][MXFL+1]; // for "one-word dictionary" created for tagging

extern int clueorderindex;
extern int gridorderindex[MXLE];
extern int checking[MXLE];
extern int lightx;
extern int lighty;
extern int lightdir;

// PREFERENCES

#define NPREFS 10
extern int prefdata[NPREFS];
#define clickblock (prefdata[0])
#define clickbar (prefdata[1])
#define shownums (prefdata[2])
#define mincheck (prefdata[3])
#define maxcheck (prefdata[4])
#define afunique (prefdata[5])
#define afrandom (prefdata[6])
#define eptsq (prefdata[7])
#define hpxsq (prefdata[8])
#define lnis (prefdata[9])

// FILLER

#define UNDOS 50

void update_grid(void);

static inline int logbase2(int i)
{
	return ffs(i) - 1;
}

static inline int onebit(int x)
{
	return x != 0 && (x & (x-1)) == 0;
}

#endif
