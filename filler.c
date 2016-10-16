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

// 0 = stopped, 1 = filling all, 2 = filling selection, 3 = word lists only (for preexport)
static int fillmode;
static clock_t ct0;

// return code: -5: aborted; -3, -4: initflist errors; -2: out of stack; -1: out of memory; 0: stopped; 1: no fill found; 2: fill found; 3: running
int filler_status;

// the following stacks keep track of the filler state as it recursively tries to fill the grid
static int sdep = -1; // stack pointer

static char **sposs;               // possibilities for this entry, 0-terminated
static int *spossp;                // which possibility we are currently trying (index into sposs)
static int ***sflist;              // pointers to restore feasible word list flist
static struct sdata ***ssdata;     // spread data
static int **sflistlen;            // pointers to restore flistlen
static ABM **sentryfl;             // feasible letter bitmap for this entry
static int *sentry;                // entry considered at this depth

static unsigned char *aused;       // answer already used while filling
static unsigned char *lused;       // light already used while filling

#define isused(l) (lused[lts[l].uniq] | aused[lts[l].ans+NMSG])
#define setused(l,v) do { \
	lused[lts[l].uniq] = v; \
	aused[lts[l].ans+NMSG] = v; \
} while (0)

static void pstate(int f) {
	int i,j,jmode;
	struct word*w;
	struct entry*e;
	char s[MXFL+1];

	for(i = 0;i<nw;i++) {
		w = words+i;
		if (w->lp->emask&EM_SPR) jmode = 2;
		else                         jmode = 0;
		printf("W%d: fe = %d jmode = %d nent = %d wlen = %d jlen = %d ",i,w->fe,jmode,w->nent,w->wlen,w->jlen);
		for(j = 0;j<w->nent;j++) {
			e = w->e[j];
			s[j] = abmtoechar(e->flbm);
			if (s[j] == ' ') s[j] = '.';
			printf(" E%d:%016llx",(int)(e-entries),e->flbm);
		}
		s[j] = 0;
		printf("\n  %s\n",s);
		if (f) {
			printf("  ");
			if (w->flistlen<8) j = 0;
			else {
				for(j = 0;j<4;j++) printf(" %s[%d]",lts[w->flist[j]].s,lts[w->flist[j]].uniq);
				printf(" ...");
				j = w->flistlen-4;
			}
			for(;j<w->flistlen;j++) printf(" %s[%d]",lts[w->flist[j]].s,lts[w->flist[j]].uniq);
			printf(" (%d)\n",w->flistlen);
		}
	}
}

