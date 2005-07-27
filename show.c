/* $Id$ */

#include <sysdeps.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/mman.h>

#include <iobuf/iobuf.h>
#include <str/str.h>
#include <systime.h>

#include "barch.h"

obuf* list = &outbuf;

static void show_meta(char type, unsigned long length, const struct stat* st,
		      const char* uname, const char* gname)
{
  struct tm* t;
  if (st == 0)
    obuf_puts(list, "                             ");
  else {
    /* symbolic mode */
    if (type == TYPE_FILE) type = '-';
    obuf_putc(list, type);
    obuf_putc(list, st->st_mode & 0400 ? 'r' : '-');
    obuf_putc(list, st->st_mode & 0200 ? 'w' : '-');
    obuf_putc(list, st->st_mode & 04000 ?
	      (st->st_mode & 0100 ? 's' : 'S') :
	      (st->st_mode & 0100 ? 'x' : '-'));
    obuf_putc(list, st->st_mode & 040 ? 'r' : '-');
    obuf_putc(list, st->st_mode & 020 ? 'w' : '-');
    obuf_putc(list, st->st_mode & 02000 ?
	      (st->st_mode & 010 ? 's' : 'S') :
	      (st->st_mode & 010 ? 'x' : '-'));
    obuf_putc(list, st->st_mode & 04 ? 'r' : '-');
    obuf_putc(list, st->st_mode & 02 ? 'w' : '-');
    obuf_putc(list, st->st_mode & 01000 ?
	      (st->st_mode & 01 ? 't' : 'T') :
	      (st->st_mode & 01 ? 'x' : '-'));
    obuf_putc(list, ' ');
    /* user and group names */
    if (uname != 0 && *uname != 0) {
      long len = strlen(uname);
      obuf_write(list, uname, len);
      while (len++ < 8) obuf_putc(list, ' ');
    }
    else
      obuf_putuw(list, st->st_uid, 8, ' ');
    obuf_putc(list, ' ');
    if (gname != 0 && *gname != 0) {
      long len = strlen(gname);
      obuf_write(list, gname, len);
      while (len++ < 8) obuf_putc(list, ' ');
    }
    else
      obuf_putuw(list, st->st_gid, 8, ' ');
    obuf_putc(list, ' ');
  }
  /* record length */
  obuf_putuw(list, length, 9, ' ');
  obuf_putc(list, ' ');
  if (st == 0)
    obuf_puts(list, "                    ");
  else {
    /* timestamp */
    t = localtime(&st->st_mtime);
    obuf_putuw(list, t->tm_year+1900, 4, '0');
    obuf_putc(list, '-');
    obuf_putuw(list, t->tm_mon+1, 2, '0');
    obuf_putc(list, '-');
    obuf_putuw(list, t->tm_mday, 2, '0');
    obuf_putc(list, ' ');
    obuf_putuw(list, t->tm_hour, 2, '0');
    obuf_putc(list, ':');
    obuf_putuw(list, t->tm_min, 2, '0');
    obuf_putc(list, ':');
    obuf_putuw(list, t->tm_sec, 2, '0');
    obuf_putc(list, ' ');
  }
}

void show_record(char type, const char* path,
		 const struct stat* st, unsigned long length,
		 const char* uname, const char* gname,
		 const char* linktype, const char* linkdest)
{
  extern int opt_verbose;
  if (opt_verbose) {
    if (opt_verbose >= 2)
      show_meta(type, length, st, uname, gname);
    switch (type) {
    case TYPE_START: obuf_puts(list, "***** SOA *****"); break;
    case TYPE_END:   obuf_puts(list, "***** EOA *****"); break;
    default:         obuf_puts(list, path);
    }
    if (opt_verbose >= 2 && linktype && linkdest)
      obuf_put2s(list, linktype, linkdest);
    obuf_endl(list);
  }
}
