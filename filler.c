// $Id: filler.c 534 2014-02-05 13:00:14Z mo $

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

/*
   Interface to dicts.c comprises:
   pregetinitflist();
   getinitflist();
   postgetinitflist();

   Interface to qxw.c / gui.c comprises:
   Calls out:
   mkfeas();
   updatefeas();
   updategrid();
   Calls in:
   filler_init();
   filler_start();
   filler_stop();
   filler_finit();
   getposs();
   */

#include <glib.h>
#include <gdk/gdk.h>
#include <time.h>
#include "common.h"
#include "filler.h"
#include "dicts.h"
#include "qxw.h"
#include "gui.h"

static GThread*fth;

static int ct_malloc=0,ct_free=0; // counters for debugging
volatile int abort_flag=0;
static int fillmode=0; // 0=stopped, 1=filling all, 2=filling selection, 3=word lists only (for preexport)
static clock_t ct0;

int filler_status=0; // return code: -5: aborted; -3, -4: initflist errors; -2: out of stack; -1: out of memory; 0: stopped; 1: no fill found; 2: fill found; 3: running

// the following stacks keep track of the filler state as it recursively tries to fill the grid
static int sdep=-1; // stack pointer

static char**sposs=0;                // possibilities for this entry, 0-terminated
static int*spossp=0;                 // which possibility we are currently trying (index into sposs)
static int***sflist;                 // pointers to restore feasible word list flist
static struct jdata***sjdata=0;      // jumble data
static ABM***sjflbm=0;               // jumble data feasible list bitmaps
static struct sdata***ssdata=0;      // spread data
static int**sflistlen=0;             // pointers to restore flistlen
static ABM**sentryfl=0;              // feasible letter bitmap for this entry
static int*sentry=0;                 // entry considered at this depth

static unsigned char*aused=0;       // answer already used while filling
static unsigned char*lused=0;       // light already used while filling

#define isused(l) (lused[lts[l].uniq]|aused[lts[l].ans+NMSG])
#define setused(l,v) lused[lts[l].uniq]=v,aused[lts[l].ans+NMSG]=v // ,printf("setused(%d,%d)->%d\n",l,v,lts[l].uniq)

static void pstate(int f) {
	int i,j,jmode;
	struct word*w;
	struct entry*e;
	char s[MXFL+1];

	for(i=0;i<nw;i++) {
		w=words+i;
		if(w->lp->emask&EM_JUM) jmode=1;
		else if(w->lp->emask&EM_SPR) jmode=2;
		else                         jmode=0;
		printf("W%d: fe=%d jmode=%d nent=%d wlen=%d jlen=%d ",i,w->fe,jmode,w->nent,w->wlen,w->jlen);
		for(j=0;j<w->nent;j++) {
			e=w->e[j];
			s[j]=abmtoechar(e->flbm);
			if(s[j]==' ') s[j]='.';
			printf(" E%d:%016llx",(int)(e-entries),e->flbm);
		}
		s[j]=0;
		printf("\n  %s\n",s);
		if(f) {
			printf("  ");
			if(w->flistlen<8) j=0;
			else {
				for(j=0;j<4;j++) printf(" %s[%d]",lts[w->flist[j]].s,lts[w->flist[j]].uniq);
				printf(" ...");
				j=w->flistlen-4;
			}
			for(;j<w->flistlen;j++) printf(" %s[%d]",lts[w->flist[j]].s,lts[w->flist[j]].uniq);
			printf(" (%d)\n",w->flistlen);
		}
	}
}


// transfer progress info to display
static void progress(void) {
	DEB1 printf("ct_malloc=%d ct_free=%d diff=%d\n",ct_malloc,ct_free,ct_malloc-ct_free);
	gdk_threads_enter();
	updategrid();
	gdk_threads_leave();
}

static int initjdata(int j) {struct word*w; int i;
	w=words+j;
	if(!(w->lp->emask&EM_JUM)) return 0;
	if(w->fe) return 0;
	w->jdata=malloc(w->flistlen*sizeof(struct jdata));
	if(!w->jdata) return -1;
	w->jflbm=malloc(w->flistlen*w->jlen*sizeof(ABM));
	if(!w->jflbm) return -1;
	for(i=0;i<w->flistlen*w->jlen;i++) w->jflbm[i]=ABM_ALL;
	return 0;
}

static int initsdata(int j) {struct word*w; int i,k;
	w=words+j;
	if(!(w->lp->emask&EM_SPR)) return 0;
	if(w->fe) return 0;
	w->sdata=malloc(w->flistlen*sizeof(struct sdata));
	if(!w->sdata) return -1;
	for(i=0;i<w->flistlen;i++) for(k=0;k<w->nent;k++) w->sdata[i].flbm[k]=ABM_ALL;
	return 0;
}

// Determine if one string is a non-trivial cyclic permutation of another
static int strcyccmp(char*s,char*t,int l) {
	int i,j,k;

	for(i=1;i<l;i++) { // cyclic shift amount
		for(j=0,k=i;j<l;j++,k++) {
			if(k==l) k=0;
			if(s[j]!=t[k]) goto ex0;
		}
		return 1;
ex0:;
	}
	return 0;
}

