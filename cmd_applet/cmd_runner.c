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
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#include <glib/gi18n.h>
#include <lxpanel/plugin.h>

#include "exec.h"

#define MIN_OUTPUT_LENGTH (3) // utf-8 characters
#define MAX_OUTPUT_LENGTH DEFAULT_BUF_CAP

#ifndef DEFAULT_OUTPUT_LENGTH
#define DEFAULT_OUTPUT_LENGTH (42)
#endif

#define MIN_UPDATE_INTERVAL (1) // 1(s)
#define MAX_UPDATE_INTERVAL (60 * 60) // 1(H)

#ifndef EXEC_TIMEOUT
#define EXEC_TIMEOUT (1000 * 10)  // milliseconds (10s)
#endif
#ifndef DEFAULT_UPDATE_INTERVAL
#define DEFAULT_UPDATE_INTERVAL (1)
#endif

#ifndef DEFAULT_CMD
#define DEFAULT_CMD "echo \\[cmd runner\\]"
#endif
#ifndef EXEC_FAILED
#define EXEC_FAILED " ??? "
#endif

typedef struct /* Main applet descriptor */
{
  int bold_text;
  int center_text;
  int run_with_shell;
  int refresh_interval;

  char *cmd;
  const char *shell; /* shell or the program name */

  /* internal */
  guint timer;
  int progress; // waiting for command output
  LXPanel *panel;
  GtkWidget *gtext; // output of command
  GtkWidget *plugin; // top level widget
  config_setting_t *settings;

  pthread_t runner_thread;
  pthread_mutex_t runner_mutex;
  pthread_cond_t runner_cond;
  enum {
    RUNT_IDLE = 0,    // nothing to do
    RUNT_READY,       // there is a command to execute
    RUNT_RUNNING,     // middle of running the command
    RUNT_DONE,        // result is ready
  } runner_state;

  /* command runner. */
  CMD exec_cmd;
  int exec_ret;
  char *exec_res;
} Cmdapp;

static gboolean cmdp_update (Cmdapp *cmdp);
static void cmdp_destructor (gpointer user_data);
static GtkWidget *cmdp_constructor (LXPanel *panel, config_setting_t *settings);
static gboolean cmdp_apply_configuration (gpointer user_data);
static void cmdp_on_panel_reconfigured (LXPanel *panel, GtkWidget *p);
static GtkWidget *cmdp_configure (LXPanel *panel, GtkWidget *p);
static void load_from_cfg (Cmdapp *cmdp);

static inline void *
g_restrdup (void *restrict gptr, const char *restrict src)
{
  if (gptr) g_free (gptr);
  return g_strdup (src);
}

static void
cmdp_destructor (gpointer user_data)
{
  Cmdapp *cmdp = user_data;
  if (cmdp->timer != 0)
    g_source_remove (cmdp->timer);

  /* Terminate & Cleanup thread stuff */
  pthread_cancel (cmdp->runner_thread);
  pthread_join (cmdp->runner_thread, NULL);
  pthread_cond_destroy (&cmdp->runner_cond);
  pthread_mutex_destroy (&cmdp->runner_mutex);

  /* Deallocate all memory. */
  g_free (cmdp->exec_cmd.args);
  g_free (cmdp->cmd);
  g_free (cmdp);
}

void *
runner_th (void *_ptr)
{
  Cmdapp *cmdp = (Cmdapp *) _ptr;
  /*
   * Do not call any lxxx_draw_yyy() function here,
   * drawing on the panel using this function (form another thread),
   * will make the GUI of the panel frozen after some time.
   */
  while (1)
    {
      /* going to bed until cmdp_update() wakes us up */
      while (cmdp->runner_state != RUNT_READY)
        pthread_cond_wait (&cmdp->runner_cond, &cmdp->runner_mutex);

      cmdp->runner_state = RUNT_RUNNING;
      cmdp->exec_ret = exec_run (&cmdp->exec_cmd); // blocking.
      cmdp->exec_res = exec_tr (&cmdp->exec_cmd, '\n', ' ');
      cmdp->runner_state = RUNT_DONE;
    }

  pthread_exit (NULL);
}

/* draw a little progress-bar like: [**  ], ... to, [  **] */
static void
draw_progress (Cmdapp *cmdp)
{
  const int positions[4] = {0, 1, 2, 1};
  {
    char tmp[] = " [    ] ";
    int star = positions[cmdp->progress];
    tmp[star + 2] = '*';
    tmp[star + 3] = '*';
    lxpanel_draw_label_text (cmdp->panel, cmdp->gtext, tmp, cmdp->bold_text, 1, TRUE);
  }
  cmdp->progress = (cmdp->progress + 1) % 4;
}

