/* $Id$ */

#include <sysdeps.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cdb/cdb.h>
#include <cdb/make.h>
#include <cdb/str.h>
#include <msg/msg.h>
#include <str/str.h>

#include "barch.h"

static str writer_tmp;
static int reader_fd;
static struct cdb reader;
static int writer_fd;
static struct cdb_make writer;
static str key;
static str data;

time_t snapshot_time = 0;

void snapshot_open(void)
{
  if (!opt_snapshot) return;
  if ((reader_fd = open(opt_snapshot, O_RDONLY)) == -1) {
    if (errno != ENOENT)
      die3sys(1, "Could not open snapshot file '", opt_snapshot, "'");
  }
  else {
    char* end;
    cdb_init(&reader, reader_fd);
    key.len = 0;
    switch (cdb_get(&reader, &key, &data)) {
    case -1: die_oom(1);
    case 0: die1(1, "Could not find timestamp record in snapshot file");
    }
    if ((snapshot_time = strtoul(data.s, &end, 10)) == 0 || *end != 0)
      die1(1, "Timestamp in snapshot file is corrupted");
  }
  if (!str_copy2s(&writer_tmp, opt_snapshot, ".barch.tmp.") ||
      !str_catu(&writer_tmp, pid) ||
      !str_catc(&writer_tmp, '.') ||
      !str_catu(&writer_tmp, start_time))
    die_oom(1);
  if ((writer_fd = open(writer_tmp.s, O_WRONLY|O_EXCL|O_CREAT, 0666)) == -1)
    die3sys(1, "Could not open temporary snapshot file for writing, '",
	    writer_tmp.s, "'");
  data.len = 0;
  if (cdb_make_start(&writer, writer_fd) != 0 ||
      !str_catu(&data, start_time) ||
      cdb_make_add(&writer, 0, 0, data.s, data.len) != 0) {
    unlink(writer_tmp.s);
    die_oom(1);
  }
  
}

void snapshot_close(void)
{
  chdir_back();
  if (!opt_snapshot) return;
  if (cdb_make_finish(&writer) != 0 || close(writer_fd) != 0)
    error3sys("Error finishing snapshot file '", writer_tmp.s, "'");
  else if (rename(writer_tmp.s, opt_snapshot) != 0)
    error5sys("Error renaming '", writer_tmp.s, "' to '", opt_snapshot, "'");
  unlink(writer_tmp.s);
  cdb_free(&reader);
  close(reader_fd);
}

void snapshot_abort(void)
{
  chdir_back();
  if (!opt_snapshot) return;
  unlink(writer_tmp.s);
}

static void make_key(const struct stat* st)
{
  key.len = 0;
  if (!str_catu(&key, st->st_dev) ||
      !str_catc(&key, ':') ||
      !str_catu(&key, st->st_ino))
    die_oom(1);
}

const char* snapshot_lookup(const struct stat* st)
{
  if (reader.fd == 0) return 0;
  make_key(st);
  switch (cdb_get(&reader, &key, &data)) {
  case -1: die_oom(1);
  case 0: return 0;
  }
  return data.s;
}

int snapshot_add(const struct stat* st, const char* path)
{
  if (!opt_snapshot) return 1;
  make_key(st);
  return cdb_make_add(&writer, key.s, key.len, path, strlen(path)) == 0;
}