// Check that jumble is not a disallowed permutation
// mode=0: use entry flbm:s; 1: use jdata flbm:s
static int checkperm(struct word*w,int j,int mode) {
	int em,k,m;
	char f[MXFL+1],r[MXFL+1],*t;

	em=w->lp->emask;
	m=w->jlen;
	for(k=0;k<m;k++) f[k]=r[m-1-k]=ltochar[logbase2(mode?w->jflbm[j*m+k]:w->e[k]->flbm)];
	f[k]=0; r[k]=0;
	t=lts[w->flist[j]].s;
	DEB16 printf("checkperm(%s : %s em=%d mode=%d)\n",f,t,em,mode);
	if((em&EM_FWD)==0) if(!strncmp(f,t,m)) return 0;
	if((em&EM_REV)==0) if(!strncmp(r,t,m)) return 0;
	if((em&EM_CYC)==0) if(strcyccmp(f,t,m)) return 0;
	if((em&EM_RCY)==0) if(strcyccmp(r,t,m)) return 0;
	return 1;
}

// Approximate test to see if a jumbled string can fit in a given word. Writes deductions to flbm etc. in jdata.
static int checkjword(struct word*w,int j) {
	unsigned char hi[NL];
	ABM bm[MXFL],u,v,*jbm;
	struct light*l;
	int c,c0,c1,f,i,k,m,n,nuf;

	l=lts+w->flist[j];
	m=w->jlen;
	jbm=w->jflbm+j*m;
	//  if(w-words==0) debug|=16; else debug&=~16;
	DEB16 {
		printf("checkjword(w=%ld,\"%s\") m=%d lbm=%016llx\n",(long int)(w-words),l->s,m,l->lbm);
		printf("hist:"); for(i=0;i<NL;i++) printf(" %d",l->hist[i]); printf("\n");
		printf("order:"); for(i=0;i<l->nhistorder;i++) printf(" %d",l->historder[i]); printf("\n");
	}
	for(k=0;k<m;k++) bm[k]=w->e[k]->flbm&l->lbm;
	do {
		DEB16 { printf("** "); for(k=0;k<m;k++) printf("%016llx ",bm[k]); printf("\n"); }
		f=0;
		memcpy(hi,l->hist,sizeof(hi));
		for(k=0;k<m;k++) {
			u=bm[k];
			if(!u) {DEB16 printf("empty bitmap\n"); return 0;} // infeasible
			if(onebit(u)) { // entry is forced
				c=logbase2(u);
				if(hi[c]--==0) {DEB16 printf("not enough in histogram for forced entries\n"); return 0;}
			}
		}

		for(u=0,n=0,nuf=0,i=0;i<l->nhistorder;i++) { // work from biggest histogram entry down, greedily looking for contradictions
			c=l->historder[i];
			n+=hi[c];   // accumulate histogram total of letters considered so far in this group
			nuf+=hi[c];
			v=1ULL<<c;
			u|=v;       // accumulate bitmap of letters considered so far
			for(c0=0,c1=0,k=0;k<m;k++) {
				if(onebit(bm[k])) continue; // these already taken off histogram
				if(bm[k]&v) c0++; // count places where this letter can go
				if(bm[k]&u) c1++; // count places where letters in this group can go
			}
			DEB16 { for(k=0;k<m;k++) printf("%016llx ",bm[k]); printf("\n"); }
			DEB16 printf("  c=%d n=%d nuf=%d u=%016llx v=%016llx c0=%d c1=%d\n",c,n,nuf,u,v,c0,c1);
			if(c0< hi[c]) {DEB16 printf("not enough slots for char %d\n",c); return 0;}
			if(c0==hi[c]) { // only just enough slots to go round for this letter
				for(k=0;k<m;k++) if(bm[k]&v) {if(bm[k]&~v) {f=1; bm[k]=v; n--;}}
				// At this point we may have newly-created "onebit" entries in bm[k], making it out of sync with hi[c].
				// However, we do not consider c again until the next time round the do loop, by which time hi[c] will have
				// been recalculated.
			}
			if(c1< n) {DEB16 printf("not enough slots for group %016llx: slot count=%d group count=%d\n",u,c1,n); return 0;}
			if(c1==n) { // only just enough slots to go round for this group
				for(k=0;k<m;k++) if(bm[k]&u) {if(bm[k]&~u) f=1; bm[k]&=u;}
				DEB16 printf("new group\n");
				n=0; // start a new group
				v=0;
				// Again we may have newly-created "onebit" entries in bm[k], making it out of sync with hi[c].
				// However, we do not consider any member of the set u again until the next time round the do loop.
			}
			w->jdata[j].nuf=nuf;
			w->jdata[j].ufhist[i]=hi[c];
			w->jdata[j].poscnt[i]=c0;
		}
	} while(f);
	memcpy(jbm,bm,m*sizeof(ABM));
	DEB16 {
		printf("checkjword returning: w=%ld \"%s\" nuf=%d hist(poscnt)=",(long int)(w-words),l->s,nuf);
		for(i=0;i<l->nhistorder;i++) printf("%d(%d) ",w->jdata[j].ufhist[i],w->jdata[j].poscnt[i]);
		printf(" flbm:");
		for(i=0;i<m;i++) printf(" %016llx",jbm[i]);
		printf("\n");
	}
	for(i=0;i<m;i++) if(!onebit(jbm[i])) break;
	if(i==m) { // all entries are forced
		i=checkperm(w,j,1);
		DEB16 printf("checkperm returns %d\n",i);
		if(i==0) return 0;
	}
	DEB16 printf("checkjword: OK\n");
	return 1;
}