static int initsdata(int j) {struct word*w; int i,k;
	w = words+j;
	if (!(w->lp->emask&EM_SPR)) return 0;
	if (w->fe) return 0;
	w->sdata = malloc(w->flistlen*sizeof(struct sdata));
	if (!w->sdata) return -1;
	for(i = 0;i<w->flistlen;i++) for(k = 0;k<w->nent;k++) w->sdata[i].flbm[k] = ABM_ALL;
	return 0;
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

	l = lts+w->flist[wn];
	m = w->nent;
	n = w->wlen;
	sd = w->sdata+wn;
	DEB16 printf("scounts: w = %ld \"%s\"\n",(long int)(w-words),l->s);
	memset(ctl,0,sizeof(ctl));
	ctl[0][0] = 1;
	for(j = 1;j <= m;j++) {
		u = bm[j-1];
		for(i = 0;i <= n;i++) {
			if (u&ABM_DASH) ctl[i][j] = ctl[i][j-1];
			if (i>0&&(u&chartoabm[(int)l->s[i-1]])) ctl[i][j] += ctl[i-1][j-1];
		}
	}
	memset(ctr,0,sizeof(ctr));
	ctr[n][m] = 1;
	for(j = m-1;j >= 0;j--) {
		u = bm[j];
		for(i = n;i >= 0;i--) {
			if (u&ABM_DASH) ctr[i][j] = ctr[i][j+1];
			if (i<n&&(u&chartoabm[(int)l->s[i]])) ctr[i][j] += ctr[i+1][j+1];
		}
	}
	DEB16 {
		printf("CTL: # of ways to put chars [0,i) in slots [0,j)\n");
		for(j = 0;j <= m;j++) {
			for(i = 0;i <= n;i++) printf("%5.1f ",ctl[i][j]);
			printf("\n");
		}
		printf("CTR: # of ways to put chars [i,n) in slots [j,m)\n");
		for(j = 0;j <= m;j++) {
			for(i = 0;i <= n;i++) printf("%5.1f ",ctr[i][j]);
			printf("\n");
		}
	}
	memset(sd->ct,0,sizeof(sd->ct));
	memset(sd->ctd,0,sizeof(sd->ctd));
	for(i = 0;i< n;i++) for(j = 0;j<m;j++) sd->ct[i][j] = ctl[i][j]*ctr[i+1][j+1];
	for(j = 0;j<m;j++) if (bm[j]&ABM_DASH) for(i = 0;i <= n;i++) sd->ctd[j]  += ctl[i][j]*ctr[i][j+1]; // do the spreading character as a special case
	DEB16 {
		printf("        " ); for(i = 0;i<n;i++) printf("  %c   ",l->s[i]); printf("      -\n");
		for(j = 0;j<m;j++) {printf("  e%2d:",j); for(i = 0;i<n;i++) printf(" %5.1f",sd->ct[i][j]); printf("     %5.1f",sd->ctd[j]); printf("\n");}
	}
	memset(sd->flbm,0,sizeof(sd->flbm));
	for(i = 0;i<n;i++) for(j = 0;j<m;j++) if (sd->ct[i][j]) sd->flbm[j] |= chartoabm[(int)l->s[i]];
	for(j = 0;j<m;j++) if (sd->ctd[j])   sd->flbm[j] |= ABM_DASH;
}

// Test to see if a spread string can fit in a given word. Writes deductions to sdata.
static int checksword(struct word*w,int j) {
	ABM bm[MXFL];
	struct light*l;
	struct sdata*sd;
	int k,m; //,n;

	l = lts+w->flist[j];
	sd = w->sdata+j;
	m = w->nent;
	//  n = w->wlen;
	for(k = 0;k<m;k++) bm[k] = w->e[k]->flbm;
	DEB16 {
		printf("checksword(w = %ld,\"%s\") bm = ",(long int)(w-words),l->s);
		for(k = 0;k<m;k++) printf("%016llx ",bm[k]); printf("\n");
	}
	scounts(w,j,bm);
	DEB16 {
		printf("  output bm = ");
		for(k = 0;k<m;k++) printf("%016llx ",sd->flbm[k]); printf("\n");
	}
	return 1;
}

// intersect light list q length l with letter position wp masked by bitmap m: result is stored in p and new length is returned
static int listisect(int*p,int*q,int l,int wp,ABM m) {int i,j;
	for(i = 0,j = 0;i<l;i++) if (m&(chartoabm[(int)(lts[q[i]].s[wp])])) p[j++] = q[i];
	//printf("listisect l(wp = %d m = %16llx) %d->%d\n",wp,m,l,j);
	return j;
}

// find the entry to expand next, or -1 if all done
static int findcritent(void) {int i,j,m;double k,l;
	m = -1;
	for(i = 0;i<ne;i++) {
		if (fillmode == 2&&entries[i].sel == 0) continue; // filling selection only: only check relevant entries
		if (onebit(entries[i].flbm)) continue;
		if (entries[i].checking>m) m = entries[i].checking; // find highest checking level // find highest checking level
	}
	j = -1;
	for(;m>0;m--) { // m >= 2: loop over checked entries; then m = 1: loop over unchecked entries
		j = -1;l = DBL_MAX;
		for(i = 0;i<ne;i++) {
			if (fillmode == 2&&entries[i].sel == 0) continue; // filling selection only: only check relevant entries
			if (!onebit(entries[i].flbm)&&entries[i].checking >= m) { // not already fixed?
				k = entries[i].crux; // get the priority for this entry
				if (k<l) l = k,j = i;}
		}
		if (j !=  -1) return j; // return entry with highest priority if one found
	}
	return j; // return -1 if no entries left to expand
}

