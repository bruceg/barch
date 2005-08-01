/* $Id$ */

#include <sysdeps.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/mman.h>

#include <adt/ghash.h>
#include <cli/cli.h>
#include <crc/crc64.h>
#include <iobuf/iobuf.h>
#include <msg/msg.h>
#include <str/iter.h>
#include <str/str.h>

#include "barch.h"

static void add_exinclude(const char*, const struct cli_option*);

const char stamp[8] = "=BArch3\n";
//const char stamp[8] = { 0xba,0xee,0x81,0xb7,0xe6,0x83,0xbf, FILE_VERSION };

int opt_absolute = 0;
int opt_checkpoint = 0;
int opt_dereference = 0;
const char* opt_filename = "-";
int opt_incremental = 0;
int opt_iobuflen = 64*1024;
const char* opt_snapshot = 0;
int opt_usetmp = 1;
int opt_verbose = 0;
time_t opt_timestamp = 0;
int opt_totals = 0;

static int cmd_create = 0;
static int cmd_list = 0;
static int cmd_extract = 0;
static const char* opt_directory = 0;
static const char* opt_timestr = 0;

const char program[] = "barch";
const int msg_show_pid = 0;
const char cli_help_prefix[] = "Create a barch archive\n";
const char cli_help_suffix[] = "";
const char cli_args_usage[] = "[file ...]";
const int cli_args_min = 0;
const int cli_args_max = -1;
cli_option cli_options[] = {
  { 0, "Main operation mode", CLI_SEPARATOR, 0, 0, 0, 0 },
  { 'c', "create", CLI_FLAG, 1, &cmd_create,
    "Create a new archive", 0 },
  { 't', "list", CLI_FLAG, 1, &cmd_list,
    "List the contents of an archive", 0 },
  { 'x', "extract", CLI_FLAG, 1, &cmd_extract,
    "Extract files from an archive", 0 },

  { 0, "Operation modifiers", CLI_SEPARATOR, 0, 0, 0, 0 },
  { 'g', "incremental", CLI_FLAG, 1, &opt_incremental,
    "Incremental backup or restore", 0 },
  { 0, "overwrite-files", CLI_FLAG, 0, &opt_usetmp,
    "Don't extract to temporary files first", 0 },

#if 0
  // FIXME: handle all these options
  { 0,   "Handling of file attributes", CLI_SEPARATOR, 0, 0, 0, 0 },
  { 0,   "owner", CLI_STRING, 0, &opt_owner,
    "Force owner for added files", 0 },
  { 0,   "group", CLI_STRING, 0, &opt_group,
    "Force group for added files", 0 },
  { 'm', "modification-time", CLI_FLAG, 1, &opt_nomtime,
    "Don't extract file modified time", 0 },
  { 0,   "same-owner", CLI_FLAG, 1, &opt_chown,
    "Try extracting files with the same ownership", 0 },
  { 0,   "no-same-owner", CLI_FLAG, 0, &opt_chown,
    "Extract files as yourself", 0 },
  { 'p', "same-permissions", CLI_FLAG, 1, &opt_chmod,
    "Extract permissions information", 0 },
  { 0,   "no-same-permissions", CLI_FLAG, 0, &opt_chmod,
    "Do not extract permissions information", 0 },
  { 0,   "preserve-permissions", CLI_FLAG, 1, &opt_chmod,
    "Same as -p", 0 },
  //
#endif

  { 0, "Archive input/output options", CLI_SEPARATOR, 0, 0, 0, 0 },
  { 'f', "file", CLI_STRING, 0, &opt_filename,
    "Archive filename", "standard input/output" },

  { 0, "Local file selection", CLI_SEPARATOR, 0, 0, 0, 0 },
  { 'C', "directory", CLI_STRING, 0, &opt_directory,
    "Change to named directory after opening archive", 0 },
  { 0, "exclude", CLI_FUNCTION, 0, add_exinclude,
    "Exclude files, given as a glob pattern", 0 },
  { 'X', "exclude-from", CLI_FUNCTION, 0, add_exinclude,
    "Exclude patterns listed in the named file", 0 },
  { 0, "include", CLI_FUNCTION, 0, add_exinclude,
    "Include files, given as a glob pattern", 0 },
  { 'I', "include-from", CLI_FUNCTION, 0, add_exinclude,
    "Include patterns listed in the named file", 0 },
  { 0, "patterns-from", CLI_FUNCTION, 0, add_exinclude,
    "Ex/include patterns listed in the named file", 0 },
  { 'P', "absolute-names", CLI_FLAG, 1, &opt_absolute,
    "Don't strip leading '/'s from file names", 0 },
  { 'h', "dereference", CLI_FLAG, 1, &opt_dereference,
    "Dump instead the files symlinks point to", 0 },
  { 'N', "newer", CLI_STRING, 0, &opt_timestr,
    "Only store files newer than the given file", 0 },
  { 0, "after-date", CLI_STRING, 0, &opt_timestr,
    "Same as --newer", 0 },
  { 0, "snapshot", CLI_STRING, 0, &opt_snapshot,
    "Snapshot CDB for creating incremental archives", 0 },

  { 0, "Informative output", CLI_SEPARATOR, 0, 0, 0, 0 },
  { 'v', "verbose", CLI_COUNTER, 1, &opt_verbose,
    "Show more information about progress", 0 },
  { 0, "checkpoint", CLI_FLAG, 1, &opt_checkpoint,
    "Print directory names as they are processed", 0 },
  { 0, "totals", CLI_FLAG, 1, &opt_totals,
    "Print total bytes written when creating archive", 0 },
  {0,0,0,0,0,0,0}
};