// Calculate number of possible spreads that put each possible letter in each position
// given implications of flbm:s.
static void scounts(struct word*w,int wn,ABM*bm) {
	struct sdata*sd;
	struct light*l;
	int i,j,m,n;
	ABM u;
	// following are static to avoid overflowing stack (!) in Windows version
	static double ctl[MXFL+1][MXFL+1]; // ctl[i][j] is # of arrangements where chars [0,i) fit in slots [0,j)
	static double ctr[MXFL+1][MXFL+1]; // ctr[i][j] is # of arrangements where chars [i,n) fit in slots [j,m)

	l=lts+w->flist[wn];
	m=w->nent;
	n=w->wlen;
	sd=w->sdata+wn;
	DEB16 printf("scounts: w=%ld \"%s\"\n",(long int)(w-words),l->s);
	memset(ctl,0,sizeof(ctl));
	ctl[0][0]=1;
	for(j=1;j<=m;j++) {
		u=bm[j-1];
		for(i=0;i<=n;i++) {
			if(u&ABM_DASH) ctl[i][j]=ctl[i][j-1];
			if(i>0&&(u&chartoabm[(int)l->s[i-1]])) ctl[i][j]+=ctl[i-1][j-1];
		}
	}
	memset(ctr,0,sizeof(ctr));
	ctr[n][m]=1;
	for(j=m-1;j>=0;j--) {
		u=bm[j];
		for(i=n;i>=0;i--) {
			if(u&ABM_DASH) ctr[i][j]=ctr[i][j+1];
			if(i<n&&(u&chartoabm[(int)l->s[i]])) ctr[i][j]+=ctr[i+1][j+1];
		}
	}
	DEB16 {
		printf("CTL: # of ways to put chars [0,i) in slots [0,j)\n");
		for(j=0;j<=m;j++) {
			for(i=0;i<=n;i++) printf("%5.1f ",ctl[i][j]);
			printf("\n");
		}
		printf("CTR: # of ways to put chars [i,n) in slots [j,m)\n");
		for(j=0;j<=m;j++) {
			for(i=0;i<=n;i++) printf("%5.1f ",ctr[i][j]);
			printf("\n");
		}
	}
	memset(sd->ct,0,sizeof(sd->ct));
	memset(sd->ctd,0,sizeof(sd->ctd));
	for(i=0;i< n;i++) for(j=0;j<m;j++) sd->ct[i][j]=ctl[i][j]*ctr[i+1][j+1];
	for(j=0;j<m;j++) if(bm[j]&ABM_DASH) for(i=0;i<=n;i++) sd->ctd[j] +=ctl[i][j]*ctr[i][j+1]; // do the spreading character as a special case
	DEB16 {
		printf("        " ); for(i=0;i<n;i++) printf("  %c   ",l->s[i]); printf("      -\n");
		for(j=0;j<m;j++) {printf("  e%2d:",j); for(i=0;i<n;i++) printf(" %5.1f",sd->ct[i][j]); printf("     %5.1f",sd->ctd[j]); printf("\n");}
	}
	memset(sd->flbm,0,sizeof(sd->flbm));
	for(i=0;i<n;i++) for(j=0;j<m;j++) if(sd->ct[i][j]) sd->flbm[j]|=chartoabm[(int)l->s[i]];
	for(j=0;j<m;j++) if(sd->ctd[j])   sd->flbm[j]|=ABM_DASH;
}

// Test to see if a spread string can fit in a given word. Writes deductions to sdata.
static int checksword(struct word*w,int j) {
	ABM bm[MXFL];
	struct light*l;
	struct sdata*sd;
	int k,m; //,n;

	l=lts+w->flist[j];
	sd=w->sdata+j;
	m=w->nent;
	//  n=w->wlen;
	for(k=0;k<m;k++) bm[k]=w->e[k]->flbm;
	DEB16 {
		printf("checksword(w=%ld,\"%s\") bm=",(long int)(w-words),l->s);
		for(k=0;k<m;k++) printf("%016llx ",bm[k]); printf("\n");
	}
	scounts(w,j,bm);
	DEB16 {
		printf("  output bm=");
		for(k=0;k<m;k++) printf("%016llx ",sd->flbm[k]); printf("\n");
	}
	return 1;
}

// intersect light list q length l with letter position wp masked by bitmap m: result is stored in p and new length is returned
static int listisect(int*p,int*q,int l,int wp,ABM m) {int i,j;
	for(i=0,j=0;i<l;i++) if(m&(chartoabm[(int)(lts[q[i]].s[wp])])) p[j++]=q[i];
	//printf("listisect l(wp=%d m=%16llx) %d->%d\n",wp,m,l,j);
	return j;
}

// find the entry to expand next, or -1 if all done
static int findcritent(void) {int i,j,m;double k,l;
	m=-1;
	for(i=0;i<ne;i++) {
		if(fillmode==2&&entries[i].sel==0) continue; // filling selection only: only check relevant entries
		if(onebit(entries[i].flbm)) continue;
		if(entries[i].checking>m) m=entries[i].checking; // find highest checking level // find highest checking level
	}
	j=-1;
	for(;m>0;m--) { // m>=2: loop over checked entries; then m=1: loop over unchecked entries
		j=-1;l=DBL_MAX;
		for(i=0;i<ne;i++) {
			if(fillmode==2&&entries[i].sel==0) continue; // filling selection only: only check relevant entries
			if(!onebit(entries[i].flbm)&&entries[i].checking>=m) { // not already fixed?
				k=entries[i].crux; // get the priority for this entry
				if(k<l) l=k,j=i;}
		}
		if(j!=-1) return j; // return entry with highest priority if one found
	}
	return j; // return -1 if no entries left to expand
}

