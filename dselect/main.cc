/*
 * dselect - Debian package maintenance user interface
 * main.cc - main program
 *
 * Copyright (C) 1994,1995 Ian Jackson <iwj10@cus.cam.ac.uk>
 * Copyright (C) 2000,2001 Wichert Akkerman <wakkerma@debian.org>
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
 * License along with dpkg; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <unistd.h>
#include <dirent.h>
#include <limits.h>
#include <ctype.h>
#include <assert.h>

#include <term.h>

extern "C" {
#include <config.h>
#include <dpkg.h>
#include <dpkg-db.h>
#include <version.h>
#include <myopt.h>
}
#include "dselect.h"
#include "bindings.h"
#include "pkglist.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

const char thisname[]= DSELECT;
const char printforhelp[]= N_("Type dselect --help for help.");

modstatdb_rw readwrite;
const char *admindir= ADMINDIR;
FILE *debug;
int expertmode = 0;

static keybindings packagelistbindings(packagelist_kinterps,packagelist_korgbindings);

struct menuentry {
  const char *command;
  const char *key;
  const char *option;
  const char *menuent;
  urqfunction *fn;
};

static const menuentry menuentries[]= {
  { "access",	N_("a"),	N_("[A]ccess"),	N_("Choose the access method to use."),			&urq_setup   },
  { "update",	N_("u"),	N_("[U]pdate"),	N_("Update list of available packages, if possible."),	&urq_update  },
  { "select",	N_("s"),	N_("[S]elect"),	N_("Request which packages you want on your system."),	&urq_list    },
  { "install",	N_("i"),	N_("[I]nstall"),N_("Install and upgrade wanted packages."),		&urq_install },
  { "config",	N_("c"),	N_("[C]onfig"),	N_("Configure any packages that are unconfigured."),	&urq_config  },
  { "remove",	N_("r"),	N_("[R]emove"),	N_("Remove unwanted software."),			&urq_remove  },
  { "quit",	N_("q"),	N_("[Q]uit"),	N_("Quit dselect."),					&urq_quit    },
  { 0,		0,  		N_("menu"),	0,							&urq_menu    },
  { 0 }
};

static const char programdesc[]=
      N_("Debian `%s' package handling frontend.");

static const char copyrightstring[]= N_(
      "Version %s.\n"
      "Copyright (C) 1994-1996 Ian Jackson.\n"
      "Copyright (C) 2000,2001 Wichert Akkerman.\n"
      "This is free software; see the GNU General Public Licence version 2\n"
      "or later for copying conditions.  There is NO warranty.  See\n"
      "dselect --licence for details.\n");

static void printversion(void) {
  if (fprintf(stdout,gettext(programdesc),DSELECT) == EOF) werr("stdout");
  if (fprintf(stdout,"\n") == EOF) werr("stdout");
  if (fprintf(stdout,gettext(copyrightstring), DPKG_VERSION_ARCH) == EOF) werr("stdout");
}

static void usage(void) {
  if (!fputs(
     _("Usage: dselect [options]\n"
       "       dselect [options] action ...\n"
       "Options:  --admindir <directory>  (default is /var/lib/dpkg)\n"
       "          --help  --version  --licence  --expert  --debug <file> | -D<file>\n"
       "Actions:  access update select install config remove quit menu\n"),
       stdout)) werr("stdout");
}

/* These are called by C code, so need to have C calling convention */
extern "C" {

  static void helponly(const struct cmdinfo*, const char*) NONRETURNING;
  static void helponly(const struct cmdinfo*, const char*) {
    usage(); exit(0);
  }
  static void versiononly(const struct cmdinfo*, const char*) NONRETURNING;
  static void versiononly(const struct cmdinfo*, const char*) {
    printversion(); exit(0);
  }

  static void setdebug(const struct cmdinfo*, const char *v) {
    debug= fopen(v,"a");
    if (!debug) ohshite(_("couldn't open debug file `%.255s'\n"),v);
    setvbuf(debug,0,_IONBF,0);
  }

  static void setexpert(const struct cmdinfo*, const char *v) {
    expertmode = 1;
  }

} /* End of extern "C" */

static const struct cmdinfo cmdinfos[]= {
  { "admindir",   0,   1,  0,  &admindir,  0                      },
  { "debug",     'D',  1,  0,  0,          setdebug               },
  { "expert",    'E',  0,  0,  0,          setexpert              },
  { "help",      'h',  0,  0,  0,          helponly               },
  { "version",    0,   0,  0,  0,          versiononly            },
  { "licence",    0,   0,  0,  0,          showcopyright          }, /* UK spelling */
  { "license",    0,   0,  0,  0,          showcopyright          }, /* US spelling */
  {  0,           0,   0,  0,  0,          0                      }
};

static int cursesareon= 0;
void curseson() {
  if (!cursesareon) {
    const char *cup, *smso;
    initscr();
    cup= tigetstr("cup");
    smso= tigetstr("smso");
    if (!cup || !smso) {
      endwin();
      if (!cup)
        fputs(_("Terminal does not appear to support cursor addressing.\n"),stderr);
      if (!smso)
        fputs(_("Terminal does not appear to support highlighting.\n"),stderr);
      fputs(_("Set your TERM variable correctly, use a better terminal,\n"
            "or make do with the per-package management tool "),stderr);
      fputs(DPKG ".\n",stderr);
      ohshit(_("terminal lacks necessary features, giving up"));
    }
  }
  cursesareon= 1;
}

void cursesoff() {
  if (cursesareon) {
    clear();
    refresh();
    endwin();
  }
  cursesareon=0;
}

