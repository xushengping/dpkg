/*
 * dselect - Debian GNU/Linux package maintenance user interface
 * pkglist.cc - package list administration
 *
 * Copyright (C) 1995 Ian Jackson <iwj10@cus.cam.ac.uk>
 *
 * This is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2,
 * or (at your option) any later version.
 *
 * This is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <stdio.h>
#include <string.h>
#include <ncurses.h>
#include <assert.h>
#include <signal.h>

extern "C" {
#include "config.h"
#include "dpkg.h"
#include "dpkg-db.h"
}
#include "dselect.h"
#include "bindings.h"

int packagelist::compareentries(struct perpackagestate *a,
                                struct perpackagestate *b) {
  const char *asection= a->pkg->section;
  if (!asection && a->pkg->name) asection= "";
  const char *bsection= b->pkg->section;
  if (!bsection && b->pkg->name) bsection= "";
  int c_section=
    !asection || !bsection ?
      (!bsection) - (!asection) :
    !*asection || !*bsection ?
      (!*asection) - (!*bsection) :
    strcasecmp(asection,bsection);
  int c_priority=
    a->pkg->priority - b->pkg->priority;
  if (!c_priority && a->pkg->priority == pkginfo::pri_other)
    c_priority= strcasecmp(a->pkg->otherpriority, b->pkg->otherpriority);
  int c_name=
    a->pkg->name && b->pkg->name ?
      strcasecmp(a->pkg->name, b->pkg->name) :
    (!b->pkg->name) - (!a->pkg->name);

  switch (sortorder) {
  case so_section:
    return c_section ? c_section : c_priority ? c_priority : c_name;
  case so_priority:
    return c_priority ? c_priority : c_section ? c_section : c_name;
  case so_alpha:
    return c_name;
  case so_unsorted:
  default:
    internerr("unsorted or unknown in compareentries");
  }
}

void packagelist::discardheadings() {
  int a,b;
  for (a=0, b=0; a<nitems; a++) {
    if (table[a]->pkg->name) {
      table[b++]= table[a];
    }
  }
  nitems= b;

  struct perpackagestate *head, *next;
  head= headings;
  while (head) {
    next= head->uprec;
    delete head->pkg;
    delete head;
    head= next;
  }
  headings= 0;
}

void packagelist::addheading(pkginfo::pkgpriority priority,
                             const char *otherpriority,
                             const char *section) {
  assert(nitems <= nallocated);
  if (nitems == nallocated) {
    nallocated += nallocated+50;
    struct perpackagestate **newtable= new struct perpackagestate*[nallocated];
    memcpy(newtable,table,nallocated*sizeof(struct perpackagestate*));
    delete[] table;
    table= newtable;
  }
  
  if (debug) fprintf(debug,"packagelist[%p]::addheading(%d,%s,%s)\n",
                     this,priority,
                     otherpriority ? otherpriority : "<null>",
                     section ? section : "<null>");

  struct pkginfo *newhead= new pkginfo;
  newhead->name= 0;
  newhead->priority= priority;
  newhead->otherpriority= (char*)otherpriority;
  newhead->section= (char*)section;

  struct perpackagestate *newstate= new perpackagestate;
  newstate->pkg= newhead;
  newstate->uprec= headings;
  headings= newstate;
 
  table[nitems++]= newstate;
}

static packagelist *sortpackagelist;

int qsort_compareentries(const void *a, const void *b) {
  return sortpackagelist->compareentries(*(perpackagestate**)a,
                                         *(perpackagestate**)b);
}

void packagelist::sortinplace() {
  sortpackagelist= this;

  if (debug) fprintf(debug,"packagelist[%p]::sortinplace()\n",this);
  qsort(table,nitems,sizeof(struct pkginfoperfile*),qsort_compareentries);
}

void packagelist::sortmakeheads() {
  discardheadings();
  sortinplace();
  assert(nitems);

  if (debug) fprintf(debug,"packagelist[%p]::sortmakeheads() sortorder=%d\n",
                     this,sortorder);
  
  int nrealitems= nitems;
  addheading(pkginfo::pri_unset,0,0);

  if (sortorder == so_alpha) { sortinplace(); return; }
  
  assert(sortorder == so_section || sortorder == so_priority);

  // Important: do not save pointers into table in this function, because
  // addheading may need to reallocate table to make it larger !
  
  struct pkginfo *lastpkg;
  struct pkginfo *thispkg;
  lastpkg= 0;
  int a;
  for (a=0; a<nrealitems; a++) {
    thispkg= table[a]->pkg;
    assert(thispkg->name);
    int prioritydiff= (!lastpkg ||
                       thispkg->priority != lastpkg->priority ||
                       (thispkg->priority == pkginfo::pri_other &&
                        strcasecmp(thispkg->otherpriority,lastpkg->otherpriority)));
    int sectiondiff= (!lastpkg ||
                      strcasecmp(thispkg->section ? thispkg->section : "",
                                 lastpkg->section ? lastpkg->section : ""));

    if (debug) fprintf(debug,"packagelist[%p]::sortmakeheads()"
                       " pkg=%s  priority=%d otherpriority=%s %s  section=%s %s\n",
                       this, thispkg->name, thispkg->priority,
                       thispkg->otherpriority ? thispkg->otherpriority : "<null>",
                       prioritydiff ? "*diff*" : "same",
                       thispkg->section ? thispkg->section : "<null>",
                       sectiondiff ? "*diff*" : "same");

    if (sortorder == so_section && sectiondiff)
      addheading(pkginfo::pri_unset,0, thispkg->section ? thispkg->section : "");
    if (sortorder == so_priority && prioritydiff)
      addheading(thispkg->priority,thispkg->otherpriority, 0);
    if (prioritydiff || sectiondiff) 
      addheading(thispkg->priority,thispkg->otherpriority,
                 thispkg->section ? thispkg->section : "");
    lastpkg= thispkg;
  }

  if (listpad) {
    int maxx, maxy;
    getmaxyx(listpad,maxx,maxy);
    if (nitems > maxy) {
      delwin(listpad);
      listpad= newpad(nitems+1, total_width);
      if (!listpad) ohshite("failed to create larger baselist pad");
    } else if (nitems < maxy) {
      werase(listpad);
    }
  }
  
  sortinplace();
}

void packagelist::initialsetup() {
  if (debug)
    fprintf(debug,"packagelist[%p]::initialsetup()\n",this);

  int allpackages= countpackages();
  datatable= new struct perpackagestate[allpackages];

  nallocated= allpackages+150; // will realloc if necessary, so 150 not critical
  table= new struct perpackagestate*[nallocated];

  depsdone= 0;
  unavdone= 0;
  currentinfo= 0;
  headings= 0;
  verbose= 0;
}

void packagelist::finalsetup() {
  setcursor(0);

  if (debug)
    fprintf(debug,"packagelist[%p]::finalsetup done; recursive=%d nitems=%d\n",
            this, recursive, nitems);
}

packagelist::packagelist(keybindings *kb) : baselist(kb) {
  // nonrecursive
  initialsetup();
  struct pkgiterator *iter;
  struct pkginfo *pkg;
  
  for (iter=iterpkgstart(), nitems=0;
       (pkg=iterpkgnext(iter));
       ) {
    struct perpackagestate *state= &datatable[nitems];
    state->pkg= pkg;
    if (pkg->status == pkginfo::stat_notinstalled &&
        !pkg->files &&
        pkg->want != pkginfo::want_install) {
      pkg->clientdata= 0; continue;
    }
    if (!pkg->available.valid) blankpackageperfile(&pkg->available);
    state->direct= state->original= pkg->want;
    if (readwrite && pkg->want == pkginfo::want_unknown) {
      state->suggested= pkg->priority <= pkginfo::pri_standard
        ? pkginfo::want_install : pkginfo::want_purge; /* fixme: configurable */
      state->spriority= sp_inherit;
    } else {
      state->suggested= pkg->want;
      state->spriority= sp_fixed;
    }
    state->dpriority= dp_must;
    state->selected= state->suggested;
    state->uprec= 0;
    state->relations.init();
    pkg->clientdata= state;
    table[nitems]= state;
    nitems++;
  }
  if (!nitems) ohshit("There are no packages to select.");
  recursive= 0;
  sortorder= so_priority;
  sortmakeheads();
  finalsetup();
}

