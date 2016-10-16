// $Id: dicts.c 563 2014-04-02 17:18:40Z mo $

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


// DICTIONARY

#include <wchar.h>
#include <pcre.h>
#include <glib.h>   // required for string conversion functions

#include <dlfcn.h>

#include "common.h"
#include "dicts.h"

// default dictionaries
#define NDEFDICTS 4
char*defdictfn[NDEFDICTS]={
"/usr/dict/words",
"/usr/share/dict/words",
"/usr/share/dict/british-english",
"/usr/share/dict/american-english"};

// ISO-8859-1 character mapping to remove accents and fold case: #=reject word, .=ignore character for answer/light purposes
static char chmap[256]="\
................\
................\
................\
0123456789......\
.ABCDEFGHIJKLMNO\
PQRSTUVWXYZ.....\
.ABCDEFGHIJKLMNO\
PQRSTUVWXYZ.....\
................\
................\
................\
................\
AAAAAAECEEEEIIII\
.NOOOOO.OUUUUY..\
AAAAAAECEEEEIIII\
.NOOOOO.OUUUUY.Y";

// set up possible file encodings for dictionary files
#define NFILEENC 6  

struct fileenc {
  char nenc[12];  // name of encoding for g_convert()
  char bom[4];    // potential byte order mark at start of file
  int lbom;       // length of byte order mark
  };

static struct fileenc fenc[NFILEENC] = {
  {"ISO-8859-1",{'\x00'},                     0},
  {"UTF-8"     ,{'\xEF','\xBB','\xBF'},       3},
  {"UTF-16LE"  ,{'\xFF','\xFE'},              2},
  {"UTF-16BE"  ,{'\xFE','\xFF'},              2},
  {"UTF-32LE"  ,{'\xFF','\xFE','\x00','\x00'},4},
  {"UTF-32BE"  ,{'\x00','\x00','\xFE','\xFF'},4}        
};

int chartol[256];
ABM chartoabm[256];
char ltochar[NL];


#define MEMBLK 100000     // unit of memory allocation

struct memblk {
  struct memblk*next;
  int ct;
  char s[MEMBLK];
  };

static struct memblk*dstrings[MAXNDICTS]={0}; // dictionary string pools

struct answer*ans=0,**ansp=0;
struct light*lts=0;

int atotal=0;                // total answers
int ltotal=0;                // total lights
int ultotal=0;               // total uniquified lights

char dfnames[MAXNDICTS][SLEN];
char dsfilters[MAXNDICTS][SLEN];
char dafilters[MAXNDICTS][SLEN];
char lemdesc[NLEM][LEMDESCLEN]={""," (rev.)"," (cyc.)"," (cyc., rev.)","*"};
char*lemdescADVP[NLEM]={"normally","reversed","cyclically permuted","cyclically permuted and reversed","with any other permutation"};

#define HTABSZ 1048576
static int ahtab[HTABSZ]={0};

static int cmpans(const void*p,const void*q) {int u; // string comparison for qsort
  u=strcmp( (*(struct answer**)p)->ul,(*(struct answer**)q)->ul); if(u) return u;
  u=strcmp( (*(struct answer**)p)->cf,(*(struct answer**)q)->cf);       return u;
  }

static void freedstrings(int d) { // free string pool associated with dictionary d
  struct memblk*p;
  while(dstrings[d]) {p=dstrings[d]->next;free(dstrings[d]);dstrings[d]=p;}
  }

void freedicts(void) { // free all memory allocated by loaddicts, even if it aborted mid-load
  int i;
  for(i=0;i<MAXNDICTS;i++) freedstrings(i);
  FREEX(ans);
  FREEX(ansp);
  atotal=0;
  }

// answer pool is linked list of `struct memblk's containing
// strings:
//   char 0 of string is word score*10+128 (in range 28..228);
//   char 1 of string is dictionary number
//   char 2 onwards:
//     (0-terminated) citation form, in UTF-8
//     (0-terminated) untreated light form, in chars

static struct memblk*memblkp=0;
static int memblkl=0;

// Add a new dictionary word with UTF-8 citation form s0, ISO-8859-1 form s1,
// dictionary number dn, score f. Return 1 if added, 0 if not, -2 for out of memory
static int adddictword(char*s0,char*s1,int dn,pcre*sre,pcre*are,float f) {
  int c,i,l0,l1;
  unsigned char m;
  struct memblk*q;
  int pcreov[120];

// printf("adddictword(\"%s\",\"%s\")\n",s0,s1);
  l0=strlen(s0);
  for(i=l1=0;s1[i];i++) {
    m=s1[i];
    c=chmap[m];
    if(c=='#') {DEB1 printf("[%d=%02x]\n",m,m);return 0;} // reject words with invalid characters
    if(c=='.') continue; // skip other non-alphanumeric
    s1[l1++]=c; // copy to temporary string
    }
  s1[l1]=0; // terminate: now have untreated light form in s1
  if(l1==0) return 0; // empty: skip
  if(l1>MXLE) return 0; // too long: skip
  if(sre) {
    i=pcre_exec(sre,0,s0,l0,0,0,pcreov,120);
    DEB1 if(i<-1) printf("PCRE error %d\n",i);
    if(i<0) return 0; // failed match
    }
  if(are) {
    i=pcre_exec(are,0,s1,l1,0,0,pcreov,120);
    DEB1 if(i<-1) printf("PCRE error %d\n",i);
    if(i<0) return 0; // failed match
    }

  if(memblkp==NULL||memblkl+2+l0+1+l1+1>MEMBLK) { // allocate more memory if needed (this always happens on first pass round loop)
    q=(struct memblk*)malloc(sizeof(struct memblk));
    if(q==NULL) {return -2;}
    q->next=NULL;
    if(memblkp==NULL) dstrings[dn]=q; else memblkp->next=q; // link into list
    memblkp=q;
    memblkp->ct=0;
    memblkl=0;
    }
  *(memblkp->s+memblkl++)=(char)floor(f*10.0+128.5); // score with rounding
  *(memblkp->s+memblkl++)=dn;
  strcpy(memblkp->s+memblkl,s0);memblkl+=l0+1;
  strcpy(memblkp->s+memblkl,s1);memblkl+=l1+1;
  memblkp->ct++; // count words in this memblk
  return 1;
  }
 
