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
static ibuf* in = 0;

static int search_prefix(void)
{
  char t;
  int i;
  i = -1;
  while (ibuf_getc(in, &t)) {
    if (t == '=')
      i = 0;
    else if (i >= 0) {
      if (t == stamp[++i]) {
	if (i >= 7)
	  return 1;
      }
      else
	i = -1;
    }
  }
  if (!ibuf_eof(in))
    die1sys(1, "Could not find prefix stamp");
  return 0;
}

char read_prefix(str* path, str* metadata)
{
  char type;
  if (!search_prefix()) return 0;
  if (!ibuf_getc(in, &type) ||
      !ibuf_getstr(in, path, 0) ||
      !ibuf_getstr(in, metadata, 0))
    die1sys(1, "Could not read prefix data");
  crc = crc64_update(CRC64INIT, &type, 1);
  crc = crc64_update(crc, path->s, path->len);
  crc = crc64_update(crc, metadata->s, metadata->len);
  return type;
}

int read_end(const char* path)
{
  char tmp[8];
  if (!ibuf_read(in, tmp, 8))
    die1sys(1, "Could not read CRC");
  if (uint64_get_lsb(tmp) != (crc ^ CRC64POST)) {
    error3("CRC check failed for '", path, "'");
    return 0;
  }
  return 1;
}

unsigned long read_length(void)
{
  static str lenbuf;
  unsigned long len;
  char* end;
  if (!ibuf_getstr(in, &lenbuf, ':'))
    die1sys(1, "Could not read length prefix");
  len = strtoul(lenbuf.s, &end, 10);
  if (end != lenbuf.s + lenbuf.len - 1)
    die1sys(1, "Misformatted length prefix");
  crc = crc64_update(crc, lenbuf.s, lenbuf.len);
  return len;
}

static unsigned long read_chunk(void)
{
  const unsigned long length = read_length();
  unsigned long len = 0;
  ready_iobuf(length+1);
  while (len < length) {
    if (!ibuf_read(in, iobuffer + len, length - len))
      if (ibuf_error(in))
	die1sys(1, "Could not read record data");
    crc = crc64_update(crc, iobuffer + len, in->count);
    len += in->count;
  }
  iobuffer[len] = 0;
  return length;
}

unsigned long read_str(str* s)
{
  unsigned long length;
  if (!str_truncate(s, 0)) die_oom(1);
  while ((length = read_chunk()) > 0)
    if (!str_catb(s, iobuffer, length)) die_oom(1);
  return s->len;
}

unsigned long read_skip(void)
{
  unsigned long total = 0;
  unsigned long length;
  while ((length = read_chunk()) > 0)
    total += length;
  return total;
}

unsigned long read_file(int fd, const char* path)
{
  unsigned long total = 0;
  unsigned long length;
  while ((length = read_chunk()) > 0) {
    const char* ptr = iobuffer;
    total += length;
    while (length > 0) {
      long wr;
      if ((wr = write(fd, ptr, length)) == -1 || wr == 0)
	die3sys(1, "Writing to '", path, "' failed");
      length -= wr;
      ptr += wr;
    }
  }
  return total;
}

/*****************************************************************************/
void open_input(void)
{
  static ibuf input;
  if (opt_filename[0] == '-' && opt_filename[1] == 0)
    in = &inbuf;
  else {
    if (!ibuf_open(&input, opt_filename, 0))
      die3sys(1, "Could not open input file '", opt_filename, "'");
    in = &input;
  }
  decompress_start(&in->io);
}

void close_input(void)
{
  compress_end();
  ibuf_close(in);
}
