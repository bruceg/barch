/* $Id$ */

#include <systime.h>

#include <msg/msg.h>
#include <str/str.h>

#include "barch.h"

const char* temppath(const char* prefix, str* path)
{
  struct timeval tv;
  gettimeofday(&tv, 0);
  if (!str_copy2s(path, prefix, ".tmp.barch.") ||
      !str_catu(path, pid) ||
      !str_catc(path, '.') ||
      !str_catu(path, tv.tv_sec) ||
      !str_catc(path, '.') ||
      !str_catuw(path, tv.tv_usec, 6, '0'))
    die_oom(1);
  return path->s;
}
