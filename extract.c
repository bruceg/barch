/* $Id$ */

#include <sysdeps.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/mman.h>

#include <cli/cli.h>
#include <crc/crc64.h>
#include <iobuf/iobuf.h>
#include <msg/msg.h>
#include <str/str.h>
#include <str/iter.h>
#include <systime.h>

#include "barch.h"

static str uname;
static str gname;

static str tmp_path;
static str linkpath;

static const char* dest_path(const char* path)
{
  if (opt_usetmp)
    path = temppath(path, &tmp_path);
  return path;
}

static void finish_path(const char* path)
{
  if (opt_usetmp) {
    if (rename(tmp_path.s, path) != 0) {
      error5sys("Error renaming '", tmp_path.s, "' to '", path, "'");
      unlink(tmp_path.s);
    }
  }
}

static void abort_path(void)
{
  if (opt_usetmp)
    unlink(tmp_path.s);
}

static void xlate_meta(struct stat* st, const char* meta)
{
  long id;
  parse_meta(st, meta, &uname, &gname);
  if (uname.len > 0 && (id = getpwnamecache(uname.s)) >= 0)
    st->st_uid = id;
  if (gname.len > 0 && (id = getgrnamecache(gname.s)) >= 0)
    st->st_gid = id;
}

static void set_mode(const char* path, const struct stat* st)
{
  chown(path, st->st_uid, st->st_gid);
  if (chmod(path, st->st_mode) != 0)
    warn3sys("Could not set permissions on '", path, "'");
}

static void fset_mode(int fd, const char* path, const struct stat* st)
{
  fchown(fd, st->st_uid, st->st_gid);
  if (fchmod(fd, st->st_mode) != 0)
    warn3sys("Could not set permissions on '", path, "'");
}

static void extract_hardlink(const char* path)
{
  read_str(&linkpath);
  show_record(TYPE_HARDLINK, path, 0, linkpath.len, 0, 0,
	      " link to ", linkpath.s);
  if (read_end(path)) {
    /* FIXME: hardlink to temporary path */
    if (link(linkpath.s, dest_path(path)) == -1) {
      error5sys("Could not link '", path, "' to '", linkpath.s, "'");
      abort_path();
    }
    else
      finish_path(path);
  }
}

static void extract_symlink(const char* path)
{
  read_str(&linkpath);
  show_record(TYPE_HARDLINK, path, 0, linkpath.len, 0, 0, " -> ", linkpath.s);
  if (read_end(path)) {
    if (symlink(linkpath.s, dest_path(path)) == -1) {
      error5sys("Could not symlink '", path, "' to '", linkpath.s, "'");
      abort_path();
    }
    else {
      finish_path(path);
    }
  }
}

static int mkdirprefix(const char* fullpath)
{
  int result;
  const char* p;
  str tmp = {0,0,0};
  p = fullpath + strlen(fullpath) - 1;
  /* Strip off trailing slashes */
  while (p > fullpath && *p == '/') --p;
  /* Strip off last path component */
  while (p > fullpath && *p != '/') --p;
  if (p == fullpath) return 0;
  /* Strip off remaining slashes */
  while (p > fullpath && *p == '/') --p;
  ++p;
  if (!str_copyb(&tmp, fullpath, p-fullpath)) die_oom(1);
  if ((result = mkdir(tmp.s, 0777)) != 0) {
    mkdirprefix(tmp.s);
    result = mkdir(tmp.s, 0777);
  }
  str_free(&tmp);
  return result;
}

static int open_write(const char* path)
{
  int fd;
  path = dest_path(path);
  /* Common case first -- just open the file */
  if ((fd = open(path, O_WRONLY|O_TRUNC|O_CREAT, 0600)) != -1) return fd;
  if (errno != ENOENT) return fd;
  /* Only try to make the leading path if it doesn't exist. */
  if (mkdirprefix(path) != 0) return -1;
  return open(path, O_WRONLY|O_TRUNC|O_CREAT, 0600);
}

static void close_write(int fd, const char* path, const struct stat* st)
{
  if (opt_usetmp) {
    fset_mode(fd, tmp_path.s, st);
    if (close(fd) != 0) {
      error3sys("Error closing '", tmp_path.s, "'");
      unlink(tmp_path.s);
    }
    else
      finish_path(path);
  }
  else {
    fset_mode(fd, path, st);
    if (close(fd) != 0)
      error3sys("Error closing '", path, "'");
  }
}

static void extract_file(const char* path, const char* meta)
{
  struct stat st;
  int fd;
  unsigned long length;
  if ((fd = open_write(path)) == -1) {
    error3sys("Could not open '", path, "' for writing");
    length = read_skip();
  }
  else
    length = read_file(fd, path);
  xlate_meta(&st, meta);
  show_record(TYPE_FILE, path, &st, length, uname.s, gname.s, 0, 0);
  if (read_end(path)) {
    if (fd != -1)
      close_write(fd, path, &st);
  }
  else {
    close(fd);
    abort_path();
  }
}