/*
 * Check updated entries and rebuild feasible word lists
 * returns -3 for aborted, -2 for infeasible, -1 for out of memory, 0 if no feasible word lists affected,  >= 1 otherwise
 */
static int settleents(void)
{
	struct entry *e;
	struct word *w;
	struct sdata *sd;
	int f, i, j, k, l, m, jmode;
	int *p;
	bool aed;

	f = 0;

	for (j = 0; j < nw; j++) {
		w = &words[j];
		if (w->lp->emask & EM_SPR)
			jmode = 2;
		else
			jmode = 0;
		//    printf("j = %d jmode = %d emask = %d\n",j,jmode,w->lp->emask);
		m = w->nent;
		for (k = 0; k < m; k++)
			if (w->e[k]->upd)
				break;
		if (k == m)
			continue;	// no update flags set on any entries for this word
		for (k = 0; k < m; k++)
			if (!onebit(w->e[k]->flbm))
				break;
		aed = (k == m);	// all entries determined?
		p = w->flist;
		l = w->flistlen;
		sd = w->sdata;
		if (sflistlen[sdep][j] == -1) {	// then we mustn't trash words[].flist
			sflist[sdep][j] = p;
			sflistlen[sdep][j] = l;
			ssdata[sdep][j] = sd;
			w->sdata = 0;
			w->flist = (int *)malloc(l * sizeof(int));	// new list can be at most as long as old one
			if (!w->flist)
				return -1;	// out of memory
			if (jmode == 2) {
				w->sdata =
				    (struct sdata *)malloc(l *
							   sizeof(struct
								  sdata));
				if (!w->sdata)
					return -1;	// out of memory
			}
		}
		if (afunique) {	// the following test makes things quite a lot slower: consider optimising by keeping track of when an update might be needed
			for (i = 0, k = 0; i < l; i++)
				if (!isused(p[i]))
					w->flist[k++] = p[i];
			p = w->flist;
			l = k;
		}

		if (jmode == 0) {	// normal case
			for (k = 0; k < m; k++) {	// think about moving this loop inside listisect()
				e = w->e[k];
				if (!e->upd)
					continue;
				l = listisect(w->flist, p, l, k, e->flbm);	// generate new feasible word list
				p = w->flist;
				if (l == 0)
					break;
			}
		} else {	// spread case
			for (i = 0, k = 0; i < l; i++) {
				w->flist[k] = p[i];
				if (checksword(w, k))
					k++;
			}
			l = k;
			w->upd = 1;
			f++;	// need to do settlents() anyway in this case
		}

		if (l != w->flistlen) {
			w->upd = 1;
			f++;	// word list has changed: feasible letter lists will need updating
			if (l) {
				p = realloc(w->flist, l * sizeof(int));
				if (p)
					w->flist = p;
				if (w->sdata) {
					sd = realloc(w->sdata,
						     l * sizeof(struct sdata));
					if (sd)
						w->sdata = sd;
				}
			}
		}
		w->flistlen = l;
		if (l == 0 && !w->fe)
			return -2;	// no options left and was not fully entered by user
		if (!aed)
			continue;	// not all entries determined yet, so don't commit
		w->flistlen = l;
		if (l == 0 && !w->fe)
			return -2;	// no options left and was not fully entered by user
		assert(w->commitdep == -1);
		for (k = 0; k < l; k++)
			setused(w->flist[k], 1);	// flag as used (can be more than one in jumble case)
		w->commitdep = sdep;
	}

	for (i = 0; i < ne; i++)
		entries[i].upd = 0;	// all entry update effects now propagated into word updates
	//  DEB1 printf("settleents returns %d\n",f);fflush(stdout);
	return f;
}