// read n bytes from fp and interpret as little-endian integer
static int getint(FILE*fp,int n) { int i,u; for(i=0,u=0;i<n;i++) u+=fgetc(fp)<<(i*8); return u; }

#define MXHNODES 512
static int hnodep[2][MXHNODES]; // "0" and "1" pointers or -1 for none
static int hnodec[MXHNODES]; // char represented or -1 for none
static int nhn;

// add a new Huffman tree entry for code m of length l representing i
static int addhcode(int m,int l,int i) {
  int *p;
  int j,n;

DEB1 printf("addhcode(%d,%d,%d) nhn=%d\n",m,l,i,nhn);
  for(n=0,j=l-1;j>=0;j--) { // big-endian...
    p=hnodep[(m>>j)&1]+n;
    if(*p==-1) { // node does not exist yet
      if(nhn>=MXHNODES) return -1;
      *p=nhn;
      hnodep[0][nhn]=-1;
      hnodep[1][nhn]=-1;
      hnodec[nhn]=-1;
      nhn++;
      }
    n=*p;
    }
  hnodec[n]=i;
  return 0;
  }

// Attempt to load a .TSD file. Return number of words >=0 on success, <0 on error.
static int loadtsd(FILE*fp,int format,int dn,pcre*sre,pcre*are) {
  int c,i,j,l,m,ml,n,u,nw;
  int hoff[MXLE+1]; // file offsets into Huffman coded block
  int dcount[MXLE+1]; // number of words of each length
  char s0[SLEN],s1[SLEN];
  GError*error=NULL;
  gchar*sp=NULL;

  DEB1 printf("attempting to load TSD format=0x%x\n",format);
  if(format!='0'&&format!='1') return -1; // only TSD0 and TSD1 supported
  if(fseek(fp,4,SEEK_SET)<0) return -1;
  ml=getint(fp,2); // maximum length
  if(ml<1) return -1;
  DEB1 printf("ml=%d\n",ml);
  if(format=='1') getint(fp,12*ml+12); // skip some bytes
  for(i=0;i<ml;i++) {
    u=getint(fp,4);
    if(u<0) return -1;
    if(i<=MXLE) hoff[i]=u;
    }
  for(i=0;i<ml;i++) {
    u=getint(fp,4);
    if(u<0) return -1;
    if(i<=MXLE) {
      dcount[i]=u;
DEB1 printf("dcount[%d]=%d\n",i,u);
      }
    }
  if(fseek(fp,format=='0'?80:1000,SEEK_CUR)<0) return -1; // skip comment
  u=getint(fp,2); // ?
  u=getint(fp,2); // ?
DEB1 printf("hcode array at %08lx\n",ftell(fp));
  nhn=1;
  hnodep[0][0]=-1;
  hnodep[1][0]=-1;
  hnodec[0]=-1;
  for(i=0;i<256;i++) { // loop over ISO-8859-1 characters
    c=getint(fp,1); // character
    l=getint(fp,1); // code length
    u=getint(fp,2); // ?
    m=getint(fp,4); // code
    if(l>0) {
DEB1 {
        printf("%02x %c ",i,isprint(i)?i:'?');
        for(j=l-1;j>=0;j--) printf("%d",(m>>j)&1);
        printf("\n");
        }
      if(addhcode(m,l,i)<0) return -1;
      }
    }
  nw=0;
  for(l=1;l<ml;l++) {
    if(l>MXLE) break;
    if(hoff[l]==0) continue;
    if(fseek(fp,hoff[l],SEEK_SET)<0) return -1;
DEB1    printf("starting to read length %d at offset %08x; dstrings[%d]=%p memblkp=%p\n",l,hoff[l],dn,dstrings[dn],memblkp);
    j=8;
    c=0;
    for(i=0;i<dcount[l];i++) {
      m=0;
      n=0;
      for(;;) {
        if(j==8) {j=0;c=fgetc(fp);if(c==EOF) return -1;}
DEB16   printf("%d",(c>>j)&1);
        n=hnodep[(c>>j)&1][n];
        j++;
        u=hnodec[n];
        if(u!=-1) {
DEB16     printf(" %02x %c\n",u,isprint(u)?u:'?');
          if((u>=0x01&&u<0x7f)||(u>=0xa0&&u<=0xff)) {
            s1[m]=u;
            if(m<SLEN-1) m++; // don't overflow
            }
          n=0;
          }
        if(u==0) break;
        }
      if(format=='1') {
        if(j==8) {j=0;c=fgetc(fp);if(c==EOF) return -1;} // skip a zero bit for some reason
        j++;
        }
      if(m==SLEN-1) continue; // hit string limit? discard
      s1[m]=0;
      for(u=0;u<m;u++) if(s1[u]<0x20) break; // look for any non-printable character
      s1[u]=0; // stop there
      sp=g_convert(s1,-1,"UTF-8","ISO-8859-1",NULL,NULL,&error);
      if(error) continue;
      strcpy(s0,sp);    // UTF-8 form in s0
      g_free(sp),sp=0;
      u=adddictword(s0,s1,dn,sre,are,0.0);
      if(u==-2) return -2;
      if(u==1) nw++;
      }
    }
  return nw;
  }


