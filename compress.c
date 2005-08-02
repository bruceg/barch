/* $Id$ */

#include <sysdeps.h>
#include <sys/wait.h>
#include <unistd.h>

#include <iobuf/iobuf.h>
#include <msg/msg.h>

#include "barch.h"

static const char* compress_argv[3] = { 0, 0, 0 };
static pid_t compress_pid = 0;

static void compress_common(iobuf* io,
			    const char* argv1,
			    int fd0,
			    int fd1)
{
  int fds[2];
  if (!opt_compress)
    return;
  compress_argv[0] = (opt_compress == COMPRESS_GZ) ? "gzip"
    : (opt_compress == COMPRESS_BZ2) ? "bzip2"
    : "compress";
  compress_argv[1] = argv1;
  if (pipe(fds) != 0)
    die1sys(1, "Could not create pipe");
  switch (compress_pid = fork()) {
  case -1:
    die1sys(1, "Could not fork");
  case 0:
    dup2(fds[fd0], fd0);
    close(fds[fd1]);
    if (io->fd != fd1) {
      dup2(io->fd, fd1);
      close(io->fd);
    }
    execvp(compress_argv[0], (char**)compress_argv);
    die3sys(1, "Could not execute '", compress_argv[0], "'");
  default:
    close(io->fd);
    close(fds[fd0]);
    io->fd = fds[fd1];
  }
}

void compress_start(iobuf* io)
{
  compress_common(io, "-c", 0, 1);
}

void decompress_start(iobuf* io)
{
  compress_common(io, "-dc", 1, 0);
}

void compress_end(void)
{
  int status;
  if (opt_compress) {
    if (waitpid(compress_pid, &status, 0) != compress_pid)
      die1sys(1, "waitpid failed?");
    if (!WIFEXITED(status))
      die2(1, compress_argv[0], " crashed");
    if (WEXITSTATUS(status))
      die2(1, compress_argv[0], " failed");
  }
}