// check updated word lists, rebuild feasible entry lists
// returns -3 for aborted, 0 if no feasible letter lists affected, >0 otherwise
static int settlewds(void)
{
	int f, i, j, k, l, m, jmode;
	int *p;
	struct entry *e;
	struct word *w;
	ABM entfl[MXFL];
	//  DEB1 printf("settlewds()\n");
	f = 0;
	for (i = 0; i < nw; i++) {
		w = words + i;
		if (!w->upd)
			continue;	// loop over updated word lists
		if (w->fe)
			continue;
		if (w->lp->emask & EM_SPR)
			jmode = 2;
		else
			jmode = 0;
		m = w->nent;
		p = w->flist;
		l = w->flistlen;

		for (k = 0; k < m; k++)
			entfl[k] = 0;
		if (jmode == 0)
			for (j = 0; j < l; j++)
				for (k = 0; k < m; k++)
					entfl[k] |= chartoabm[(int)lts[p[j]].s[k]];	// find all feasible letters from word list
		else if (jmode == 2)
			for (j = 0; j < l; j++)
				for (k = 0; k < m; k++)
					entfl[k] |= w->sdata[j].flbm[k];	// main work has been done in settleents()

		DEB16 {
			printf("w = %d entfl: ", i);
			for (k = 0; k < m; k++)
				printf(" %016llx", entfl[k]);
			printf("\n");
		}
		for (j = 0; j < m; j++) {
			e = w->e[j];	// propagate from word to entry
			if (e->flbm & ~entfl[j]) {	// has this entry been changed by the additional constraint?
				e->flbm &= entfl[j];
				e->upd = 1;
				f++;	// flag that it will need updating
				//      printf("E%d %16llx\n",k,entries[k].flbm);fflush(stdout);
			}
		}
	}
	for (i = 0; i < nw; i++)
		words[i].upd = 0;	// all word list updates processed
	//  DEB1 printf("settlewds returns %d\n",f);fflush(stdout);
	return f;
}

// calculate scores for spread entry
static void sscores(struct word*w,int wn,double(*sc)[NL]) {
	struct sdata*sd;
	struct light*l;
	int c,i,j,k,m,n;

	l = lts+w->flist[wn];
	m = w->nent;
	n = w->wlen;
	sd = w->sdata+wn;
	memset(sc,0,m*NL*sizeof(double));
	for(i = 0;i<n;i++) {
		c = chartol[(int)l->s[i]];
		for(j = 0;j<m;j++) sc[j][c] += sd->ct[i][j];
	}
	// now do the spreading character as a special case: i = #chars to left of candidate '-'
	for(j = 0;j<m;j++) sc[j][NL-1] += sd->ctd[j];
	DEB16 for(k = 0;k<m;k++) {printf("  e%2d:",k); for(i = 0;i<NL;i++) {printf(" %5.1f",sc[k][i]); if (i == 25||i == 35) printf("  ");} printf("\n"); }
}


// calculate per-entry scores
// returns -3 if aborted
static int mkscores(void) {
	int c,i,j,k,l,m,jmode;
	int*p;
	double f;
	struct word*w;
	// following static to reduce stack use
	static double sc[MXFL][NL],tsc[MXFL][NL]; // weighted count of number of words that put a given letter in a given place

	for(i = 0;i<ne;i++) for(j = 0;j<NL;j++) entries[i].score[j] = 1.0;
	for(i = 0;i<nw;i++) {
		w = words+i;
		if (w->fe) continue;
		if (w->lp->emask&EM_SPR) jmode = 2;
		else                         jmode = 0;
		m = w->nent;
		p = w->flist;
		l = w->flistlen;
		for(k = 0;k<m;k++) for(j = 0;j<NL;j++) sc[k][j] = 0.0;

		if (jmode == 0) { // normal case
			if (afunique&&w->commitdep >= 0) {  // avoid zero score if we've committed
				if (l == 1) for(k = 0;k<m;k++) sc[k][chartol[(int)lts[p[0]].s[k]]] += 1.0;
			}
			else {
				for(j = 0;j<l;j++) if (!(afunique&&isused(p[j]))) { // for each remaining feasible word
					if (lts[p[j]].ans<0) f = 1;
					else f = (double)ansp[lts[p[j]].ans]->score;
					for(k = 0;k<m;k++) sc[k][chartol[(int)lts[p[j]].s[k]]] += f; // add in its score to this cell's score
				}
			}
		} else { // spread case
			for(j = 0;j<l;j++) {
				sscores(w,j,tsc);
				for(k = 0;k<m;k++) for(c = 0;c<NL;c++) sc[k][c] += tsc[k][c];
			}
		}

		for(k = 0;k<m;k++) for(j = 0;j<NL;j++) w->e[k]->score[j] *= sc[k][j];
	}
	for(i = 0;i<ne;i++) {
		f = -DBL_MAX; for(j = 0;j<NL;j++) f = MX(f,entries[i].score[j]);
		entries[i].crux = f; // crux at an entry is the greatest score over all possible letters
	}
	return 0;
}