static int loadonedict(int dn,int sil) { // return number of answers loaded
  int at,i,j;
  FILE *fp=0;
  char s0[SLEN]; // citation form
  char s1[SLEN]; // light form
  gchar t[SLEN+1]; // input buffer

  float f;
  int mode,owd,rc;
  pcre*sre,*are;
  const char*pcreerr;
  int pcreerroff;
  char sfilter[SLEN+1];
  char afilter[SLEN+1];
  GError *error = NULL;
  gchar *sp = NULL;
  gsize l0;
  char bom[5];

  rc=0;
  owd=0;
  at=0;
  freedstrings(dn);
  memblkl=0; // number of bytes stored so far in current memory block
  memblkp=0; // current memblk being filled
  if(dfnames[dn][0]=='\0') {
    if(dafilters[dn][0]=='\0') return 0; // dictionary slot unused
    owd=1; // one-word dictionary
    sre=0;
    are=0;
    goto ew3;
    }
  strcpy(sfilter,dsfilters[dn]);
  if(!strcmp(sfilter,"")) sre=0;
  else {
    sre=pcre_compile(sfilter,PCRE_CASELESS,&pcreerr,&pcreerroff,0);
    if(pcreerr) {
      sprintf(t,"Dictionary %d\nBad file filter syntax: %.100s",dn+1,pcreerr);
      reperr(t);
      }
    }
  strcpy(afilter,dafilters[dn]);
  if(!strcmp(afilter,"")) are=0;
  else {
    are=pcre_compile(afilter,PCRE_CASELESS,&pcreerr,&pcreerroff,0);
    if(pcreerr) {
      sprintf(t,"Dictionary %d\nBad answer filter syntax: %.100s",dn+1,pcreerr);
      reperr(t);
      }
    }

ew3:
  mode=-1;    // Indicates file encoding not set by BOM in file
  if(owd) { mode=1; goto retry; }   // Always set file encoding to UTF-8 for one-word dictionary
  fp=fopen(dfnames[dn],"rb"); // try first in binary mode
  if(!fp) {
    sprintf(t,"Dictionary %d\nFile not found",dn+1);
    if(!sil) reperr(t);
    rc=-1; goto exit;
    }
  if(fread(bom,1,4,fp)<4) {rewind(fp); goto retry;} // too short for a BOM, so use mode -1
  if(!strncmp(bom,"TSD",3)) {
    i=loadtsd(fp,bom[3],dn,sre,are);
    if(i>=0) {at=i; goto exit;} // successfully read
    }
  rewind(fp);
  if(fgets(bom,5,fp)!=NULL) {       // re-read BOM at start of file
    for(i=0; i<NFILEENC; i++) {
      if(fenc[i].lbom>0) {
        if(!(memcmp(bom,fenc[i].bom,fenc[i].lbom))) {
          mode=i;                             // BOM found so set mode
          fseek(fp,fenc[i].lbom,SEEK_SET);    // and skip past BOM 
          break;
          }
        }
      }   // end of for loop
    if(mode>1) {    // Cannot read this file encoding mode
      sprintf(t,"Dictionary %d\nCannot read file encoding:\ntry UTF-8 or ISO-8859-1 encoding",dn+1);
      if(!sil) reperr(t);
      rc=-1; goto exit;
      }
    if(mode==-1) rewind(fp);    // No BOM, so read words from start of file
    }
retry:
  at=0;
  freedstrings(dn);
  memblkl=0; // number of bytes stored so far in current memory block
  memblkp=0; // current memblk being filled
  dstrings[dn]=0;
  DEB1 printf("Reading dictionary file %s (in mode %d), owd=%d\n",dfnames[dn],mode,owd);
  for(;;) {       // Loop through dictionary 'words' until EOF
    if(owd) strcpy(t,dafilters[dn]);
    else {
      if(feof(fp)) break;
      if(fgets(t,SLEN,fp)==NULL) break;
      }
    j=strlen(t)-1;
    if(j<0) goto skipword;
    while(j>=0&&t[j]>=0&&t[j]<=' ') t[j--]=0;     // Strip control characters/white space from end of string
    while (j>=0&&((t[j]>='0'&&t[j]<='9')||t[j]=='.'||t[j]=='+'||t[j]=='-')) j--;  // get score (if any) from end
    j++;
    f=0.0;
    if(j==0||t[j-1]!=' ') j=strlen(t); // all digits, or no space? treat it as a 'word'
    else {
      sscanf(t+j,"%f",&f);
      if(f>= 10.0) f= 10.0;
      if(f<=-10.0) f=-10.0;
      t[j--]=0; // rest of input is treated as a 'word' (which may contain spaces)
      }
    while(j>=0&&t[j]>=0&&t[j]<=' ') t[j--]=0;  // remove trailing white space
    j++;
    if(j<1) goto skipword;

    // t now contains 'word' for conversion; convert it first to UTF-8 for use as citation form
    if(mode>-1) {     // Use encoding mode based on BOM
      sp=g_convert(t,-1,"UTF-8",fenc[mode].nenc,NULL,&l0,&error);
      if(error) {
        sprintf(t,"Dictionary %d\nFile not encoded in accordance with its BOM",dn+1);
        if(!sil) reperr(t);
        rc=-1; goto exit;
        }
    } else { // try fallback encodings in turn
      if(mode==-1) sp=g_convert(t,-1,"UTF-8","UTF-8",NULL,&l0,&error); // effectively a check for valid UTF-8
      if(mode==-2) sp=g_convert(t,-1,"UTF-8","ISO-8859-1",NULL,&l0,&error);
      if(error) {
        DEB1 {
          printf("error: %s\n",error->message);
          for(i=0;t[i];i++) printf(" %02x",t[i]);
          printf("\n");
          }
        g_clear_error(&error);
        freedstrings(dn);
        rewind(fp);
        mode--;
        if(mode>-3) goto retry; // go round again and try next encoding
        // here if none of the encodings worked 
        sprintf(t,"Dictionary %d\nFile does not use a recognised encoding:\ntry UTF-8 or ISO-8859-1",dn+1);
        if(!sil) reperr(t);
        rc=-1; goto exit;
        }
      }
    // here we have UTF-8 citation form of string in sp, length in l0
    if(l0>=SLEN) goto skipword;
    strcpy(s0,sp);
    g_free(sp),sp=0;

    // Now convert s0 to ISO-8859-1 to create untreated light form
    sp=g_convert(s0,-1,"ISO-8859-1","UTF-8",NULL,NULL,&error);
    if(error==NULL) {strcpy(s1,sp);}    // ISO-8859-1 form in s1
    else {
      DEB1 printf("Failed conversion to ISO-8859-1: %s\n",s0);
      goto skipword;   // 'word' contained symbols outside of Latin-1 character set; go to next word
      }
    i=adddictword(s0,s1,dn,sre,are,f);
    if(i==-2) {rc=-2; goto exit;}
    if(i==1) at++;

skipword:
    if(sp) g_free(sp),sp=0;
    g_clear_error(&error);
    if(owd) break;
    }  // End of 'word' for loop

exit:
  if(sp) g_free(sp),sp=0;
  if(sre) pcre_free(sre);
  if(are) pcre_free(are);
  g_clear_error(&error);
  if(!owd) if(fp) fclose(fp);
  if(rc<0) return rc; else return at;
  }