// check updated entries and rebuild feasible word lists
// returns -3 for aborted, -2 for infeasible, -1 for out of memory, 0 if no feasible word lists affected, >=1 otherwise
static int settleents(void) {
	struct entry*e;
	struct word*w;
	struct jdata*jd;
	struct sdata*sd;
	ABM*jdf;
	int aed,f,i,j,k,l,m,mj,jmode;
	int*p;
	//  DEB1 printf("settleents() sdep=%d\n",sdep);
	f=0;
	for(j=0;j<nw;j++) {
		if(abort_flag) return -3;
		w=words+j;
		if(w->lp->emask&EM_JUM) jmode=1;
		else if(w->lp->emask&EM_SPR) jmode=2;
		else                         jmode=0;
		//    printf("j=%d jmode=%d emask=%d\n",j,jmode,w->lp->emask);
		m=w->nent;
		mj=w->jlen;
		for(k=0;k<m;k++) if(w->e[k]->upd) break;
		if(k==m) continue; // no update flags set on any entries for this word
		for(k=0;k<m;k++) if(!onebit(w->e[k]->flbm)) break;
		aed=(k==m); // all entries determined?
		p=w->flist;
		l=w->flistlen;
		jd=w->jdata;
		sd=w->sdata;
		if(sflistlen[sdep][j]==-1) {  // then we mustn't trash words[].flist
			sflist   [sdep][j]=p;
			sflistlen[sdep][j]=l;
			sjdata   [sdep][j]=jd;
			sjflbm   [sdep][j]=w->jflbm;
			ssdata   [sdep][j]=sd;
			w->jdata=0;
			w->jflbm=0;
			w->sdata=0;
			w->flist=(int*)malloc(l*sizeof(int)); // new list can be at most as long as old one
			if(!w->flist) return -1; // out of memory
			ct_malloc++;
			if(jmode==1) {
				w->jdata=(struct jdata*)malloc(l*sizeof(struct jdata));
				if(!w->jdata) return -1; // out of memory
				ct_malloc++;
				w->jflbm=(ABM*)malloc(l*mj*sizeof(ABM));
				if(!w->jflbm) return -1; // out of memory
				ct_malloc++;
			}
			if(jmode==2) {
				w->sdata=(struct sdata*)malloc(l*sizeof(struct sdata));
				if(!w->sdata) return -1; // out of memory
				ct_malloc++;
			}
		}
		if(afunique) { // the following test makes things quite a lot slower: consider optimising by keeping track of when an update might be needed
			for(i=0,k=0;i<l;i++) if(!isused(p[i])) w->flist[k++]=p[i];
			p=w->flist;
			l=k;
		}

		if(jmode==0) { // normal case
			for(k=0;k<m;k++) { // think about moving this loop inside listisect()
				e=w->e[k];
				if(!e->upd) continue;
				l=listisect(w->flist,p,l,k,e->flbm); // generate new feasible word list
				p=w->flist;
				if(l==0) break;
			}
		} else if(jmode==1) { // jumble case
			for(k=mj;k<m;k++) { // loop over tags if any
				e=w->e[k];
				if(!e->upd) continue;
				l=listisect(w->flist,p,l,k,e->flbm); // generate new feasible word list
				p=w->flist;
			}
			for(i=0,k=0;i<l;i++) {w->flist[k]=p[i]; if(checkjword(w,k)) k++;}
			l=k;
			w->upd=1; f++; // need to do settlents() anyway in this case
		} else { // spread case
			for(i=0,k=0;i<l;i++) {w->flist[k]=p[i]; if(checksword(w,k)) k++;}
			l=k;
			w->upd=1; f++; // need to do settlents() anyway in this case
		}

		if(l!=w->flistlen) {
			w->upd=1;f++; // word list has changed: feasible letter lists will need updating
			if(l) {
				p=realloc(w->flist,l*sizeof(int));
				if(p) w->flist=p;
				if(w->jdata) {
					jd=realloc(w->jdata,l*sizeof(struct jdata));
					if(jd) w->jdata=jd;
					jdf=realloc(w->jflbm,l*mj*sizeof(ABM));
					if(jdf) w->jflbm=jdf;
				}
				if(w->sdata) {
					sd=realloc(w->sdata,l*sizeof(struct sdata));
					if(sd) w->sdata=sd;
				}
			}
		}
		w->flistlen=l;
		if(l==0&&!w->fe) return -2; // no options left and was not fully entered by user
		if(!aed) continue; // not all entries determined yet, so don't commit
		if(jmode==1) { // final check that the "jumble" is not actually a cyclic permutation etc.
			for(i=0,k=0;i<l;i++) {w->flist[k]=w->flist[i]; if(checkperm(w,k,0)) k++;}
			l=k;
		}
		w->flistlen=l;
		if(l==0&&!w->fe) return -2; // no options left and was not fully entered by user
		assert(w->commitdep==-1);
		for(k=0;k<l;k++) setused(w->flist[k],1); // flag as used (can be more than one in jumble case)
		w->commitdep=sdep;
	}

	for(i=0;i<ne;i++) entries[i].upd=0; // all entry update effects now propagated into word updates
	//  DEB1 printf("settleents returns %d\n",f);fflush(stdout);
	return f;
}