packagelist::packagelist(keybindings *kb, pkginfo **pkgltab) : baselist(kb) {
  // takes over responsibility for pkgltab (recursive)
  initialsetup();
  
  recursive= 1;
  nitems= 0;
  if (pkgltab) {
    add(pkgltab);
    delete[] pkgltab;
  }
    
  sortorder= so_unsorted;
  finalsetup();
}

void perpackagestate::free(int recursive) {
  if (pkg->name) {
    if (readwrite) {
      if (uprec) {
        assert(recursive);
        uprec->selected= selected;
        pkg->clientdata= uprec;
      } else {
        assert(!recursive);
        if (pkg->want != selected) {
          pkg->want= selected;
        }
        pkg->clientdata= 0;
      }
    }
    relations.free();
  }
}

packagelist::~packagelist() {
  if (debug) fprintf(debug,"packagelist[%p]::~packagelist()\n",this);

  discardheadings();
  
  int index;
  for (index=0; index<nitems; index++) table[index]->free(recursive);
  delete[] table;
  delete[] datatable;
  if (debug) fprintf(debug,"packagelist[%p]::~packagelist() tables freed\n",this);
  
  doneent *search, *next;
  for (search=depsdone; search; search=next) {
    next= search->next;
    delete search;
  }
  
  if (debug) fprintf(debug,"packagelist[%p]::~packagelist() done\n",this);
}

