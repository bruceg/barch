/* $Id$ */

#include <sysdeps.h>
#include <grp.h>
#include <pwd.h>
#include <string.h>

#include <adt/ghash.h>
#include <str/str.h>

#include "barch.h"

/* Generic functions *********************************************************/
static unsigned long name_hash(const char* const* a)
{
  return ghash_hashs(*a);
}

static int name_cmp(const char* const* a, const char* const* b)
{
  return strcmp(*a, *b);
}

static int name_copy(const char** a, const char* const* b)
{
  if (*b == 0)
    *a = 0;
  else
    if ((*a = strdup(*b)) == 0) return 0;
  return 1;
}

static void name_free(char** a)
{
  free(*a);
}

static unsigned long long_hash(const long* p)
{
  unsigned long l = *p;
  return l ^ (l << 3) ^ (l << 6) ^ (l << 9) ^ (l << 12) ^ (l << 15);
}

static int long_cmp(const long* a, const long* b)
{
  return *a != *b;
}

static int long_copy(long* a, const long* b)
{
  *a = *b;
  return 1;
}

/* Hash table definitions ****************************************************/
GHASH_DECL(pwnamecache,const char*,long);
GHASH_DEFN(pwnamecache,const char*,long,
	   name_hash,name_cmp,name_copy,long_copy,name_free,0);

static struct ghash pwnamecache;
static struct ghash grnamecache;

GHASH_DECL(pwuidcache,long,const char*);
GHASH_DEFN(pwuidcache,long,const char*,
	   long_hash,long_cmp,long_copy,name_copy,0,name_free);

static struct ghash pwuidcache;
static struct ghash grgidcache;

/* Public accessors **********************************************************/
void pwcache_init(void)
{
  pwnamecache_init(&pwnamecache);
  pwnamecache_init(&grnamecache);
  pwuidcache_init(&pwuidcache);
  pwuidcache_init(&grgidcache);
}

const char* getpwuidcache(long id)
{
  struct pwuidcache_entry* e;
  struct passwd* pw;
  const char* name;
  if ((e = pwuidcache_get(&pwuidcache, &id)) != 0)
    return e->data;
  name = ((pw = getpwuid(id)) == 0) ? 0 : pw->pw_name;
  pwuidcache_add(&pwuidcache, &id, &name);
  return name;
}

const char* getgrgidcache(long id)
{
  struct pwuidcache_entry* e;
  struct group* gr;
  const char* name;
  if ((e = pwuidcache_get(&grgidcache, &id)) != 0)
    return e->data;
  name = ((gr = getgrgid(id)) == 0) ? 0 : gr->gr_name;
  pwuidcache_add(&grgidcache, &id, &name);
  return name;
}

long getpwnamecache(const char* name)
{
  struct pwnamecache_entry* e;
  struct passwd* pw;
  long id;
  if ((e = pwnamecache_get(&pwnamecache, &name)) != 0)
    return e->data;
  id = ((pw = getpwnam(name)) == 0) ? -1 : (long)pw->pw_uid;
  pwnamecache_add(&pwnamecache, &name, &id);
  return id;
}

long getgrnamecache(const char* name)
{
  struct pwnamecache_entry* e;
  struct group* gr;
  long id;
  if ((e = pwnamecache_get(&grnamecache, &name)) != 0)
    return e->data;
  id = ((gr = getgrnam(name)) == 0) ? -1 : (long)gr->gr_gid;
  pwnamecache_add(&grnamecache, &name, &id);
  return id;
}