// check updated word lists, rebuild feasible entry lists
// returns -3 for aborted, 0 if no feasible letter lists affected, >0 otherwise
static int settlewds(void) {
	int f,i,j,k,l,m,mj,jmode;
	int*p;
	struct entry*e;
	struct word*w;
	ABM entfl[MXFL];
	//  DEB1 printf("settlewds()\n");
	f=0;
	for(i=0;i<nw;i++) {
		if(abort_flag) return -3;
		w=words+i;
		if(!w->upd) continue; // loop over updated word lists
		if(w->fe) continue;
		if(w->lp->emask&EM_JUM) jmode=1;
		else if(w->lp->emask&EM_SPR) jmode=2;
		else                         jmode=0;
		m=w->nent;
		mj=w->jlen;
		p=w->flist;
		l=w->flistlen;

		for(k=0;k<m;k++) entfl[k]=0;
		if(jmode==0) for(j=0;j<l;j++) for(k=0;k<m;k++) entfl[k]|=chartoabm[(int)lts[p[j]].s[k]]; // find all feasible letters from word list
		else if(jmode==1) for(j=0;j<l;j++) {
			for(k=0;k<mj;k++) entfl[k]|=w->jflbm[j*mj+k]; // main work has been done in settleents()
			for(   ;k<m ;k++) entfl[k]|=chartoabm[(int)lts[p[j]].s[k]];
		}
		else if(jmode==2) for(j=0;j<l;j++) for(k=0;k<m;k++) entfl[k]|=w->sdata[j].flbm[k]; // main work has been done in settleents()

		DEB16 {
			printf("w=%d entfl: ",i);
			for(k=0;k<m;k++) printf(" %016llx",entfl[k]);
			printf("\n");
		}
		for(j=0;j<m;j++) {
			e=w->e[j]; // propagate from word to entry
			if(e->flbm&~entfl[j]) { // has this entry been changed by the additional constraint?
				e->flbm&=entfl[j];
				e->upd=1;f++; // flag that it will need updating
				//      printf("E%d %16llx\n",k,entries[k].flbm);fflush(stdout);
			}
		}
	}
	for(i=0;i<nw;i++) words[i].upd=0; // all word list updates processed
	//  DEB1 printf("settlewds returns %d\n",f);fflush(stdout);
	return f;
}

// Very approximate attempt to estimate number of possible jumbles that put each possible letter in each position
// given implications of flbm:s.
// Returns equally poor estimate of total number of permutations.
static double jscores(struct word*w,int j,double(*sc)[NL]) {
	struct jdata*jd;
	struct light*l;
	int c,h,i,k,m,nuf,p,t;
	double r,s;
	double tp;
	ABM u,*jbm;

	l=lts+w->flist[j];
	m=w->jlen;
	jd=w->jdata+j;
	jbm=w->jflbm+j*m;
	nuf=jd->nuf;
	DEB16 {
		printf("jscores: w=%ld \"%s\" nuf=%d hist(poscnt)=",(long int)(w-words),l->s,nuf);
		for(i=0;i<l->nhistorder;i++) printf("%d(%d) ",jd->ufhist[i],jd->poscnt[i]);
	}
	// first make a very poor man's estimate of total feasible permutations given jdata information for word
	t=nuf;
	tp=1;
	for(i=0;i<l->nhistorder;i++) {
		h=jd->ufhist[i]; p=jd->poscnt[i];
		if(!h||h==nuf) continue;
		r=(double)(p-h)/(nuf-h)*(t-h)+h; // if p==h, r=h; if p==nuf, r=t; and linearly (!) in between
		for(k=1;k<=h;k++) tp*=r,tp/=k,r-=1;
		t-=h;
	}
	DEB16 {
		printf("  tp==%g\n",tp);
		printf("  flbm:"); for(k=0;k<m;k++) printf(" %016llx",jbm[k]); printf("\n");
	}
	memset(sc,0,m*NL*sizeof(double));
	for(k=0;k<m;k++) {
		u=jbm[k];
		if(u==0) continue;
		if(onebit(u)) sc[k][logbase2(u)]=tp; // forced entry
		else {
			for(r=0,i=0;i<l->nhistorder;i++) {
				c=l->historder[i];
				if(jd->poscnt[i]==0) continue;
				if(u&(1ULL<<c)) {
					s=(double)jd->ufhist[i]/jd->poscnt[i]; // proportion of fills that will have letter c in each position
					r+=sc[k][c]=s;
				}
			}
			if(r>0) r=tp/r;
			for(i=0;i<NL;i++) sc[k][i]*=r;
		}
		DEB16 {       printf("  e%d:",k); for(i=0;i<26;i++) printf(" %5.1f",sc[k][i]); printf("\n"); }
	}
	return tp;
}



// calculate scores for spread entry
static void sscores(struct word*w,int wn,double(*sc)[NL]) {
	struct sdata*sd;
	struct light*l;
	int c,i,j,k,m,n;

	l=lts+w->flist[wn];
	m=w->nent;
	n=w->wlen;
	sd=w->sdata+wn;
	memset(sc,0,m*NL*sizeof(double));
	for(i=0;i<n;i++) {
		c=chartol[(int)l->s[i]];
		for(j=0;j<m;j++) sc[j][c]+=sd->ct[i][j];
	}
	// now do the spreading character as a special case: i=#chars to left of candidate '-'
	for(j=0;j<m;j++) sc[j][NL-1]+=sd->ctd[j];
	DEB16 for(k=0;k<m;k++) {printf("  e%2d:",k); for(i=0;i<NL;i++) {printf(" %5.1f",sc[k][i]); if(i==25||i==35) printf("  ");} printf("\n"); }
}