static int mkdirp(const char* path, mode_t mode)
{
  if (mkdir(path, mode) == 0) return 0;
  if (errno != ENOENT) return -1;
  if (mkdirprefix(path) != 0) return -1;
  return mkdir(path, mode);
}

static void do_incremental(const char* path, const str* entries)
{
  striter i;
  int dummy;
  direntry* entry;
  DIR* dir;
  struct ghash dentries = {0,0,0,0,0,0,0,0,0,0,0};
  struct fndict_entry* de;
  str todo = {0,0,0};
  str fullpath = {0,0,0};
  unsigned long pathlen;

  if ((dir = opendir(path)) == 0)
    error3sys("Could not open directory '", path, "'");
  else {
    /* Each entry has the following value:
     * 0: Listed in archive
     * 1: Listed in archive and present in directory
     */
    fndict_init(&dentries);
    dummy = 0;
    for (striter_start(&i, entries, 0); striter_valid(&i); striter_advance(&i))
      if (!fndict_add(&dentries, (char**)&i.startptr, &dummy)) die_oom(1);
    while ((entry = readdir(dir)) != 0) {
      const char* name = entry->d_name;
      if (name[0] == '.' &&
	  (name[1] == 0 ||
	   (name[1] == '.' && name[2] == 0)))
	continue;
      if ((de = fndict_get(&dentries, (char**)&name)) == 0) {
	if (!str_catb(&todo, name, strlen(name)+1)) die_oom(1);
      }
      else
	de->data = 1;
    }
    closedir(dir);
    if (!str_copys(&fullpath, path) ||
	!str_catc(&fullpath, '/'))
      die_oom(1);
    pathlen = fullpath.len;

    /* Check for files listed in the archive
     * but not present in the directory */
    for (striter_start(&i, entries, 0); striter_valid(&i); striter_advance(&i))
      if (i.len > 0
	  && fndict_get(&dentries, (char**)&i.startptr)->data == 0) {
	fullpath.len = pathlen;
	if (!str_catiter(&fullpath, &i)) die_oom(1);
	warn3("File '", fullpath.s, "' is missing from destination");
      }
    fndict_free(&dentries);

    /* Check for files present in the directory
     * but not listed in the archive */
    for (striter_start(&i, &todo, 0); striter_valid(&i); striter_advance(&i)) {
      fullpath.len = pathlen;
      if (!str_catiter(&fullpath, &i)) die_oom(1);
      if (opt_verbose)
	obuf_put3s(list, "Removing extraneous file '", fullpath.s, "'\n");
      // FIXME: what to do about removing directories?
      if (unlink(fullpath.s) != 0)
	error3sys("Could not unlink '", fullpath.s, "'");
    }
    str_free(&fullpath);
    str_free(&todo);
  }
}

static void extract_dir(const char* path, const char* meta)
{
  struct stat st;
  int exists;
  str entries = {0,0,0};
  unsigned long length;

  if (opt_incremental) {
    length = read_str(&entries);
    if (!str_catc(&entries, 0)) die_oom(1);
  }
  else
    length = read_skip();

  exists = stat(path, &st) == 0;
  xlate_meta(&st, meta);
  show_record(TYPE_HARDLINK, path, &st, length, uname.s, gname.s, 0, 0);

  if (read_end(path)) {
    if (!exists && mkdirp(path, 0700) != 0)
      error3sys("Could not create directory '", path, "'");
    else {
      if (opt_incremental)
	do_incremental(path, &entries);
      set_mode(path, &st);
    }
  }
  str_free(&entries);
}

static void extract_pipe(const char* path, const char* meta)
{
  struct stat st;
  unsigned long length;
  length = read_skip();
  xlate_meta(&st, meta);
  show_record(TYPE_PIPE, path, &st, length, uname.s, gname.s, 0, 0);
  if (read_end(path)) {
    if (mknod(dest_path(path), 0, S_IFIFO) != 0)
      error3sys("Could not create pipe '", path, "'");
    else {
      // FIXME: Should set mode before renaming, not other way around.
      finish_path(path);
      set_mode(path, &st);
    }
  }
}

int do_extract(int argc, char* argv[])
{
  char type;
  str path = {0,0,0};
  str meta = {0,0,0};

  open_input();
  do_chdir();
  while ((type = read_prefix(&path, &meta)) != 0) {
    if (check_filename(path.s, argc, argv)) {
      switch (type) {
      case TYPE_SYMLINK:  extract_symlink(path.s); break;
      case TYPE_HARDLINK: extract_hardlink(path.s); break;
      case TYPE_FILE:     extract_file(path.s, meta.s); break;
      case TYPE_DIR:      extract_dir(path.s, meta.s); break;
      case TYPE_PIPE:     extract_pipe(path.s, meta.s); break;
      case TYPE_SOCKET:
	warn3("Cannot extract socket '", path.s, "'");
	break;
      case TYPE_START:
      case TYPE_END:
	read_skip();
	read_end(path.s);
	break;
      default:
	read_skip();
	read_end(path.s);
	warn3("Unhandled record type for '", path.s, "'");
      }
    }
    else {
      read_skip();
      read_end(path.s);
    }
  }
  obuf_flush(list);
  close_input();
  return 0;
}
