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

#include "barch.h"

/*****************************************************************************/
struct devino
{
  dev_t dev;
  ino_t ino;
};

static unsigned long devino_hash(const struct devino* p)
{
  unsigned long l = p->dev ^ p->ino;
  return l ^ (l << 3) ^ (l << 6) ^ (l << 9) ^ (l << 12) ^ (l << 15);
}

static int devino_cmp(const struct devino* a, const struct devino* b)
{
  return (a->dev != b->dev) || (a->ino != b->ino);
}

static int devino_copy(struct devino* a, const struct devino* b)
{
  *a = *b;
  return 1;
}

static int name_copy(char** a, char* const* b)
{
  return (*a = strdup(*b)) != 0;
}

static void name_free(char** a)
{
  free(*a);
}

GHASH_DECL(dicache,struct devino,char*);
GHASH_DEFN(dicache,struct devino,char*,
	   devino_hash,devino_cmp,devino_copy,
	   name_copy,0,name_free);

static struct ghash dicache;

/*****************************************************************************/
static void dump_rec(const char* path, const struct stat* parent, int addall);

static void dump_file(const char* path, const struct stat* st)
{
  int fd;
  if ((fd = open(path, O_RDONLY)) == -1)
    error3sys("Could not open file '", path, "'");
  else {
    write_meta(TYPE_FILE, path, st);
    write_file(fd, path);
    write_end();
    show_record(TYPE_FILE, path, st, st->st_size, 0, 0, 0, 0);
    close(fd);
  }
}

static void dump_dir(const char* path, struct stat* st,
		     int recurse, int addall)
{
  DIR* dir;
  direntry* entry;
  str fullpath = {0,0,0};
  str entries = {0,0,0};
  long pathlen;
  if (recurse) {
    if ((dir = opendir(path)) == 0) {
      error3sys("Could not open directory '", path, "'");
      return;
    }
    if (!str_copys(&fullpath, path) || !str_catc(&fullpath, '/'))
      die_oom(1);
    pathlen = fullpath.len;
    while ((entry = readdir(dir)) != 0) {
      const char* name = entry->d_name;
      if (name[0] == '.' &&
	  (name[1] == 0 ||
	   (name[1] == '.' && name[2] == 0)))
	continue;
      fullpath.len = pathlen;
      if (!str_cats(&fullpath, name)) die_oom(1);
      if (check_filename(fullpath.s, 0, 0)) {
	dump_rec(fullpath.s, st, addall);
	if (opt_incremental)
	  if (!str_cats(&entries, name) ||
	      !str_catc(&entries, 0))
	    die_oom(1);
      }
    }
    closedir(dir);
    if (entries.len > 0) --entries.len;
  }
  st->st_size = 0;
  write_meta(TYPE_DIR, path, st);
  if (opt_incremental)
    write_data(entries.s, entries.len);
  write_end();
  if (opt_checkpoint && opt_verbose == 0) {
    obuf_puts(list, path);
    obuf_endl(list);
  }
  show_record(TYPE_DIR, path, st, entries.len, 0, 0, 0, 0);
  str_free(&entries);
  str_free(&fullpath);
}

static void dump_device(const char* path, struct stat* st, char type)
{
  static str numbers;
  numbers.len = 0;
  if (!str_catu(&numbers, major(st->st_rdev)) ||
      !str_catc(&numbers, ',') ||
      !str_catc(&numbers, minor(st->st_rdev)))
    die_oom(1);
  st->st_size = 0;
  write_prefix(type, path, "");
  write_data(numbers.s, numbers.len);
  write_end();
  show_record(type, path, st, numbers.len, 0, 0, 0, 0);
}

static void dump_special(const char* path, struct stat* st, char type)
{
  st->st_size = 0;
  write_meta(type, path, st);
  write_end();
  show_record(type, path, st, 0, 0, 0, 0, 0);
}

static void dump_symlink(const char* path, struct stat* st)
{
  long len;
  if ((len = readlink(path, iobuffer, iobuflen-1)) == -1) {
    error3sys("Error reading symbolic link '", path, "'");
    return;
  }
  iobuffer[len] = 0;
  st->st_size = 0;
  write_prefix(TYPE_SYMLINK, path, "");
  write_data(iobuffer, len);
  write_end();
  show_record(TYPE_SYMLINK, path, st, len, 0, 0, 0, 0);
}

static void dump_rec(const char* path, const struct stat* parent, int addall)
{
  struct stat st;
  if (!check_filename(path, 0, 0)) return;
  if ((opt_dereference ? stat(path, &st) : lstat(path, &st)) != 0)
    error3sys("Could not stat '", path, "'");
  else {
    struct devino di;
    const struct dicache_entry* e;
    const char* snapfile = snapshot_lookup(&st);
    const int snapdiffers = (snapfile == 0) || (strcmp(snapfile, path) != 0);
    snapshot_add(&st, path);
    /* Check if the file has been recently modified */
    if (!addall
	&& !S_ISDIR(st.st_mode)
	&& !snapdiffers
	&& (st.st_mtime < opt_timestamp
	    && st.st_ctime < opt_timestamp))
      return;
    /* Check if the file has already been added to the archive */
    di.dev = st.st_dev;
    di.ino = st.st_ino;
    if ((e = dicache_get(&dicache, &di)) != 0) {
      unsigned long len = strlen(e->data);
      /* If the filename is the same as the previously added file,
       * just ignore it */
      if (memcmp(e->data, path, len+1) == 0) return;
      /* Otherwise, record a hard link. */
      write_prefix(TYPE_HARDLINK, path, "");
      write_data(e->data, len);
      write_end();
      show_record(TYPE_HARDLINK, path, 0, len, 0, 0, " link to ", e->data);
    }
    else {
      dicache_add(&dicache, &di, (char**)&path);
      if (S_ISREG(st.st_mode))
	dump_file(path, &st);
      else if (S_ISDIR(st.st_mode))
	dump_dir(path, &st,
		 !opt_onefilesystem
		 || (parent == 0)
		 || (parent->st_dev == st.st_dev),
		 addall || snapdiffers);
      else if (S_ISLNK(st.st_mode))
	dump_symlink(path, &st);
      else if (S_ISCHR(st.st_mode))
	dump_device(path, &st, TYPE_CHAR);
      else if (S_ISBLK(st.st_mode))
	dump_device(path, &st, TYPE_BLOCK);
      else if (S_ISFIFO(st.st_mode))
	dump_special(path, &st, TYPE_PIPE);
      else if (S_ISSOCK(st.st_mode))
	dump_special(path, &st, TYPE_SOCKET);
      else
	error3sys("Unhandled file type for '", path, "'");
    }
  }
}

/*****************************************************************************/
int do_create(int argc, char* argv[])
{
  int i;
  if (argc <= 0) die1(1, "Missing parameters");
  dicache_init(&dicache);
  open_output(argc, argv);
  snapshot_open();
  do_chdir();
  for (i = 0; i < argc; ++i)
    dump_rec(argv[i], 0, 0);
  obuf_flush(list);
  close_output();
  snapshot_close();
  return 0;
}