// calculate per-entry scores
// returns -3 if aborted
static int mkscores(void) {
	int c,i,j,k,l,m,mj,jmode;
	int*p;
	double f;
	struct word*w;
	// following static to reduce stack use
	static double sc[MXFL][NL],tsc[MXFL][NL]; // weighted count of number of words that put a given letter in a given place

	for(i=0;i<ne;i++) for(j=0;j<NL;j++) entries[i].score[j]=1.0;
	for(i=0;i<nw;i++) {
		if(abort_flag) return -3;
		w=words+i;
		if(w->fe) continue;
		if(w->lp->emask&EM_JUM) jmode=1;
		else if(w->lp->emask&EM_SPR) jmode=2;
		else                         jmode=0;
		m=w->nent;
		mj=w->jlen;
		p=w->flist;
		l=w->flistlen;
		for(k=0;k<m;k++) for(j=0;j<NL;j++) sc[k][j]=0.0;

		if(jmode==0) { // normal case
			if(afunique&&w->commitdep>=0) {  // avoid zero score if we've committed
				if(l==1) for(k=0;k<m;k++) sc[k][chartol[(int)lts[p[0]].s[k]]]+=1.0;
			}
			else {
				for(j=0;j<l;j++) if(!(afunique&&isused(p[j]))) { // for each remaining feasible word
					if(lts[p[j]].ans<0) f=1;
					else f=(double)ansp[lts[p[j]].ans]->score;
					for(k=0;k<m;k++) sc[k][chartol[(int)lts[p[j]].s[k]]]+=f; // add in its score to this cell's score
				}
			}
		} else if(jmode==1) { // jumble case
			for(j=0;j<l;j++) {
				f=jscores(w,j,tsc);
				for(k=0;k<mj;k++) for(c=0;c<NL;c++) sc[k][c]+=tsc[k][c];
				k=lts[p[j]].ans;
				if(k>=0) f*=(double)ansp[k]->score; // score once for each feasible permutation; assume score=1 if a treatment light
				for(k=mj;k<m;k++) sc[k][chartol[(int)lts[p[j]].s[k]]]+=f;
			}
		} else { // spread case
			for(j=0;j<l;j++) {
				sscores(w,j,tsc);
				for(k=0;k<m;k++) for(c=0;c<NL;c++) sc[k][c]+=tsc[k][c];
			}
		}

		for(k=0;k<m;k++) for(j=0;j<NL;j++) w->e[k]->score[j]*=sc[k][j];
	}
	for(i=0;i<ne;i++) {
		f=-DBL_MAX; for(j=0;j<NL;j++) f=MX(f,entries[i].score[j]);
		entries[i].crux=f; // crux at an entry is the greatest score over all possible letters
	}
	return 0;
}


// sort possible letters into order of decreasing favour with randomness r; write results to s
void getposs(struct entry*e,char*s,int r,int dash) {int i,l,m,n,nl;double j,k;
	//  DEB2 printf("getposs(%d)\n",(int)(e-entries));
	nl=dash?NL:NL-1; // avoid outputting dashes?
	l=0;
	k=-DBL_MAX; for(i=0;i<nl;i++) if(e->score[i]>k) k=e->score[i]; // find highest score
	k*=2;
	for(;;) {
		for(i=0,j=-DBL_MAX;i<nl;i++) if(e->score[i]>j&&e->score[i]<k) j=e->score[i]; // peel off scores from top down
		//    DEB2 printf("getposs(%d): j=%g\n",(int)(e-entries),j);
		if(j<=0) break;
		for(i=0;i<nl;i++) if(e->score[i]==j) s[l++]=ltochar[i]; // add to output string
		k=j;} // get next highest set of equal scores
	s[l]='\0';
	if(r==0) return;
	for(i=0;i<l;i++) { // randomise if necessary
		m=i+rand()%(r*2+1); // candidate for swap: distance depends on randomisation level
		if(m>=0&&m<l) n=s[i],s[i]=s[m],s[m]=n; // swap candidates
	}
}

// indent according to stack depth
static void sdepsp(void) {int i; if(sdep<0) printf("<%d",sdep); for(i=0;i<sdep;i++) printf(" ");}

static void freestack() {int i;
	for(i=0;i<=ne;i++) {
		if(sposs     ) FREEX(sposs     [i]);
		if(sflist    ) FREEX(sflist    [i]);
		if(sjdata    ) FREEX(sjdata    [i]);
		if(sjflbm    ) FREEX(sjflbm    [i]);
		if(ssdata    ) FREEX(ssdata    [i]);
		if(sflistlen ) FREEX(sflistlen [i]);
		if(sentryfl  ) FREEX(sentryfl  [i]);
	}
	FREEX(sposs);
	FREEX(spossp);
	FREEX(sflist);
	FREEX(sjdata);
	FREEX(sjflbm);
	FREEX(ssdata);
	FREEX(sflistlen);
	FREEX(sentryfl);
	FREEX(sentry);
}