extern void *operator new(size_t size) {
  void *p;
  p= m_malloc(size);
  assert(p);
  return p;
}

extern void operator delete(void *p) {
  free(p);
}

urqresult urq_list(void) {
  readwrite= modstatdb_init(admindir,msdbrw_writeifposs);

  curseson();

  packagelist *l= new packagelist(&packagelistbindings);
  l->resolvesuggest();
  l->display();
  delete l;

  modstatdb_shutdown();
  resetpackages();
  return urqr_normal;
}

void dme(int i, int so) {
  char buf[120];
  const menuentry *me= &menuentries[i];
  sprintf(buf," %c %d. %-11.11s %-80.80s ",
          so ? '*' : ' ', i,
          gettext(me->option),
          gettext(me->menuent));
  
  int y,x;
  getmaxyx(stdscr,y,x);

  attrset(so ? A_REVERSE : A_NORMAL);
  mvaddnstr(i+2,0, buf,x-1);
  attrset(A_NORMAL);
}

int refreshmenu(void) {
  char buf[2048];
  static int l,lockfd;
  static char *lockfile;

  curseson(); cbreak(); noecho(); nonl(); keypad(stdscr,TRUE);

  int y,x;
  getmaxyx(stdscr,y,x);

  clear();
  attrset(A_BOLD);
  sprintf(buf,gettext(programdesc),DSELECT);
  mvaddnstr(0,0,buf,x-1);

  attrset(A_NORMAL);
  const struct menuentry *mep; int i;
  for (mep=menuentries, i=0; mep->option && mep->menuent; mep++, i++)
    dme(i,0);

  attrset(A_BOLD);
  addstr(_("\n\n"
         "Move around with ^P and ^N, cursor keys, initial letters, or digits;\n"
         "Press <enter> to confirm selection.   ^L redraws screen.\n\n"));

  attrset(A_NORMAL);
  sprintf(buf,gettext(copyrightstring),DPKG_VERSION_ARCH);
  addstr(buf);

  l = strlen(admindir);
  lockfile = new char[l+sizeof(LOCKFILE)+2];
  strcpy(lockfile,admindir);
  strcpy(lockfile+l, "/" LOCKFILE);
  lockfd = open(lockfile, O_RDWR|O_CREAT|O_TRUNC, 0660);
  if (errno == EACCES || errno == EPERM)
    addstr(_("\n\n"
             "Read-only access: only preview of selections is available!"));

  return i;
}

urqresult urq_menu(void) {
#define C(x) ((x)-'a'+1)
  int entries, c, i;
  entries= refreshmenu();
  int cursor=0;
  dme(0,1);
  for (;;) {
    refresh();
    do
      c= getch();
    while (c == ERR && errno == EINTR);
    if (c==ERR)  {
      if(errno != 0)
        ohshite(_("failed to getch in main menu"));
      else {
        clearok(stdscr,TRUE); clear(); refreshmenu(); dme(cursor,1); 
      }
    }

    if (c==C('n') || c==KEY_DOWN || c==' ' || c=='j') {
      dme(cursor,0); cursor++; cursor %= entries; dme(cursor,1);
    } else if (c==C('p') || c==KEY_UP || c==C('h') ||
               c==KEY_BACKSPACE || c==KEY_DC || c=='k') {
      dme(cursor,0); cursor+= entries-1; cursor %= entries; dme(cursor,1);
    } else if (c=='\n' || c=='\r' || c==KEY_ENTER) {
      clear(); refresh();
      switch (menuentries[cursor].fn()) { /* fixme: trap errors in urq_... */
      case urqr_quitmenu:
        return urqr_quitmenu;
      case urqr_normal:
        cursor++; cursor %= entries;
      case urqr_fail:
        break;
      default:
        internerr("unknown menufn");
      }
      refreshmenu(); dme(cursor,1);
    } else if (c==C('l')) {
      clearok(stdscr,TRUE); clear(); refreshmenu(); dme(cursor,1);
    } else if (isdigit(c)) {
      char buf[2]; buf[0]=c; buf[1]=0; c=atoi(buf);
      if (c < entries) {
        dme(cursor,0); cursor=c; dme(cursor,1);
      } else {
        beep();
      }
    } else if (isalpha(c)) {
      c= tolower(c);
      for (i=0; i<entries && gettext(menuentries[i].key)[0] != c; i++);
      if (i < entries) {
        dme(cursor,0); cursor=i; dme(cursor,1);
      } else {
        beep();
      }
    } else {
      beep();
    }
  }
}

urqresult urq_quit(void) {
  return urqr_quitmenu;
  /* fixme: check packages OK */
}

int main(int, const char *const *argv) {
  jmp_buf ejbuf;

  setlocale(LC_ALL, "");
  bindtextdomain(PACKAGE, LOCALEDIR);
  textdomain(PACKAGE);

  if (setjmp(ejbuf)) { /* expect warning about possible clobbering of argv */
    cursesoff();
    error_unwind(ehflag_bombout); exit(2);
  }
  push_error_handler(&ejbuf,print_error_fatal,0);

  myfileopt(CONFIGDIR "/" DSELECT ".cfg", cmdinfos);
  myopt(&argv,cmdinfos);

  if (*argv) {
    const char *a;
    while ((a= *argv++) != 0) {
      const menuentry *me;
      for (me= menuentries; me->command && strcmp(me->command,a); me++);
      if (!me->command) badusage(_("unknown action string `%.50s'"),a);
      me->fn();
    }
  } else {
    urq_menu();
  }

  cursesoff();
  set_error_display(0,0);
  error_unwind(ehflag_normaltidy);
  return(0);
}