pkginfo **packagelist::display() {
  // returns list of packages as null-terminated array, which becomes owned
  // by the caller, if a recursive check is desired.
  // returns 0 if no recursive check is desired.
  int response, index;
  const keybindings::interpretation *interp;
  pkginfo **retl;

  if (debug) fprintf(debug,"packagelist[%p]::display()\n",this);

  setupsigwinch();
  startdisplay();
  displayhelp(helpmenulist(),'i');

  if (debug) fprintf(debug,"packagelist[%p]::display() entering loop\n",this);
  for (;;) {
    if (whatinfo_height) wcursyncup(whatinfowin);
    if (doupdate() == ERR) ohshite("doupdate failed");
    signallist= this;
    if (sigprocmask(SIG_UNBLOCK,&sigwinchset,0)) ohshite("failed to unblock SIGWINCH");
    response= getch();
    if (sigprocmask(SIG_BLOCK,&sigwinchset,0)) ohshite("failed to re-block SIGWINCH");
    if (response == ERR) ohshite("getch failed");
    interp= (*bindings)(response);
    if (debug)
      fprintf(debug,"packagelist[%p]::display() response=%d interp=%s\n",
              this,response, interp ? interp->action : "[none]");
    if (!interp) { beep(); continue; }
    (this->*(interp->pfn))();
    if (interp->qa != qa_noquit) break;
  }
  pop_cleanup(ehflag_normaltidy); // unset the SIGWINCH handler
  enddisplay();
  
  if (interp->qa == qa_quitnochecksave || !readwrite) {
    if (debug) fprintf(debug,"packagelist[%p]::display() done - quitNOcheck\n",this);
    return 0;
  }
  
  if (recursive) {
    retl= new pkginfo*[nitems+1];
    for (index=0; index<nitems; index++) retl[index]= table[index]->pkg;
    retl[nitems]= 0;
    if (debug) fprintf(debug,"packagelist[%p]::display() done, retl=%p\n",this,retl);
    return retl;
  } else {
    packagelist *sub= new packagelist(bindings,0);
    for (index=0; index < nitems; index++)
      if (table[index]->pkg->name)
        sub->add(table[index]->pkg);
    repeatedlydisplay(sub,dp_must);
    if (debug)
      fprintf(debug,"packagelist[%p]::display() done, not recursive no retl\n",this);
    return 0;
  }
}