int loaddicts(int sil) { // load (or reload) dictionaries from dfnames[]
  // sil=1 suppresses error reporting
  // returns: 0=success; 1=bad file; 2=no words; 4=out of memory
  struct answer*ap;
  struct memblk*p;
  int at,dn,i,j,k,l,rc,u;
  char t[SLEN];
  unsigned int h;

  freedicts();
  at=0;
  rc=0;

  for(dn=0;dn<MAXNDICTS;dn++) {
    u=loadonedict(dn,sil);
    if(u==-2) goto ew4; // out of memory
    if(u<0) rc=1;
    else at+=u;
    }

  if(at==0) {  // No words from any dictionary
    sprintf(t,"No words available from any dictionary");
    if(!sil) reperr(t);
    rc=2; 
    }
  if(rc>0) {
    freedicts();
    return rc;
    }
  // allocate array space from counts
  atotal=at;
  DEB1 printf("atotal=%9d\n",atotal);
  ans =(struct answer* )malloc(atotal*sizeof(struct answer));  if(ans ==NULL) goto ew4;
  ansp=(struct answer**)malloc(atotal*sizeof(struct answer*)); if(ansp==NULL) goto ew4; // pointer array for sorting

  k=0;
  for(dn=0;dn<MAXNDICTS;dn++) {
    p=dstrings[dn];
    while(p!=NULL) {
      for(i=0,l=0;i<p->ct;i++) { // loop over all words
        ans[k].score=pow(10.0,((float)(*(unsigned char*)(p->s+l++))-128.0)/10.0);
        ans[k].cfdmask=
        ans[k].dmask=1<<*(unsigned char*)(p->s+l++);
        ans[k].cf   =p->s+l; l+=strlen(p->s+l)+1;
        ans[k].acf  =0;
        ans[k].ul   =p->s+l; l+=strlen(p->s+l)+1;
        k++;
        }
      p=p->next;
      }
    }
  DEB1 printf("k=%9d\n",k);
  assert(k==atotal);
  for(i=0;i<atotal;i++) ansp[i]=ans+i;

  qsort(ansp,atotal,sizeof(struct answer*),cmpans);
  ap=0; // ap points to first of each group of matching citation forms
  for(i=0,j=-1;i<atotal;i++) { // now remove duplicate entries
    if(i==0||strcmp(ansp[i]->ul,ansp[i-1]->ul)) {j++;ap=ansp[j]=ansp[i];}
    else {
      ansp[j]->dmask|=ansp[i]->dmask; // union masks
      ansp[j]->score*=ansp[i]->score; // multiply scores over duplicate entries
      if(strcmp(ansp[i]->cf,ansp[i-1]->cf)) ap->acf=ansp[i],ap=ansp[i]; // different citation forms? link them together
      else ap->cfdmask|=ansp[i]->cfdmask; // cf:s the same: union masks
      }
    }
  atotal=j+1;

  for(i=0;i<HTABSZ;i++) ahtab[i]=-1;
  for(i=0;i<atotal;i++) {
    h=0;
    for(j=0;ansp[i]->ul[j];j++) h=h*113+ansp[i]->ul[j];
    h=h%HTABSZ;
    ansp[i]->ahlink=ahtab[h];
    ahtab[h]=i;
    }

  for(i=0;i<atotal;i++) {
    if(ansp[i]->score>= 1e10) ansp[i]->score= 1e10; // clamp scores
    if(ansp[i]->score<=-1e10) ansp[i]->score=-1e10;
    }
  for(i=0;i<atotal;i++) {
    if(ansp[i]->acf==0) continue;
  //  printf("A%9d %08x %20s %5.2f",i,ansp[i]->dmask,ansp[i]->ul,ansp[i]->score);
    ap=ansp[i];
    do {
  //    printf(" \"%s\"",ap->cf);
      ap=ap->acf;
      } while(ap);
  //  printf("\n");
    }

  DEB1 printf("Total unique answers by entry: %d\n",atotal);
  return 0;
ew4:
  freedicts();
  reperr("Out of memory loading dictionaries");
  return 4;
  }

