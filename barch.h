/* $Id$ */

#ifndef BARCH__H__
#define BARCH__H__

#include <sysdeps.h>
#include <systime.h>
#include <adt/ghash.h>
#include <str/str.h>

struct stat;
struct str;
struct cli_stringlist;
struct iobuf;

#define TYPE_START '^'
#define TYPE_END '$'
#define TYPE_BLOCK 'b'
#define TYPE_CHAR 'c'
#define TYPE_DIR 'd'
#define TYPE_FILE 'f'
#define TYPE_IFILE 'F'
#define TYPE_HARDLINK 'h'
#define TYPE_SYMLINK 'l'
#define TYPE_PIPE 'p'
#define TYPE_SOCKET 's'

/* barch.c */
#define COMPRESS_Z 1
#define COMPRESS_GZ 2
#define COMPRESS_BZ2 3

extern int opt_absolute;
extern int opt_checkpoint;
extern int opt_dereference;
extern const char* opt_filename;
extern int opt_incremental;
extern int opt_iolen;
extern const char* opt_snapshot;
extern const char* opt_differential;
extern time_t opt_timestamp;
extern int opt_usetmp;
extern int opt_verbose;
extern int opt_totals;
extern int opt_compress;
extern int opt_onefilesystem;

extern const char stamp[8];
extern pid_t pid;
extern time_t start_time;
extern char* iobuffer;
extern unsigned long iobuflen;

extern void do_chdir(void);
extern void chdir_back(void);
extern int check_filename(const char* filename, int argc, char* argv[]);
extern void ready_iobuf(unsigned long size);

/* compress.c */
extern void compress_start(struct iobuf* io);
extern void compress_end(void);
extern void decompress_start(struct iobuf* io);

/* fndict.c */
GHASH_DECL(fndict,char*,int);

/* meta.c */
extern const char* make_meta(const struct stat* s);
extern int parse_meta(struct stat* st, const char* s, str* uname, str* gname);

/* pwcache.c */
extern void pwcache_init(void);
extern const char* getpwuidcache(long id);
extern const char* getgrgidcache(long id);
extern long getpwnamecache(const char* name);
extern long getgrnamecache(const char* name);

/* read.c */
extern void open_input(void);
extern void close_input(void);
extern char read_prefix(struct str* path, struct str* metadata);
extern int read_end(const char* path);
extern unsigned long read_file(int fd, const char* path);
extern unsigned long read_skip(void);
extern unsigned long read_str(struct str* s);

/* show.c */
extern struct obuf* list;
extern void show_record(char type, const char* path,
			const struct stat* st, unsigned long length,
			const char* uname, const char* gname,
			const char* linktype, const char* linkdest);

/* snapshot.c */
extern void snapshot_open(void);
extern void snapshot_close(void);
extern void snapshot_abort(void);
extern const char* snapshot_lookup(const struct stat* st);
extern int snapshot_add(const struct stat* st, const char* path);

/* write.c */
extern void open_output(int argc, char* argv[]);
extern void close_output(void);
extern void write_prefix(char type, const char* path, const char* metadata);
extern void write_data(const char* data, unsigned long length);
extern void write_end(void);
extern void write_meta(char type, const char* path, const struct stat* s);
extern void write_file(int fd, const char* path);

#endif