static int allocstack() {int i;
	freestack();
	if(!(sposs     =calloc(ne+1,sizeof(char*         )))) return 1;
	if(!(spossp    =calloc(ne+1,sizeof(int           )))) return 1;
	if(!(sflist    =calloc(ne+1,sizeof(int**         )))) return 1;
	if(!(sjdata    =calloc(ne+1,sizeof(struct jdata**)))) return 1;
	if(!(sjflbm    =calloc(ne+1,sizeof(ABM**         )))) return 1;
	if(!(ssdata    =calloc(ne+1,sizeof(struct sdata**)))) return 1;
	if(!(sflistlen =calloc(ne+1,sizeof(int*          )))) return 1;
	if(!(sentryfl  =calloc(ne+1,sizeof(ABM*          )))) return 1;
	if(!(sentry    =calloc(ne+1,sizeof(int           )))) return 1;
	for(i=0;i<=ne;i++) { // for each stack depth that can be reached
		if(!(sposs     [i]=malloc(NL+1                    ))) return 1;
		if(!(sflist    [i]=malloc(nw*sizeof(int*         )))) return 1;
		if(!(sjdata    [i]=malloc(nw*sizeof(struct jdata*)))) return 1;
		if(!(sjflbm    [i]=malloc(nw*sizeof(ABM*         )))) return 1;
		if(!(ssdata    [i]=malloc(nw*sizeof(struct sdata*)))) return 1;
		if(!(sflistlen [i]=malloc(nw*sizeof(int          )))) return 1;
		if(!(sentryfl  [i]=malloc(ne*sizeof(ABM          )))) return 1;
	}
	return 0;
}

// initialise state stacks
static void state_init(void) {sdep=-1;filler_status=0;}

// push stack
static void state_push(void) {int i;
	sdep++;
	assert(sdep<=ne);
	for(i=0;i<nw;i++) sflistlen[sdep][i]=-1;  // flag that flists need allocating
	for(i=0;i<ne;i++) sentryfl[sdep][i]=entries[i].flbm; // feasible letter lists
}

// undo effect of last deepening operation
static void state_restore(void) {int i,j,l; struct word*w;
	for(i=0;i<nw;i++) {
		w=words+i;
		if(w->commitdep>=sdep) { // word to uncommit?
			l=w->flistlen;
			DEB16 {
				printf("sdep=%d flistlen=%d uncommitting word %d commitdep=%d:",sdep,w->flistlen,i,w->commitdep);
				for(j=0;j<l;j++) printf(" %s",lts[w->flist[j]].s);
				printf("\n");
			}
			for(j=0;j<l;j++) setused(w->flist[j],0);
			w->commitdep=-1;
		}
		if(sflistlen[sdep][i]!=-1&&w->flist!=0) { // word feasible list to free?
			free(w->flist);
			ct_free++;
			w->flist=sflist[sdep][i];
			w->flistlen=sflistlen[sdep][i];
			if(w->jdata) {
				free(w->jdata);
				ct_free++;
				w->jdata=sjdata[sdep][i];
			}
			if(w->jflbm) {
				free(w->jflbm);
				ct_free++;
				w->jflbm=sjflbm[sdep][i];
			}
			if(w->sdata) {
				free(w->sdata);
				ct_free++;
				w->sdata=ssdata[sdep][i];
			}
		}
	}
	for(i=0;i<ne;i++) entries[i].flbm=sentryfl[sdep][i];
}

// pop stack
static void state_pop(void) {assert(sdep>=0);state_restore();sdep--;}

// clear state stacks and free allocated memory
static void state_finit(void) {
	while(sdep>=0) state_pop();
	freestack();
}

// build initial feasible lists, calling plug-in as necessary
static int buildlists(void) {int u,i,j;
	for(i=0;i<nw;i++) {
		FREEX(words[i].flist);
		FREEX(words[i].jdata);
		FREEX(words[i].jflbm);
		FREEX(words[i].sdata);
		lightx=words[i].gx0;
		lighty=words[i].gy0;
		lightdir=words[i].ldir;
		for(j=0;j<words[i].nent;j++) {
			gridorderindex[j]=words[i].goi[j];
			checking[j]=words[i].e[j]->checking;
		}
		u=getinitflist(&words[i].flist,&words[i].flistlen,words[i].lp,words[i].wlen);
		if(abort_flag) {
			DEB1 printf("aborted while building word lists\n");
			filler_status=-5;
			return 1;
		}
		if(u) {filler_status=-3;return 0;}
		if(words[i].lp->ten) clueorderindex++;
		if(initjdata(i)) {filler_status=-3;return 0;}
		if(initsdata(i)) {filler_status=-3;return 0;}
	}
	if(postgetinitflist()) {filler_status=-4;return 1;}
	FREEX(aused);
	FREEX(lused);
	aused=(unsigned char*)calloc(atotal+NMSG,sizeof(unsigned char)); // enough for "msgword" answers too
	if(aused==NULL) {filler_status=-3;return 0;}
	lused=(unsigned char*)calloc(ultotal,sizeof(unsigned char));
	if(lused==NULL) {filler_status=-3;return 0;}
	return 0;
}

