/*
 * Command output plugin to lxpanel
 * This file is a part of lxPlugin project.
 *
 * Copyright (C) 2026 Ahmad <edu.siamak@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; If not, see <https://www.gnu.org/licenses/>.
 */
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <sys/wait.h>

#include "exec.h"

#define MIN_BUF_CAP (3)
#define BUF_CAP_RW(cmd) ((cmd)->buf_cap - 2)

/* calls: execvp with {shell, "-c", cmd} */
static inline void
__shell_exec (const char *shell, const char **args)
{
  if (! shell)
    shell = getenv ("SHELL");
  if (! shell)
    shell = "/bin/sh";
  if (! args[0])
    args[0] = shell;

  int ret = execvp (shell, (char * const*) args);
  _exit (ret); /* only reaches here if exec itself fails */
}

static void
__child (CMD *cmd, int pipefd[2])
{
  close (pipefd[0]);
  dup2 (pipefd[1], STDOUT_FILENO);
  if (cmd->redirect_stderr)
    dup2 (STDOUT_FILENO, STDERR_FILENO);
  else
    {
      int dnull = open ("/dev/null", O_WRONLY);
      if (dnull >= 0) /* can this fail btw? */
        {
          dup2 (dnull, STDERR_FILENO);
          close (dnull);
        }
    }
  // close (pipefd[1]); 
  __shell_exec (cmd->execvp_bin, cmd->args);
}

static int
__parent (CMD *cmd, int pid, int pipefd[2], struct timeval *tvout)
{
  fd_set readfds;
  int rw = 0, ret = ERR_NO_RESULT;

  close (pipefd[1]);
  for (int rd = 0; rw < cmd->max_output_len; rw += rd)
    {
      FD_ZERO (&readfds);
      FD_SET (pipefd[0], &readfds);
      if (select (pipefd[0] + 1, &readfds, NULL, NULL, tvout) <= 0)
        {
          kill (pid, SIGKILL);
          if (0 == rw)
            ret = ERR_TIMEOUT_NO_RESULT;
          goto end;
        }
      int max_read = cmd->max_output_len;
      if (rw < cmd->max_output_len)
        max_read -= rw;
      rd = read (pipefd[0], cmd->buf+rw, max_read);
      if (rd <= 0)
        break;
      ret = EXIT_SUCCESS;
    }

 end:
  if (rw && (cmd->buf[rw-1] == '\n'))
    cmd->buf[rw-1] = '\0';
  else
    cmd->buf[rw] = '%', cmd->buf[rw+1] = '\0';

  close (pipefd[0]);
  wait (&cmd->child_status);
  return ret;
}

void
exec_init (CMD *cmd)
{
  if (cmd->buf_cap <= MIN_BUF_CAP)
    cmd->buf_cap = DEFAULT_BUF_CAP;
  if (cmd->max_output_len <= 0 || cmd->max_output_len >= BUF_CAP_RW(cmd))
    cmd->max_output_len = BUF_CAP_RW(cmd);
  if (! cmd->buf)
    cmd->buf = malloc (cmd->buf_cap);
}

int
exec_run (CMD *cmd)
{
  pid_t pid;
  int pipefd[2];
  struct timeval tv;

  if (cmd->timeout_ms > 0)
    {
      tv.tv_sec = cmd->timeout_ms / 1000;
      tv.tv_usec = (cmd->timeout_ms % 1000) * 1000;
    }
  else
    tv.tv_sec = 1; /* apply 1s timeout */
  
  if (pipe (pipefd) < 0)
    return ERR_PIPE_CREATE;
  pid = fork ();
  if (pid < 0)
    return ERR_FORK_CALL;

  if (0 == pid) /* executing. */
    {
      __child (cmd, pipefd);
      return -1; /* unreachable */
    }
  else /* collecting output. */
    return __parent (cmd, pid, pipefd, &tv);
}

char *
exec_tr (CMD *cmd, const int from, const int to)
{
  for (int i=0; i < cmd->buf_cap && cmd->buf[i] != '\0'; ++i)
    {
      if (from == cmd->buf[i])
        cmd->buf[i] = to;
    }
  return cmd->buf;
}