// sort possible letters into order of decreasing favour with randomness r; write results to s
void getposs(struct entry*e,char*s,int r,int dash) {int i,l,m,n,nl;double j,k;
	//  DEB2 printf("getposs(%d)\n",(int)(e-entries));
	nl = dash?NL:NL-1; // avoid outputting dashes?
	l = 0;
	k = -DBL_MAX; for(i = 0;i<nl;i++) if (e->score[i]>k) k = e->score[i]; // find highest score
	k *= 2;
	for(;;) {
		for(i = 0,j = -DBL_MAX;i<nl;i++) if (e->score[i]>j&&e->score[i]<k) j = e->score[i]; // peel off scores from top down
		//    DEB2 printf("getposs(%d): j = %g\n",(int)(e-entries),j);
		if (j <= 0) break;
		for(i = 0;i<nl;i++) if (e->score[i] == j) s[l++] = ltochar[i]; // add to output string
		k = j;} // get next highest set of equal scores
	s[l] = '\0';
	if (r == 0) return;
	for(i = 0;i<l;i++) { // randomise if necessary
		m = i+rand()%(r*2+1); // candidate for swap: distance depends on randomisation level
		if (m >= 0&&m<l) n = s[i],s[i] = s[m],s[m] = n; // swap candidates
	}
}

// indent according to stack depth
static void sdepsp(void) {int i; if (sdep<0) printf("<%d",sdep); for(i = 0;i<sdep;i++) printf(" ");}

static void freestack() {int i;
	for(i = 0;i <= ne;i++) {
		if (sposs     ) FREEX(sposs     [i]);
		if (sflist    ) FREEX(sflist    [i]);
		if (ssdata    ) FREEX(ssdata    [i]);
		if (sflistlen ) FREEX(sflistlen [i]);
		if (sentryfl  ) FREEX(sentryfl  [i]);
	}
	FREEX(sposs);
	FREEX(spossp);
	FREEX(sflist);
	FREEX(ssdata);
	FREEX(sflistlen);
	FREEX(sentryfl);
	FREEX(sentry);
}

static int allocstack() {int i;
	freestack();
	if (!(sposs     =calloc(ne+1,sizeof(char*         )))) return 1;
	if (!(spossp    =calloc(ne+1,sizeof(int           )))) return 1;
	if (!(sflist    =calloc(ne+1,sizeof(int**         )))) return 1;
	if (!(ssdata    =calloc(ne+1,sizeof(struct sdata**)))) return 1;
	if (!(sflistlen =calloc(ne+1,sizeof(int*          )))) return 1;
	if (!(sentryfl  =calloc(ne+1,sizeof(ABM*          )))) return 1;
	if (!(sentry    =calloc(ne+1,sizeof(int           )))) return 1;
	for(i = 0;i <= ne;i++) { // for each stack depth that can be reached
		if (!(sposs     [i] = malloc(NL+1                    ))) return 1;
		if (!(sflist    [i] = malloc(nw*sizeof(int*         )))) return 1;
		if (!(ssdata    [i] = malloc(nw*sizeof(struct sdata*)))) return 1;
		if (!(sflistlen [i] = malloc(nw*sizeof(int          )))) return 1;
		if (!(sentryfl  [i] = malloc(ne*sizeof(ABM          )))) return 1;
	}
	return 0;
}