// Main search routine. Returns
// -5: told to abort
// -1: out of memory
// -2: out of stack
//  1: all done, no result found
//  2: all done, result found or only doing BG fill anyway
static int search() {
	int e,f;
	char c;
	clock_t ct1;

	// Initially entry flbms are not consistent with word lists or vice versa. So we
	// need to make sure we call both settlewds() and settleents() before proceeding.
	if(settlewds()==-3) {DEB1 printf("aborting...\n"); return -5;};
resettle: // "unit propagation"
	do {
		if(abort_flag) {DEB1 printf("aborting...\n"); return -5;}
		f=settleents(); // rescan entries
		if(f==-3) {DEB1 printf("aborting...\n"); return -5;}
		if(f==0) break;
		if(f==-1) return -1; // out of memory: abort
		if(f==-2) goto backtrack; // proved impossible
		f=settlewds(); // rescan words
		if(f==-3) {DEB1 printf("aborting...\n"); return -5;}
	} while(f); // need to iterate until everything settles down
	f=mkscores();
	if(f==-3) {DEB1 printf("aborting...\n"); return -5;}
	if(fillmode==0||fillmode==3) return 2; // only doing BG/preexport fill? stop after first settle
	DEB16 pstate(1);

	// go one level deeper in search tree
	DEB1 { int w; for(w=0;w<nw;w++) printf("[w%d: %d]",w,words[w].flistlen); printf("\n"); }
	e=findcritent(); // find the most critical entry, over whose possible letters we will iterate
	if(e==-1) return 2; // all done, result found
	getposs(entries+e,sposs[sdep],afrandom,1); // find feasible letter list in descending order of score
	DEB1{printf("D%3d ",sdep);sdepsp();printf("E%d %s\n",e,sposs[sdep]);fflush(stdout);}
	sentry[sdep]=e;
	spossp[sdep]=0; // start on most likely possibility

	// try one possibility at the current critical entry
nextposs:
	e=sentry[sdep];
	if(sposs[sdep][spossp[sdep]]=='\0') goto backtrack; // none left: backtrack
	c=sposs[sdep][spossp[sdep]++]; // get letter to try
	DEB1 {  printf("D%3d ",sdep);sdepsp();printf(":%c:\n",c);fflush(stdout); }
	if(sdep==ne) return -2; // out of stack space (should never happen)
	state_push();
	entries[e].upd=1;
	entries[e].flbm=chartoabm[(int)c]; // fix feasible list
	ct1=clock(); if(ct1-ct0>CLOCKS_PER_SEC*3||ct1-ct0<0) {progress();ct0=clock();} // update display every three seconds or so
	goto resettle; // update internal data from new entry

backtrack:
	state_pop();
	if(sdep!=-1) goto nextposs;
	return 1; // all done, no solution found
}

static void searchdone() {
	int i;
	DEB1 printf("searchdone: A\n");
	gdk_threads_enter();
	DEB1 printf("searchdone: B\n");
	if(abort_flag==0) { // finishing gracefully?
		if(filler_status==2) {
			mkfeas(); // construct feasible word list
			DEB1 pstate(1);
		}
		else {
			for(i=0;i<ne;i++) entries[i].flbm=0; // clear feasible letter bitmaps
			llistp=NULL;llistn=0; // no feasible word list
			DEB1 printf("BG fill failed\n"),fflush(stdout);
		}
		if(fillmode==1||fillmode==2) killcurrdia();
		updatefeas();
	}
	updategrid();
	gdk_threads_leave();
	DEB1 printf("searchdone: C\n");
	state_finit();
	// if(fillmode&&currentdia) gtk_dialog_response(GTK_DIALOG(currentdia),GTK_RESPONSE_CANCEL);
	for(i=0;i<nw;i++) {
		if(words[i].commitdep>=0) printf("assertion failing i=%d nw=%d words[i].commitdep=%d\n",i,nw,words[i].commitdep);
		assert(words[i].commitdep==-1); // ... and uncommitted
	}
	DEB1 printf("search done\n");
	DEB1 printf(">> ct_malloc=%d ct_free=%d diff=%d\n",ct_malloc,ct_free,ct_malloc-ct_free);fflush(stdout);
	// j=0; for(i=0;i<ltotal;i++) j+=isused[i]; printf("total lused=%d\n",j);fflush(stdout);
	return;
}

static gpointer fillerthread(gpointer data) {
	int i;
	clock_t ct;

	ct=ct0=clock();
	clueorderindex=0;
	if(buildlists()) goto ex0;
	DEB1 pstate(1);
	for(i=0;i<ne;i++) entries[i].upd=1;
	for(i=0;i<nw;i++) words[i].upd=1;
	filler_status=search();
	if(fillmode!=3) searchdone(); // tidy up unless in pre-export mode
	DEB1 printf("search finished: %.3fs\n",(double)(clock()-ct)/CLOCKS_PER_SEC);
ex0:
	DEB1 printf("fillerthread() terminating filler_status=%d\n",filler_status);
	return 0;
}





// EXTERNAL INTERFACE

// returns !=0 on error
int filler_start(int mode) {int i;
	assert(fth==0);
	DEB1 printf("filler_start(%d)\n",mode);
	DEB1 pstate(0);
	fillmode=mode;
	if(allocstack()) return 1;
	if(pregetinitflist()) return 1;
	state_init();
	for(i=0;i<nw;i++) words[i].commitdep=-1; // flag word uncommitted
	state_push();
	filler_status=3;
	fth=g_thread_create_full(&fillerthread,0,0,1,1,fillmode==3?G_THREAD_PRIORITY_LOW:G_THREAD_PRIORITY_NORMAL,0);
	if(!fth) { state_finit(); return 1; }
	return 0;
}

void filler_wait() {
	DEB1 printf("filler_wait() A\n");
	if(fth) {
		gdk_threads_leave();
		g_thread_join(fth);
		fth=0;
		gdk_threads_enter();
	}
	DEB1 printf("filler_wait() B\n");
}

void filler_stop(void) {
	DEB1 printf("filler_stop() A\n");
	//  if(filler_status!=3) return;
	abort_flag=1;
	filler_wait();
	abort_flag=0;
	DEB1 printf("filler_stop() B\n");
	state_finit();
	DEB1 printf("filler_stop() C\n");
}

void filler_init() {
}

void filler_finit() {
}
