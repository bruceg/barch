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
#include <uint64.h>

#include "barch.h"

static uint64 crc;
static obuf* out = 0;
static str tmpstr;

void write_prefix(char type, const char* path, const char* metadata)
{
  static int warned_absolute = 0;
  if (!opt_absolute && path[0] == '/') {
    if (!warned_absolute) {
      warn1("Removing leading '/' from member names");
      warned_absolute = 1;
    }
    ++path;
  }

  tmpstr.len = 0;
  if (!str_catc(&tmpstr, type) ||
      !str_catb(&tmpstr, path, strlen(path)+1) ||
      !str_catb(&tmpstr, metadata, strlen(metadata)+1))
    die_oom(1);
  if (!obuf_write(out, stamp, sizeof stamp) ||
      !obuf_putstr(out, &tmpstr))
    die1sys(1, "Could not write record prefix");
  crc = crc64_update(CRC64INIT, tmpstr.s, tmpstr.len);
}

void write_data(const char* data, unsigned long length)
{
  if (length == 0) return;
  tmpstr.len = 0;
  if (!str_catu(&tmpstr, length) ||
      !str_catc(&tmpstr, ':'))
    die_oom(1);
  if (!obuf_putstr(out, &tmpstr) ||
      !obuf_write(out, data, length))
    die1sys(1, "Could not write record data");
  crc = crc64_update(crc, tmpstr.s, tmpstr.len);
  crc = crc64_update(crc, data, length);
}

void write_end(void)
{
  char tmp[8];
  crc = crc64_update(crc, "0:", 2);
  uint64_pack_lsb(crc ^ CRC64POST, tmp);
  if (!obuf_write(out, "0:", 2) ||
      !obuf_write(out, tmp, 8))
    die1sys(1, "Could not write CRC");
}

void write_meta(char type, const char* path, const struct stat* s)
{
  write_prefix(type, path, make_meta(s));
}

void write_file(int fd, const char* path)
{
  long rd;
  while ((rd = read(fd, iobuffer, iobuflen)) > 0)
    write_data(iobuffer, rd);
  if (rd == -1)
    die3sys(1, "Reading from '", path, "' failed");
}

/*****************************************************************************/
void open_output(int argc, char* argv[])
{
  static obuf output;
  str meta = {0,0,0};
  int i;
  /* Generate the metadata string */
  if (!str_copys(&meta, "incremental=") ||
      !str_catu(&meta, opt_incremental))
    die_oom(1);
  /* Open the output file */
  if (opt_filename[0] == '-' && opt_filename[1] == 0) {
    out = &outbuf;
    list = &errbuf;
  }
  else {
    if (!obuf_open(&output, opt_filename, OBUF_CREATE|OBUF_TRUNCATE, 0666, 0))
      die3sys(1, "Could not open output file '", opt_filename, "'");
    out = &output;
  }
  /* Write out SOA record */
  write_prefix(TYPE_START, "", meta.s);
  // FIXME: concatenate into one chunk
  meta.len = 0;
  for (i = 0; i < argc; ++i) {
    if (!str_catb(&meta, argv[i], strlen(argv[i]) + (meta.len > 0)))
      die_oom(1);
  }
  write_data(meta.s, meta.len);
  write_end();
  str_free(&meta);
}

void close_output(void)
{
  write_prefix(TYPE_END, "", "");
  write_end();
  if (!obuf_flush(out))
    die3sys(1, "Could not write to '", opt_filename, "'");
  if (opt_totals) {
    obuf_puts(list, "Total bytes written: ");
    obuf_putu(list, out->io.offset);
    obuf_endl(list);
  }
  if (!obuf_close(out))
    die3sys(1, "Could not close '", opt_filename, "'");
}