// initialise state stacks
static void state_init(void) {
	sdep = -1;
	filler_status = 0;
}

// push stack
static void state_push(void) {int i;
	sdep++;
	assert(sdep <= ne);
	for(i = 0;i<nw;i++) sflistlen[sdep][i] = -1;  // flag that flists need allocating
	for(i = 0;i<ne;i++) sentryfl[sdep][i] = entries[i].flbm; // feasible letter lists
}

// undo effect of last deepening operation
static void state_restore(void) {int i,j,l; struct word*w;
	for(i = 0;i<nw;i++) {
		w = words+i;
		if (w->commitdep >= sdep) { // word to uncommit?
			l = w->flistlen;
			DEB16 {
				printf("sdep = %d flistlen = %d uncommitting word %d commitdep = %d:",sdep,w->flistlen,i,w->commitdep);
				for(j = 0;j<l;j++) printf(" %s",lts[w->flist[j]].s);
				printf("\n");
			}
			for(j = 0;j<l;j++) setused(w->flist[j],0);
			w->commitdep = -1;
		}
		if (sflistlen[sdep][i] !=  -1&&w->flist !=  0) { // word feasible list to free?
			free(w->flist);
			w->flist = sflist[sdep][i];
			w->flistlen = sflistlen[sdep][i];
			if (w->sdata) {
				free(w->sdata);
				w->sdata = ssdata[sdep][i];
			}
		}
	}
	for(i = 0;i<ne;i++) entries[i].flbm = sentryfl[sdep][i];
}

// pop stack
static void state_pop(void) {
	assert(sdep >= 0);
	state_restore();
	sdep--;
}

// clear state stacks and free allocated memory
static void state_finit(void) {
	while(sdep >= 0)
		state_pop();
	freestack();
}

// build initial feasible lists, calling plug-in as necessary
static int buildlists(void) {int u,i,j;
	for(i = 0;i<nw;i++) {
		FREEX(words[i].flist);
		FREEX(words[i].sdata);
		lightx = words[i].gx0;
		lighty = words[i].gy0;
		lightdir = words[i].ldir;
		for(j = 0;j<words[i].nent;j++) {
			gridorderindex[j] = words[i].goi[j];
			checking[j] = words[i].e[j]->checking;
		}
		u = getinitflist(&words[i].flist,&words[i].flistlen,words[i].lp,words[i].wlen);
		if (u) {filler_status = -3;return 0;}
		if (words[i].lp->ten) clueorderindex++;
		if (initsdata(i)) {filler_status = -3;return 0;}
	}
	if (postgetinitflist()) {filler_status = -4;return 1;}
	FREEX(aused);
	FREEX(lused);
	aused = (unsigned char*)calloc(atotal+NMSG,sizeof(unsigned char)); // enough for "msgword" answers too
	if (aused == NULL) {filler_status = -3;return 0;}
	lused = (unsigned char*)calloc(ultotal,sizeof(unsigned char));
	if (lused == NULL) {filler_status = -3;return 0;}
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
	if (settlewds() == -3) {DEB1 printf("aborting...\n"); return -5;};
resettle: // "unit propagation"
	do {
		f = settleents(); // rescan entries
		if (f == -3) {DEB1 printf("aborting...\n"); return -5;}
		if (f == 0) break;
		if (f == -1) return -1; // out of memory: abort
		if (f == -2) goto backtrack; // proved impossible
		f = settlewds(); // rescan words
		if (f == -3) {DEB1 printf("aborting...\n"); return -5;}
	} while(f); // need to iterate until everything settles down
	f = mkscores();
	if (f == -3) {DEB1 printf("aborting...\n"); return -5;}
	if (fillmode == 0||fillmode == 3) return 2; // only doing BG/preexport fill? stop after first settle
	DEB16 pstate(1);

	// go one level deeper in search tree
	DEB1 { int w; for(w = 0;w<nw;w++) printf("[w%d: %d]",w,words[w].flistlen); printf("\n"); }
	e = findcritent(); // find the most critical entry, over whose possible letters we will iterate
	if (e == -1) return 2; // all done, result found
	getposs(entries+e,sposs[sdep],afrandom,1); // find feasible letter list in descending order of score
	DEB1{printf("D%3d ",sdep);sdepsp();printf("E%d %s\n",e,sposs[sdep]);fflush(stdout);}
	sentry[sdep] = e;
	spossp[sdep] = 0; // start on most likely possibility

	// try one possibility at the current critical entry
nextposs:
	e = sentry[sdep];
	if (sposs[sdep][spossp[sdep]] == '\0') goto backtrack; // none left: backtrack
	c = sposs[sdep][spossp[sdep]++]; // get letter to try
	DEB1 {  printf("D%3d ",sdep);sdepsp();printf(":%c:\n",c);fflush(stdout); }
	if (sdep == ne) return -2; // out of stack space (should never happen)
	state_push();
	entries[e].upd = 1;
	entries[e].flbm = chartoabm[(int)c]; // fix feasible list
	ct1 = clock();
	if (ct1-ct0>CLOCKS_PER_SEC*3 || ct1-ct0<0) {
		update_grid();
		ct0 = clock();
	} // update display every three seconds or so
	goto resettle; // update internal data from new entry

backtrack:
	state_pop();
	if (sdep !=  -1) goto nextposs;
	return 1; // all done, no solution found
}