// Run through some likely candidates for default dictionaries.
// Return
//   0: found something
// !=0: nothing found
int loaddefdicts() {int i;
  for(i=0;i<NDEFDICTS;i++) {
    strcpy(dfnames[0],defdictfn[i]);
    strcpy(dsfilters[0],"^.*+(?<!'s)");
    loaddicts(1);
    if(atotal!=0) return 0; // happy if we found any words at all
    }
  strcpy(dfnames[0],""),strcpy(dsfilters[0],"");
  return 4;
  }



// INITIAL FEASIBLE LIST GENERATION

static int curans,curem,curten,curdm;

int treatmode=0,treatorder[NMSG]={0,0};
char tpifname[SLEN]="";
char*treatmessage[NMSG]={"",""};
ABM treatcstr[NMSG][MXFL]={{0},{0}};
int tambaw=0;

char*treatmessageAZ[NMSG];
char*treatmessageAZ09[NMSG];
char treatmsg[NMSG][MXLT+1];
char treatmsgAZ[NMSG][MXLT+1];
char treatmsgAZ09[NMSG][MXLT+1];
char msgchar[NMSG];
char msgcharAZ[NMSG];
char msgcharAZ09[NMSG];

int clueorderindex;
int gridorderindex[MXLE];
int checking[MXLE];
int lightlength;
int lightx;
int lighty;
int lightdir;

// STATICS FOR VARIOUS TREATMENTS

// Playfair square
static char psq[25];
static int psc['Z'+1];

static int*tfl=0; // temporary feasible list
static int ctfl,ntfl;

static int clts;
static int hstab[HTABSZ];
static int haestab[HTABSZ];
static struct memblk*lstrings=0;
static struct memblk*lmp=0;
static int lml=MEMBLK;

static void dohistdata(struct light*l) {
  int i,j,m,n;
  memset(l->hist,0,sizeof(l->hist));
  l->lbm=0;
  m=0;
  n=strlen(l->s);
  if(l->tagged) n-=NMSG;
  for(i=0;i<n;i++) {
    j=chartol[(int)l->s[i]];
    l->hist[j]++;
    if(l->hist[j]>m) m=l->hist[j];
    l->lbm|=1ULL<<j;
    }
  l->nhistorder=0;
  for(i=m;i>0;i--) for(j=0;j<NL;j++) if(l->hist[j]==i) l->historder[l->nhistorder++]=j;
  }

// return index of light, creating if it doesn't exist; -1 on no memory
static int findlight(const char*s,int tagged,int a,int e) {
  unsigned int h0,h1;
  int f,i,u,l0;
  int l;
  int len0,len1;
  struct light*p;
  struct memblk*q;

  len0=strlen(s);
  if(tagged) len0-=NMSG;
  assert(len0>0);
  h0=0;
  for(i=0;i<len0;i++) h0=h0*113+s[i];
  h1=h0*113+a*27+e;
  h0=h0%HTABSZ; // h0 is hash of string only
  h1=h1%HTABSZ; // h1 is hash of string+treatment+entry method
  l=haestab[h1];
  while(l!=-1) {
    if(lts[l].ans==a&&lts[l].em==e&&!strcmp(s,lts[l].s)) return l; // exact hit in all particulars? return it
    l=lts[l].hashaeslink;
    }
  if(ltotal>=clts) { // out of space to store light structures? (always happens first time)
    clts=clts*2+5000; // try again a bit bigger
    p=realloc(lts,clts*sizeof(struct light));
    if(!p) return -1;
    lts=p;
    DEB2 printf("lts realloc: %d\n",clts);
    }
  l=hstab[h0];
  u=-1; // look for the light string, independent of how it arose
  f=0;
  while(l!=-1) {
    len1=strlen(lts[l].s);
    if(lts[l].tagged) len1-=NMSG;
    assert(len1>0);
    if(len0==len1&&!strncmp(s,lts[l].s,len0)) { // match as far as non-tag part is concerned
      u=lts[l].uniq;
      if(!strcmp(s,lts[l].s)) {f=1; break;} // exact match including possible tags
      }
    l=lts[l].hashslink;
    }
  if(f==0) { // we do not have a full-string match
    l0=strlen(s)+1;
    if(lml+l0>MEMBLK) { // make space to store copy of light string
      DEB1 printf("memblk alloc\n");
      q=(struct memblk*)malloc(sizeof(struct memblk));
      if(!q) return -1;
      q->next=NULL;
      if(lmp==NULL) lstrings=q; else lmp->next=q; // link into list
      lmp=q;
      lml=0;
      }
    if(u==-1) u=ultotal++; // allocate new uniquifying number if needed
    lts[ltotal].s=lmp->s+lml;
    strcpy(lmp->s+lml,s);lml+=l0;
  } else {
    lts[ltotal].s=lts[l].s;
    }
  lts[ltotal].ans=a;
  lts[ltotal].em=e;
  lts[ltotal].uniq=u;
  lts[ltotal].tagged=tagged;
  lts[ltotal].hashslink  =hstab  [h0]; hstab  [h0]=ltotal; // insert into hash tables
  lts[ltotal].hashaeslink=haestab[h1]; haestab[h1]=ltotal;
  dohistdata(lts+ltotal);
  return ltotal++;
  }