static gboolean
cmdp_update (Cmdapp *cmdp)
{
  gchar *res_utf8;

  if (g_source_is_destroyed (g_main_current_source ()))
    return FALSE;
  cmdp->timer = g_timeout_add (cmdp->refresh_interval * 1000,
                               (GSourceFunc) cmdp_update, (gpointer) cmdp);

  switch (cmdp->runner_state)
    {
    case RUNT_RUNNING:
      if (! cmdp->exec_res)
        draw_progress (cmdp);
      break;
    case RUNT_READY: // unreachable.
      break;

    case RUNT_DONE:
      if (0 != cmdp->exec_ret)
        lxpanel_draw_label_text (cmdp->panel, cmdp->gtext, EXEC_FAILED,
                                 cmdp->bold_text, 1, TRUE);
      else /* exec() success */
        {
          if ((res_utf8 = g_locale_to_utf8 (cmdp->exec_res, -1, NULL, NULL, NULL)))
            {
              lxpanel_draw_label_text (cmdp->panel, cmdp->gtext, res_utf8,
                                       cmdp->bold_text, 1, TRUE);
              g_free (res_utf8);
            }
        }
      cmdp->runner_state = RUNT_READY;
      pthread_cond_signal (&cmdp->runner_cond);
      break;

    case RUNT_IDLE:
      draw_progress (cmdp);
      cmdp->runner_state = RUNT_READY;
      pthread_cond_signal (&cmdp->runner_cond);
      break;
    }

  return FALSE;
}

static void
ensure_cfg (Cmdapp *cmdp)
{
  if (! cmdp->cmd || '\0' == cmdp->cmd[0])
    {
      cmdp->run_with_shell = true;
      cmdp->cmd = g_restrdup (cmdp->cmd, DEFAULT_CMD);
    }
  if (0 == cmdp->exec_cmd.timeout_ms)
    cmdp->exec_cmd.timeout_ms = EXEC_TIMEOUT;
  if (MIN_OUTPUT_LENGTH >= cmdp->exec_cmd.max_output_len ||
      MAX_OUTPUT_LENGTH <= cmdp->exec_cmd.max_output_len)
    cmdp->exec_cmd.max_output_len = DEFAULT_OUTPUT_LENGTH;
  if (MIN_UPDATE_INTERVAL >= cmdp->refresh_interval)
    cmdp->refresh_interval = DEFAULT_UPDATE_INTERVAL;

  /* our execvp() call will have either 2 or 4 args. */
  if (! cmdp->exec_cmd.args)
    cmdp->exec_cmd.args = g_new0 (const char *, 4);
  if (cmdp->run_with_shell)
    { /* 4 args. */
      cmdp->exec_cmd.execvp_bin = cmdp->shell;
      cmdp->exec_cmd.args[0] = cmdp->shell;
      cmdp->exec_cmd.args[1] = "-c";
      cmdp->exec_cmd.args[2] = cmdp->cmd;
      cmdp->exec_cmd.args[3] = NULL;
    }
  else /* 2 args. */
    {
      cmdp->exec_cmd.execvp_bin = cmdp->cmd;
      cmdp->exec_cmd.args[0] = cmdp->cmd;
      cmdp->exec_cmd.args[1] = NULL;
    }
}

/* Plugin constructor */
static GtkWidget *
cmdp_constructor (LXPanel *panel, config_setting_t *settings)
{
  GtkWidget *p;
  Cmdapp * cmdp = g_new0 (Cmdapp, 1);
  cmdp->panel = panel;
  cmdp->settings = settings;

  cmdp->plugin = p = gtk_event_box_new ();
  lxpanel_plugin_set_data (p, cmdp, cmdp_destructor);

  load_from_cfg (cmdp);
  ensure_cfg (cmdp);
  exec_init (&cmdp->exec_cmd);

  /* Put the label inside an event box so it can handle clicks
     (do we really need this?) */
  gtk_widget_set_has_window (p, FALSE);
  gtk_container_set_border_width (GTK_CONTAINER(p), 1);
  {
    GtkWidget * hbox = gtk_hbox_new (TRUE, 0);
    gtk_container_add (GTK_CONTAINER(p), hbox);
    gtk_widget_show (hbox);
    cmdp->gtext = gtk_label_new (NULL);
    gtk_misc_set_alignment (GTK_MISC(cmdp->gtext), 0.5, 0.5);
    gtk_misc_set_padding (GTK_MISC(cmdp->gtext), 4, 0);
    gtk_container_add (GTK_CONTAINER(hbox), cmdp->gtext);
  }
  gtk_widget_show_all (p);

  cmdp->runner_state = RUNT_IDLE;
  pthread_cond_init (&cmdp->runner_cond, NULL);
  pthread_mutex_init(&cmdp->runner_mutex, NULL);
  pthread_create (&cmdp->runner_thread, NULL, runner_th, cmdp);
  cmdp->timer = g_idle_add ((GSourceFunc) cmdp_update, cmdp);

  return p;
}

