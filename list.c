/* $Id$ */

#include <sysdeps.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/mman.h>

#include <cli/cli.h>
#include <crc/crc64.h>
#include <iobuf/iobuf.h>
#include <msg/msg.h>
#include <str/str.h>
#include <systime.h>

#include "barch.h"

int do_list(int argc, char* argv[])
{
  char type;
  str path = {0,0,0};
  str meta = {0,0,0};
  str uname = {0,0,0};
  str gname = {0,0,0};
  str linkpath = {0,0,0};
  unsigned long length;
  const char* linktype;
  struct stat st;
  struct stat* stptr;

  ++opt_verbose;
  open_input();
  while ((type = read_prefix(&path, &meta)) != 0) {
    if (check_filename(path.s, argc, argv)) {
      switch (type) {
      case TYPE_SYMLINK:
	linktype = " -> ";
	length = read_str(&linkpath);
	break;
      case TYPE_HARDLINK:
	linktype = " link to ";
	length = read_str(&linkpath);
	break;
      default:
	linktype = 0;
	length = read_skip();
      }
      if (!parse_meta(stptr = &st, meta.s, &uname, &gname)) stptr = 0;
      show_record(type, path.s, stptr, length, uname.s, gname.s,
		  linktype, linkpath.s);
    }
    else
      read_skip();
    read_end(path.s);
  }
  obuf_flush(&outbuf);
  close_input();
  return 0;
}