static void searchdone() {
	int i;
	DEB1 printf("searchdone: A\n");
	gdk_threads_enter();
	DEB1 printf("searchdone: B\n");
	if (filler_status == 2) {
		mkfeas(); // construct feasible word list
		DEB1 pstate(1);
	}
	else {
		for(i = 0;i<ne;i++) entries[i].flbm = 0; // clear feasible letter bitmaps
		llistp = NULL;llistn = 0; // no feasible word list
		DEB1 printf("BG fill failed\n"),fflush(stdout);
	}
	// updatefeas();
	update_grid();
	gdk_threads_leave();
	DEB1 printf("searchdone: C\n");
	state_finit();
	// if (fillmode&&currentdia) gtk_dialog_response(GTK_DIALOG(currentdia),GTK_RESPONSE_CANCEL);
	for(i = 0;i<nw;i++) {
		if (words[i].commitdep >= 0) printf("assertion failing i = %d nw = %d words[i].commitdep = %d\n",i,nw,words[i].commitdep);
		assert(words[i].commitdep == -1); // ... and uncommitted
	}
	DEB1 printf("search done\n");
	// j = 0; for(i = 0;i<ltotal;i++) j += isused[i]; printf("total lused = %d\n",j);fflush(stdout);
	return;
}

int filler_search()
{
	int i;
	clock_t ct;

	ct = ct0 = clock();
	clueorderindex = 0;
	if (buildlists())
		goto ex0;

	DEB1 pstate(1);
	for(i = 0;i<ne;i++)
		entries[i].upd = 1;
	for(i = 0;i<nw;i++)
		words[i].upd = 1;

	filler_status = search();
	if (fillmode != 3)
		searchdone(); // tidy up unless in pre-export mode

	DEB1 printf("search finished: %.3fs\n",(double)(clock()-ct)/CLOCKS_PER_SEC);

ex0:
	DEB1 printf("fillerthread() terminating filler_status = %d\n",filler_status);
	return 0;
}


int filler_init(int mode)
{
	int i;

	DEB1 printf("filler_start(%d)\n",mode);
	DEB1 pstate(0);

	fillmode = mode;
	if (allocstack())
		return 1;
	if (pregetinitflist())
		return 1;

	state_init();

	for (i = 0; i < nw; i++)
		words[i].commitdep = -1; // flag word uncommitted

	state_push();
	filler_status = 3;
	return 0;
}

int filler_destroy()
{
	state_finit();
	return 0;
}