static gboolean
cmdp_apply_configuration (gpointer user_data)
{
  GtkWidget * p = user_data;
  Cmdapp *cmdp = lxpanel_plugin_get_data(p);
  ensure_cfg (cmdp);

  /* stop the updater now */
  if (cmdp->timer)
    g_source_remove (cmdp->timer);

  if (cmdp->center_text)
    gtk_label_set_justify (GTK_LABEL(cmdp->gtext), GTK_JUSTIFY_CENTER);
  else
    gtk_label_set_justify (GTK_LABEL(cmdp->gtext), GTK_JUSTIFY_LEFT);

  if (cmdp->exec_cmd.buf)
    cmdp->exec_cmd.buf[0] = '\0';
  cmdp->exec_res = NULL;

  /* Save configuration */
  config_group_set_string (cmdp->settings, "Command", cmdp->cmd);
  config_group_set_int (   cmdp->settings, "BoldFont", cmdp->bold_text);
  config_group_set_int (   cmdp->settings, "CenterText", cmdp->center_text);
  config_group_set_int (   cmdp->settings, "WithShell", cmdp->run_with_shell);
  config_group_set_int (   cmdp->settings, "MaxLength", cmdp->exec_cmd.max_output_len);
  config_group_set_int (   cmdp->settings, "ShowStderr", cmdp->exec_cmd.redirect_stderr);
  config_group_set_int (   cmdp->settings, "UpdateInterval", cmdp->refresh_interval);  

  /* spawn the updater again. */
  pthread_cancel (cmdp->runner_thread);
  pthread_join (cmdp->runner_thread, NULL);
  cmdp->runner_state = RUNT_IDLE;
  pthread_create (&cmdp->runner_thread, NULL, runner_th, cmdp);
  cmdp->timer = g_idle_add ((GSourceFunc) cmdp_update, cmdp);
  return FALSE;
}

static void
load_from_cfg (Cmdapp *cmdp)
{
  int tmp_int;
  const char *tmp_str;

  if (config_setting_lookup_string (cmdp->settings, "Command", &tmp_str))
    cmdp->cmd = g_restrdup (cmdp->cmd, tmp_str);
  if (config_setting_lookup_int (   cmdp->settings, "BoldFont", &tmp_int))
    cmdp->bold_text = (tmp_int != 0);
  if (config_setting_lookup_int (   cmdp->settings, "CenterText", &tmp_int))
    cmdp->center_text = (tmp_int != 0);
  if (config_setting_lookup_int (   cmdp->settings, "WithShell", &tmp_int))
    cmdp->run_with_shell = (tmp_int != 0);
  if (config_setting_lookup_int (   cmdp->settings, "MaxLength", &tmp_int))
    cmdp->exec_cmd.max_output_len = (tmp_int >= 0) ? tmp_int : DEFAULT_OUTPUT_LENGTH;
  if (config_setting_lookup_int (   cmdp->settings, "ShowStderr", &tmp_int))
    cmdp->exec_cmd.redirect_stderr = (tmp_int != 0);
  if (config_setting_lookup_int (   cmdp->settings, "UpdateInterval", &tmp_int))
    cmdp->refresh_interval = (tmp_int > 0) ? tmp_int : DEFAULT_UPDATE_INTERVAL;
}

static GtkWidget *
cmdp_configure (LXPanel *panel, GtkWidget *p)
{
  Cmdapp *cmdp = lxpanel_plugin_get_data (p);
  GtkWidget *res = lxpanel_generic_config_dlg (_("Command output applet"), panel,
                                               cmdp_apply_configuration, p,
        _("Command"), &cmdp->cmd, CONF_TYPE_STR,
        _("Bold font"), &cmdp->bold_text, CONF_TYPE_BOOL,
        _("Center text"), &cmdp->center_text, CONF_TYPE_BOOL,
        _("Include stderr"), &cmdp->exec_cmd.redirect_stderr, CONF_TYPE_BOOL,
        _("Run with shell"), &cmdp->run_with_shell, CONF_TYPE_BOOL,
            _("run the given command with the default system shell;"
              " or as a system command;"), NULL, CONF_TYPE_TRIM,
        _("Maximum length"), &cmdp->exec_cmd.max_output_len, CONF_TYPE_INT,
            _("set it zero to get the maximum allowed length"), NULL, CONF_TYPE_TRIM,
        _("Update interval (s)"), &cmdp->refresh_interval, CONF_TYPE_INT,
        NULL);
  return res;
}

static void
cmdp_on_panel_reconfigured (LXPanel *panel, GtkWidget *p)
{
  (void) panel;
  cmdp_apply_configuration(p);
}

/* Plugin descriptor */
__attribute__ ((visibility("default")))
LXPanelPluginInit fm_module_init_lxpanel_gtk =
  {
    .name = "Command output",
    .description = "Display output of system commands",
    .one_per_system = 0, /* Allow multiple instances */

    .new_instance        = cmdp_constructor,
    .config              = cmdp_configure,
    .reconfigure         = cmdp_on_panel_reconfigured,
  };
FM_DEFINE_MODULE (lxpanel_gtk, cmd_runner);