// is word in dictionaries specified by curdm?
int isword(const char*s) {
  int i,p;
  unsigned int h;
  h=0;
  for(i=0;s[i];i++) h=h*113+s[i];
  h=h%HTABSZ;
  p=ahtab[h];
  while(p!=-1) {
    if(!strcmp(s,ansp[p]->ul)) return !!(ansp[p]->dmask&curdm);
    p=ansp[p]->ahlink;
    }
  return 0;
  }

// add light to feasible list: s=text of light, a=answer from which treated, e=entry method
// returns 0 if OK, !=0 on (out of memory) error
static int addlight(const char*s,int a,int e) {
  int l;
  int*p;
  char t[MXFL+1]; // curten should never be set when adding msgword[]:s (got from msglprop); as MXLE+NMSG<=MXFL this never overflows

  l=strlen(s);
  if(l<1) return 0; // is this test needed?
  memcpy(t,s,l);
  if(curten) memcpy(t+l,msgcharAZ09,NMSG),l+=NMSG; // append tag characters if any
  t[l]=0;
  l=findlight(t,curten,a,e);
  if(l<0) return l;
  if(ntfl>=ctfl) {
    ctfl=ctfl*3/2+500;
    p=realloc(tfl,ctfl*sizeof(int));
    if(!p) return -1;
    tfl=p;
    DEB2 printf("tfl realloc: %d\n",ctfl);
    }
  tfl[ntfl++]=l;
  return 0;
  }

// Add treated answer to feasible light list if suitable
// returns !=0 for error
int treatedanswer(const char*s) {
  int l,u;

  l=strlen(s);
  if(l!=lightlength) return 0;
  assert(l>0);
  if(tambaw&&!isword(s)) return 0;
  u=addlight(s,curans,0);if(u) return u;
  return 0;
  }

// returns !=0 on error
static int inittreat(void) {int i,k;
  int c;

  switch(treatmode) {
    case 1: // Playfair
      for(i='A';i<='Z';i++) psc[i]=-1; // generate code square
      for(i=0,k=0;treatmsgAZ[0][i]&&k<25;i++) {
        c=treatmsgAZ[0][i];
        if(c=='J') c='I';
        if(psc[c]==-1) {psc[c]=k;psq[k]=c;k++;}
        }
      for(c='A';c<='Z';c++) {
        if(c=='J') continue;
        if(psc[c]==-1) {psc[c]=k;psq[k]=c;k++;}
        }
      assert(k==25); // we should have filled in the whole square now
      psc['J']=psc['I'];
      DEB1 for(i=0;i<25;i++) {
        printf("%c ",psq[i]);
        if(i%5==4) printf("\n");
        }
      return 0;
    case 9: // custom plug-in
      for(i=0;i<NMSG;i++) {
        treatmessage[i]=treatmsg[i];
        treatmessageAZ[i]=treatmsgAZ[i];
        treatmessageAZ09[i]=treatmsgAZ09[i];
        }
      break;
    default:break;
    }
  return 0;
  }

static void finittreat(void) {}


static void *tpih=0;
static int (*treatf)(const char*)=0;

// returns error string or 0 for OK
char*loadtpi(void) {
  int (*f)(void);
  unloadtpi();
  dlerror(); // clear any existing error
  tpih=dlopen(tpifname,RTLD_LAZY);
  if(!tpih) return dlerror();
  dlerror();
  *(void**)(&f)=dlsym(tpih,"init"); // see man dlopen for the logic behind this
  if(!dlerror()) (*f)(); // initialise the plug-in
  *(void**)(&treatf)=dlsym(tpih,"treat");
  return dlerror();
  }

void unloadtpi(void) {void (*f)();
  if(tpih) {
    dlerror(); // clear any existing error
    *(void**)(&f)=dlsym(tpih,"finit");
    if(!dlerror()) (*f)(); // finalise it before unloading
    dlclose(tpih);
    }
  tpih=0;
  treatf=0;
  }

