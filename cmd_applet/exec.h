#ifndef EXEC_H__
#define EXEC_H__

#ifndef DEFAULT_BUF_CAP
#define DEFAULT_BUF_CAP 128
#endif

/* Return values of exec_run() on failure,
   EXIT_SUCCESS on success.  */
#define ERR_PIPE_CREATE (1)
#define ERR_FORK_CALL (2)
#define ERR_TIMEOUT_NO_RESULT (3)
#define ERR_NO_RESULT (4)

typedef struct
{
  /* first arg of execvp()
     to use the default shell, set it to NULL. */
  const char *execvp_bin;
  const char **args; /* second arg of execvp() */

  int redirect_stderr; /* redirect 2>&1 */
  int timeout_ms; /* milliseconds */
  int max_output_len; /* collect at most output bytes */
  int child_status; /* return code of the command */

  int buf_cap;
  char *buf;
} CMD;

// init
void exec_init (CMD *cmd);
#define exec_free(cmd) (free ((cmd)->buf))

// execute @sh_cmd.
int exec_run (CMD *cmd);

// Similar to the tr command for @cmd->buf buffer.
// returns pointer to exec_buf (output buffer).
char * exec_tr (CMD *cmd, const int from, const int to);

// exec output buffer. 
#define exec_output(cmd) ((cmd)->buf)

#endif /* EXEC_H__ */


/**
 **  Example
 **/
#ifdef TEST_EXEC
int
main (void)
{
  const char *shell = "bash";

  CMD cmd = {
    .timeout_ms = 1000,
    .max_output_len = 0,
    .redirect_stderr = true,

#if 1
    .execvp_bin = shell,
    .args = (const char*[]) {
      shell,
      "-c",
      "sleep 0.6; echo first echo;"
      "           echo second one >&2;"
      "sleep 999; echo You shall not see me.;",
      NULL, /* Do NOT forget End of Args. */
    },
#else
    .execvp_bin = "pwd",
    .args = (const char*[]) {"pwd", NULL},
#endif
  };

  exec_init (&cmd);
  int ret = exec_run (&cmd);
  if (EXIT_SUCCESS != ret)
    {
      printf ("== failed -- %s.\n", strerror (errno));
      return 1;
    }

  printf ("Output:  '%s'.\n"
          "Exit_Code: (%d).\n",
          exec_tr (&cmd, '\n', ' '),
          cmd.child_status);
  exec_free (&cmd);
  return 0;
}
#endif /* TEST_EXEC */
