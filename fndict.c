/* $Id$ */

#include <string.h>
#include "barch.h"

static int keycmp(char* const* a, char* const* b)
{
  return strcmp(*a, *b);
}

static int keycopy(char** a, char* const* b)
{
  *a = *b;
  return 1;
}

static int datacopy(int* a, int const* b)
{
  *a = *b;
  return 1;
}

static unsigned long hash(char* const* a)
{
  return ghash_hashs(*a);
}

GHASH_DEFN(fndict,char*,int,hash,keycmp,keycopy,datacopy,0,0);