pid_t pid;
time_t start_time;

static int cwd_fd;

void do_chdir(void)
{
  if (opt_directory) {
    if ((cwd_fd = open(".", O_RDONLY)) == -1)
      die1sys(1, "Could not open current directory");
    if (chdir(opt_directory) != 0)
      die3sys(1, "Could not change directory to '", opt_directory, "'");
  }
}

void chdir_back(void)
{
  if (cwd_fd != 0 && fchdir(cwd_fd) != 0)
    die1sys(1, "Could not change directory back");
}

static void parse_timestamp(void)
{
  struct stat st;
  if (opt_timestr == 0) return;
  if (stat(opt_timestr, &st) != 0)
    die3sys(1, "Could not stat '", opt_timestr, "'");
  opt_timestamp = st.st_mtime;
}

char* iobuffer;
unsigned long iobuflen;
static int iobufmmap;

static void alloc_iobuf(void)
{
  if ((iobuffer = mmap(0, iobuflen, PROT_READ|PROT_WRITE,
		    MAP_PRIVATE|MAP_ANON, -1, 0)) != MAP_FAILED)
    iobufmmap = 1;
  else {
    if ((iobuffer = malloc(iobuflen)) != 0)
      iobufmmap = 0;
    else
      die_oom(1);
  }
}

static void make_iobuf(void)
{
  iobuflen = getpagesize();
  while ((long)iobuflen < opt_iobuflen) iobuflen *= 2;
  alloc_iobuf();
}

void ready_iobuf(unsigned long size)
{
  if (iobuflen >= size) return;
  if (iobufmmap) munmap(iobuffer, iobuflen);
  else free(iobuffer);
  while (iobuflen < size) iobuflen *= 2;
  alloc_iobuf();
}

/* Excludes/Includes *********************************************************/
static str exincludes = {0,0,0};

static void add_exinclude_one(char t, const char* s)
{
  if (exincludes.len > 0)
    if (!str_catc(&exincludes, 0)) die_oom(1);
  if (t != 0)
    if (!str_catc(&exincludes, t)) die_oom(1);
  if (!str_cats(&exincludes, s)) die_oom(1);
}

static void add_exinclude_file(char t, const char* f)
{
  ibuf in;
  str line = {0,0,0};
  if (!ibuf_open(&in, f, 0))
    die3sys(1, "Could not open '", f, "'");
  while (ibuf_getstr(&in, &line, LF)) {
    str_strip(&line);
    if (line.len > 0 && line.s[0] != '#')
      add_exinclude_one(t, line.s);
  }
  str_free(&line);
  ibuf_close(&in);
}

static void add_exinclude(const char* s, const struct cli_option* o)
{
  if (o->name[0] == 'p')
    add_exinclude_file(0, s);
  else {
    char t = (o->name[0] == 'e') ? '-' : '+';
    if (o->ch != 0)
      add_exinclude_file(t, s);
    else
      add_exinclude_one(t, s);
  }
}

static int check_filename_part(const char* path)
{
  striter i;
  const str p = { (char*)path, strlen(path), 0 };
  for (striter_start(&i, &exincludes, 0);
       striter_valid(&i);
       striter_advance(&i))
    if (str_globs(&p, i.startptr+1))
      return *i.startptr == '+';
  return -1;
}

static int is_prefix(const char* path, const char* arg)
{
  unsigned long arglen = strlen(arg);
  unsigned long pathlen = strlen(path);
  while (arglen > 1 && arg[arglen-1] == '/') --arglen;
  if (arglen > pathlen) return 0;
  if (memcmp(arg, path, arglen) != 0) return 0;
  return arglen == pathlen || path[arglen] == '/';
}

int check_filename(const char* path, int argc, char* argv[])
{
  int r;
  if (argc > 0) {
    for (r = 0; r < argc; ++r)
      if (is_prefix(path, argv[r]))
	return 1;
    return 0;
  }
  if ((r = check_filename_part(path)) >= 0) return r;
  if ((path = strrchr(path, '/')) != 0)
    if ((r = check_filename_part(++path)) >= 0) return r;
  return 1;
}

/* Main routine **************************************************************/
extern int do_create(int, char**);
extern int do_list(int, char**);
extern int do_extract(int, char**);

int cli_main(int argc, char* argv[])
{
  if (cmd_create + cmd_list + cmd_extract != 1)
    die1(1, "Must specify exactly one of create, list, or extract");

  pid = getpid();
  start_time = time(0);
  pwcache_init();
  parse_timestamp();
  make_iobuf();

  if (cmd_create) return do_create(argc, argv);
  if (cmd_list) return do_list(argc, argv);
  if (cmd_extract) return do_extract(argc, argv);
  return 1;
}
