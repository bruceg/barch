/* $Id$ */

#include <sysdeps.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/mman.h>

#include <crc/crc64.h>
#include <iobuf/iobuf.h>
#include <msg/msg.h>
#include <str/str.h>

#include "barch.h"

static str meta;

static int str_cato(str* s, unsigned long o)
{
  return str_catunumw(s, o, 0, 0, 8, str_lcase_digits);
}

const char* make_meta(const struct stat* s)
{
  const char* uname;
  const char* gname;
  if ((uname = getpwuidcache(s->st_uid)) == 0) uname = "";
  if ((gname = getgrgidcache(s->st_gid)) == 0) gname = "";
  if (!str_truncate(&meta, 0)) die_oom(1);
  if (!str_catc(&meta, 's') ||
      !str_catu(&meta, s->st_size) ||
      !str_catb(&meta, ":p", 2) ||
      !str_cato(&meta, s->st_mode & 07777) ||
      !str_catb(&meta, ":c", 2) ||
      !str_catu(&meta, s->st_ctime) ||
      !str_catb(&meta, ":m", 2) ||
      !str_catu(&meta, s->st_mtime) ||
      !str_catb(&meta, ":o", 2) ||
      !str_catu(&meta, s->st_uid) ||
      !str_catb(&meta, ":O", 2) ||
      !str_cats(&meta, uname) ||
      !str_catb(&meta, ":g", 2) ||
      !str_catu(&meta, s->st_gid) ||
      !str_catb(&meta, ":G", 2) ||
      !str_cats(&meta, gname))
    die_oom(1);
  return meta.s;
}

const char* make_xmeta(const struct stat* s)
{
  make_meta(s);
  if (!str_catb(&meta, ":d", 2) ||
      !str_catu(&meta, s->st_dev) ||
      !str_catb(&meta, ":i", 2) ||
      !str_catu(&meta, s->st_ino))
    die_oom(1);
  return meta.s;
}

static void str_copyst(str* s, const char* c, const char** end, char t)
{
  const char* start = c;
  while (*c != 0 && *c != t) ++c;
  if (!str_copyb(s, start, c-start)) die_oom(1);
  if (end) *end = c;
}
    
int parse_meta(struct stat* st, const char* s, str* uname, str* gname)
{
  const char* e;
  memset(st, 0, sizeof *st);
  while (*s != 0) {
    switch (*s++) {
    case 's': st->st_size = strtoul(s, (char**)&e, 10); break;
    case 'p': st->st_mode = strtoul(s, (char**)&e, 8); break;
    case 'c': st->st_ctime = strtoul(s, (char**)&e, 10); break;
    case 'm': st->st_mtime = strtoul(s, (char**)&e, 10); break;
    case 'o': st->st_uid = strtoul(s, (char**)&e, 10); break;
    case 'g': st->st_gid = strtoul(s, (char**)&e, 10); break;
    case 'O': str_copyst(uname, s, &e, ':'); break;
    case 'G': str_copyst(gname, s, &e, ':'); break;
    default:
      e = s;
      while (*e != 0 && *e != ':') ++e;
    }
    if (*e != 0 && *e != ':') return 0;
    s = e + (*e == ':');
  }
  return 1;
}