// returns !=0 on error
static int treatans(const char*s) {
  int c0,c1,i,j,l,l0,l1,o,u;
  char t[MXLE+2]; // enough for "insert single character"
  l=strlen(s);
  // printf("treatans(%s)",s);fflush(stdout);
  for(i=0;s[i];i++) assert((s[i]>='A'&&s[i]<='Z')||(s[i]>='0'&&s[i]<='9'));
  switch(treatmode) {
  case 0:return treatedanswer(s);
  case 1: // Playfair
    if(l!=lightlength) return 0;
    if(ODD(l)) return 0;
    for(i=0;i<l;i+=2) {
      c0=s[i];c1=s[i+1]; // letter pair to encode
      if(!isalpha(c0)||!isalpha(c1)) return 0; // abandon on digits
      l0=psc[c0];l1=psc[c1]; // positions of chars in the square
      if(l0==l1) return 0; // don't handle double letters (including 'IJ' etc.)
      if     (l0/5==l1/5) t[i]=psq[ l0/5     *5+(l0+1)%5],t[i+1]=psq[ l1/5     *5+(l1+1)%5]; // same row
      else if(l0%5==l1%5) t[i]=psq[(l0/5+1)%5*5+ l0   %5],t[i+1]=psq[(l1/5+1)%5*5+ l1   %5]; // same col
      else                t[i]=psq[ l0/5     *5+ l1   %5],t[i+1]=psq[ l1/5     *5+ l0   %5]; // rectangle
      }
    t[i]=0;
    return treatedanswer(t);
  case 2: // substitution
    if(l!=lightlength) return 0;
    l0=strlen(treatmsgAZ09[0]);
    for(i=0;s[i];i++) {
      if(isalpha(s[i])) j=s[i]-'A';
      else              j=s[i]-'0';
      if(j<l0) t[i]=treatmsgAZ09[0][j];
      else     t[i]=s[i];
      }
    t[i]=0;
    return treatedanswer(t);
  case 3: // fixed Caesar/Vigenère
    if(l!=lightlength) return 0;
    l0=strlen(treatmsgAZ09[0]);
    if(l0==0) return treatedanswer(s); // no keyword, so leave as plaintext
    for(i=0;s[i];i++) {
      o=treatmsgAZ09[0][i%l0];
      if(isalpha(o)) o=o-'A';
      else           o=o-'0';
      if(isalpha(s[i])) t[i]=(s[i]-'A'+o)%26+'A';
      else              t[i]=(s[i]-'0'+o)%10+'0';
      }
    t[i]=0;
    return treatedanswer(t);
  case 4: // variable Caesar
    if(l!=lightlength) return 0;
    if(treatorder[0]==0) { // for backwards compatibility
      l0=strlen(treatmsgAZ09[0]);
      if(l0==0) return treatedanswer(s); // no keyword, so leave as plaintext
      o=treatmsgAZ09[0][clueorderindex%l0];
    } else {
      o=msgcharAZ09[0];
      if(o=='-') return treatedanswer(s); // leave as plaintext
      }
    if(isalpha(o)) o=o-'A';
    else           o=o-'0';
    for(i=0;s[i];i++) {
      if(isalpha(s[i])) t[i]=(s[i]-'A'+o)%26+'A';
      else              t[i]=(s[i]-'0'+o)%10+'0';
      }
    t[i]=0;
    return treatedanswer(t);
  case 10: // misprint, correct letters specified
    if(l!=lightlength) return 0;
    c0=msgcharAZ09[0];
    if(c0=='-') return treatedanswer(s); // unmisprinted
    c1='.';
    goto misp0;
  case 11: // misprint, misprinted letters specified
    if(l!=lightlength) return 0;
    c1=msgcharAZ09[0];
    if(c1=='-') return treatedanswer(s); // unmisprinted
    c0='.';
    goto misp0;
  case 5: // misprint
    if(l!=lightlength) return 0;
    l0=strlen(treatmsg[0]);
    l1=strlen(treatmsg[1]);
    if(clueorderindex>=l0&&clueorderindex>=l1) return treatedanswer(s);
    c0=clueorderindex<l0?treatmsg[0][clueorderindex]:'.';
    c1=clueorderindex<l1?treatmsg[1][clueorderindex]:'.';
    c0=toupper(c0);
    c1=toupper(c1);
    if(!isalnum(c0)) c0='.';
    if(!isalnum(c1)) c1='.';
    if(c0=='.'&&c1=='.') return treatedanswer(s); // allowing this case would slow things down too much for now
misp0:
    strcpy(t,s);
    for(i=0;s[i];i++) if(c0=='.'||s[i]==c0) {
      if(c1=='.') {
        for(c1='A';c1<='Z';c1++) {
          if(s[i]==c1) continue; // not a *mis*print
          t[i]=c1;
          u=treatedanswer(t); if(u) return u;
          t[i]=s[i];
          }
      } else {
        if(c0=='.'&&s[i]==c1) continue; // not a *mis*print unless specifically instructed otherwise
        t[i]=c1;
        u=treatedanswer(t); if(u) return u;
        t[i]=s[i];
        if(c0==c1) break; // only one entry for the `misprint as self' case
        }
      }
    return 0;
  case 6: // delete single occurrence
    if(l<lightlength) return 0;
    c0=msgcharAZ09[0];
    if(c0=='-') return treatedanswer(s);
    if(l<=lightlength) return 0;
    for(i=0;s[i];i++) if(s[i]==c0) {
      for(j=0;j<i;j++) t[j]=s[j];
      for(;s[j+1];j++) t[j]=s[j+1];
      t[j]=0;
      u=treatedanswer(t); if(u) return u;
      while(s[i+1]==c0) i++; // skip duplicate outputs
      }
    return 0;
  case 7: // delete all occurrences
    if(l<lightlength) return 0;
    c0=msgcharAZ09[0];
    if(c0=='-') return treatedanswer(s);
    if(l<=lightlength) return 0;
    for(i=0,j=0;s[i];i++) if(s[i]!=c0) t[j++]=s[i];
    t[j]=0;
    if(j!=lightlength) return 0; // not necessary, but improves speed slightly
    return treatedanswer(t);
  case 8: // insert single character
    if(l>lightlength) return 0;
    c0=msgcharAZ09[0];
    if(c0=='-') return treatedanswer(s);
    if(l!=lightlength-1) return 0;
    for(i=0;i<=l;i++) {
      for(j=0;j<i;j++) t[j]=s[j];
      t[j++]=c0;
      for(;s[j-1];j++) t[j]=s[j-1];
      t[j]=0;
      u=treatedanswer(t); if(u) return u;
      while(s[i]==c0) i++; // skip duplicate outputs
      }
    return 0;
  case 9: // custom plug-in
    if(treatf) return (*treatf)(s);
    return 1;
  default:break;
    }
  return 0;
  }

int pregetinitflist(void) {
  int i;
  struct memblk*p;
  while(lstrings) {p=lstrings->next;free(lstrings);lstrings=p;} lmp=0; lml=MEMBLK;
  FREEX(tfl);ctfl=0;ntfl=0;
  FREEX(lts);clts=0;ltotal=0;ultotal=0;
  for(i=0;i<HTABSZ;i++) hstab[i]=-1,haestab[i]=-1;
  if (inittreat()) return 1;
  return 0;
  }

// returns !=0 for error
int postgetinitflist(void) {
  finittreat();
  FREEX(tfl);ctfl=0;
  return 0;
  }

// construct an initial list of feasible lights for a given length etc.
// caller's responsibility to free(*l)
// returns !=0 on error; -5 on abort
int getinitflist(int**l,int*ll,struct lprop*lp,int llen) {
  int i,j,u;
  ABM mfl[NMSG],ml[NMSG],b;

  ntfl=0;
  curdm=lp->dmask,curem=lp->emask,curten=lp->ten;
  for(i=0;i<NMSG;i++) if(curdm&(1<<(MAXNDICTS+i))) { // "special" word for message spreading/jumble?
    DEB1 printf("msgword[%d]=<%s>\n",i,msgword[i]);
    u=addlight(msgword[i],-1-i,0);
    if(u) return u;
    goto ex0;
    }
  curem=EM_FWD; // force normal entry to be allowed if all are disabled
  lightlength=llen;
  DEB2 printf("getinitflist(%p) llen=%d dmask=%08x emask=%08x ten=%d:\n",lp,llen,curdm,curem,curten);
  memset(mfl,0,sizeof(mfl));
  for(i=0;i<NMSG;i++) {
    if(clueorderindex<(int)strlen(treatmsg    [i])) msgchar    [i]=treatmsg    [i][clueorderindex]; else  msgchar    [i]='-';
    if(clueorderindex<(int)strlen(treatmsgAZ  [i])) msgcharAZ  [i]=treatmsgAZ  [i][clueorderindex]; else  msgcharAZ  [i]='-';
    if(clueorderindex<(int)strlen(treatmsgAZ09[i])) msgcharAZ09[i]=treatmsgAZ09[i][clueorderindex]; else  msgcharAZ09[i]='-';
    if(curten&&treatorder[i]>0) {
      for(j=0;treatmsgAZ09[i][j];j++) mfl[i]|=chartoabm[(int)treatmsgAZ09[i][j]];
      if(ntw>(int)strlen(treatmsgAZ09[i])) mfl[i]|=ABM_DASH; // add in "-" if message not long enough
      if(clueorderindex<MXFL) mfl[i]&=treatcstr[i][clueorderindex];
      ml[i]=mfl[i]&~(mfl[i]-1); // bottom set bit
      if(ml[i]==0) goto ex0; // no possibilities: abort
      }
    }
  for(;;) { // loop over all combinations of msgchar:s
    for(i=0;i<NMSG;i++) if(curten&&treatorder[i]>0) msgcharAZ09[i]=ltochar[logbase2(ml[i])]; // extract msgchar:s from counters
DEB2 {
    printf("  building list with msgcharAZ09[]=");
    for(i=0;i<NMSG;i++) putchar(msgcharAZ09[i]);
    printf("\n");
    }
    for(i=0;i<atotal;i++) {
      curans=i;
      if((curdm&ansp[curans]->dmask)==0) continue; // not in a valid dictionary
      if(curten) u=treatans(ansp[curans]->ul);
      else       u=treatedanswer(ansp[curans]->ul);
      if(u) return u;
      }
    for(i=0;i<NMSG;i++) if(curten&&treatorder[i]>0) {
      b=mfl[i]&~(ml[i]|(ml[i]-1)); // clear bits mf[] and below
      b&=~(b-1); // find new bottom set bit
      if(b) {ml[i]=b; break;} // try next feasible character
      ml[i]=mfl[i]&~(mfl[i]-1); // reset to bottom set bit and proceed to advance next character
      }
    if(i==NMSG) break; // finish when all combinations done
    }
ex0:
  *l=malloc(ntfl*sizeof(int));
  if(*l==0) return 1;
  memcpy(*l,tfl,ntfl*sizeof(int));
  *ll=ntfl;
  DEB2 printf("%d entries\n",ntfl);
  return 0;
  }
