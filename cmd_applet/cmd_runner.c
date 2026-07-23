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

#include <lxpanel/plugin.h>
#include <glib/gi18n.h>

#include "exec.h"

#ifndef EXEC_TIMEOUT
#define EXEC_TIMEOUT (1000 * 10)  // milliseconds (10s)
#endif

#define MIN_OUTPUT_LENGTH (3) // utf-8 characters
#define MAX_OUTPUT_LENGTH DEFAULT_BUF_CAP
#ifndef DEFAULT_OUTPUT_LENGTH
#define DEFAULT_OUTPUT_LENGTH (42)
#endif

#define MIN_UPDATE_INTERVAL (1) // 1(s)
#define MAX_UPDATE_INTERVAL (999)
#ifndef DEFAULT_UPDATE_INTERVAL
#define DEFAULT_UPDATE_INTERVAL (1)
#endif

typedef struct Cmdout
{
  int bold_text;
  int center_text;
  int run_with_shell;
  int refresh_interval;

  char *cmd;
  const char *shell; /* shell or the program name */

  /* internal */
  guint timer;
  LXPanel *panel;
  GtkWidget *gtext; // output of command
  GtkWidget *plugin; // top level widget
  config_setting_t *settings;

  pthread_t runner_thread;
  enum {
    RUNT_UNINITIALIZED = 0,
    RUNT_UNUSED,
    RUNT_RUNNING,
  } runner_state;

  /* command runner. */
  CMD exec_cmd;
} Cmdout;

static gboolean cmdo_update (Cmdout *cmdo);
static void cmdo_destructor (gpointer user_data);
static GtkWidget *cmdo_constructor (LXPanel *panel, config_setting_t *settings);
static gboolean cmdo_apply_configuration (gpointer user_data);
static void cmdo_on_panel_reconfigured (LXPanel *panel, GtkWidget *p);
static GtkWidget *cmdo_configure (LXPanel *panel, GtkWidget *p);
static void load_from_cfg (Cmdout *cmdo);

static inline void *
g_restrdup (void *restrict gptr, const char *restrict src)
{
  if (gptr) g_free (gptr);
  return g_strdup (src);
}

static void
cmdo_destructor (gpointer user_data)
{
  Cmdout *cmdo = user_data;
  if (cmdo->timer != 0)
    g_source_remove (cmdo->timer);

  /* Deallocate all memory. */
  g_free (cmdo);
  g_free (cmdo->exec_cmd.args);
}

void *
runner_th (void *_ptr)
{
  Cmdout *cmdo = (Cmdout *) _ptr;
  cmdo->runner_state = RUNT_RUNNING;
  {
    int ret = exec_run (&cmdo->exec_cmd);
    if (0 != ret)
      lxpanel_draw_label_text (cmdo->panel, cmdo->gtext, "???", FALSE, 1, TRUE);
    else
      {
        const char *res = exec_tr (&cmdo->exec_cmd, '\n', ' ');
        gchar *utf8 = g_locale_to_utf8 (res, -1, NULL, NULL, NULL);
        if (utf8)
          {
            lxpanel_draw_label_text (cmdo->panel, cmdo->gtext, utf8,
                                     cmdo->bold_text, 1, TRUE);
            g_free (utf8);
          }
      }
  }
  cmdo->runner_state = RUNT_UNUSED;
  pthread_exit (NULL);
}

static gboolean
cmdo_update (Cmdout *cmdo)
{
  if (g_source_is_destroyed (g_main_current_source ()))
    return FALSE;

  if (cmdo->runner_state == RUNT_UNINITIALIZED)
    lxpanel_draw_label_text (cmdo->panel, cmdo->gtext, "<-->", TRUE, 1, TRUE);

  if (cmdo->runner_state != RUNT_RUNNING)
    pthread_create (&cmdo->runner_thread, NULL, runner_th, cmdo);

  cmdo->timer = g_timeout_add (cmdo->refresh_interval * 1000,
                               (GSourceFunc) cmdo_update, (gpointer) cmdo);
  return FALSE;
}

static void
ensure_cfg (Cmdout *cmdo)
{
  if (! cmdo->cmd || '\0' == cmdo->cmd[0])
    {
      cmdo->run_with_shell = true;
      cmdo->cmd = g_restrdup (cmdo->cmd, DEFAULT_CMD);
    }
  if (0 == cmdo->exec_cmd.timeout_ms)
    cmdo->exec_cmd.timeout_ms = EXEC_TIMEOUT;
  if (MIN_OUTPUT_LENGTH >= cmdo->exec_cmd.max_output_len ||
      MAX_OUTPUT_LENGTH <= cmdo->exec_cmd.max_output_len)
    cmdo->exec_cmd.max_output_len = DEFAULT_OUTPUT_LENGTH;
  if (MIN_UPDATE_INTERVAL >= cmdo->refresh_interval)
    cmdo->refresh_interval = DEFAULT_UPDATE_INTERVAL;

  if (cmdo->run_with_shell)
    {
      cmdo->exec_cmd.execvp_bin = cmdo->shell;
      cmdo->exec_cmd.args[0] = cmdo->shell;
      cmdo->exec_cmd.args[1] = "-c";
      cmdo->exec_cmd.args[2] = cmdo->cmd;
      cmdo->exec_cmd.args[3] = NULL;
    }
  else
    {
      cmdo->exec_cmd.execvp_bin = cmdo->cmd;
      cmdo->exec_cmd.args[0] = cmdo->cmd;
      cmdo->exec_cmd.args[1] = NULL;
    }
}

static void
init_cmd_exec (Cmdout *cmdo)
{
  cmdo->exec_cmd.args = g_new0 (const char *, 4);
  ensure_cfg (cmdo);
  exec_init (&cmdo->exec_cmd);
}

/* Plugin constructor */
static GtkWidget *
cmdo_constructor (LXPanel *panel, config_setting_t *settings)
{
  GtkWidget *p;
  Cmdout * cmdo = g_new0 (Cmdout, 1);
  cmdo->panel = panel;
  cmdo->settings = settings;

  cmdo->plugin = p = gtk_event_box_new ();
  lxpanel_plugin_set_data (p, cmdo, cmdo_destructor);

  load_from_cfg (cmdo);
  init_cmd_exec (cmdo);

  /* Put the label inside an event box so it can handle clicks
     (do we really need this?) */
  gtk_widget_set_has_window (p, FALSE);
  gtk_container_set_border_width (GTK_CONTAINER(p), 1);
  {
    GtkWidget * hbox = gtk_hbox_new (TRUE, 0);
    gtk_container_add (GTK_CONTAINER(p), hbox);
    gtk_widget_show (hbox);
    cmdo->gtext = gtk_label_new (NULL);
    gtk_misc_set_alignment (GTK_MISC(cmdo->gtext), 0.5, 0.5);
    gtk_misc_set_padding (GTK_MISC(cmdo->gtext), 4, 0);
    gtk_container_add (GTK_CONTAINER(hbox), cmdo->gtext);
  }
  gtk_widget_show_all (p);

  // cmdo->timer = g_idle_add ((GSourceFunc) cmdo_update, cmdo);
  cmdo_update (cmdo);
  return p;
}

static gboolean
cmdo_apply_configuration (gpointer user_data)
{
  GtkWidget * p = user_data;
  Cmdout *cmdo = lxpanel_plugin_get_data(p);
  ensure_cfg (cmdo);

  /* stop the updater now */
  if (cmdo->timer)
    g_source_remove (cmdo->timer);

  if (cmdo->center_text)
    gtk_label_set_justify (GTK_LABEL(cmdo->gtext), GTK_JUSTIFY_CENTER);
  else
    gtk_label_set_justify (GTK_LABEL(cmdo->gtext), GTK_JUSTIFY_LEFT);

  if (cmdo->exec_cmd.buf)
    cmdo->exec_cmd.buf[0] = '\0';

  /* Save configuration */
  config_group_set_string (cmdo->settings, "Command", cmdo->cmd);
  config_group_set_int (   cmdo->settings, "BoldFont", cmdo->bold_text);
  config_group_set_int (   cmdo->settings, "CenterText", cmdo->center_text);
  config_group_set_int (   cmdo->settings, "WithShell", cmdo->run_with_shell);
  config_group_set_int (   cmdo->settings, "MaxLength", cmdo->exec_cmd.max_output_len);
  config_group_set_int (   cmdo->settings, "ShowStderr", cmdo->exec_cmd.redirect_stderr);
  config_group_set_int (   cmdo->settings, "UpdateInterval", cmdo->refresh_interval);  

  /* spawn the updater again. */
  if (cmdo->runner_state == RUNT_RUNNING)
    {
      pthread_cancel (cmdo->runner_thread);
      cmdo->runner_state = RUNT_UNUSED;
    }
  cmdo->timer = g_idle_add ((GSourceFunc) cmdo_update, cmdo);
  return FALSE;
}

static void
load_from_cfg (Cmdout *cmdo)
{
  int tmp_int;
  const char *tmp_str;

  if (config_setting_lookup_string (cmdo->settings, "Command", &tmp_str))
    cmdo->cmd = g_restrdup (cmdo->cmd, tmp_str);
  if (config_setting_lookup_int (   cmdo->settings, "BoldFont", &tmp_int))
    cmdo->bold_text = (tmp_int != 0);
  if (config_setting_lookup_int (   cmdo->settings, "CenterText", &tmp_int))
    cmdo->center_text = (tmp_int != 0);
  if (config_setting_lookup_int (   cmdo->settings, "WithShell", &tmp_int))
    cmdo->run_with_shell = (tmp_int != 0);
  if (config_setting_lookup_int (   cmdo->settings, "MaxLength", &tmp_int))
    cmdo->exec_cmd.max_output_len = (tmp_int >= 0) ? tmp_int : DEFAULT_OUTPUT_LENGTH;
  if (config_setting_lookup_int (   cmdo->settings, "ShowStderr", &tmp_int))
    cmdo->exec_cmd.redirect_stderr = (tmp_int != 0);
  if (config_setting_lookup_int (   cmdo->settings, "UpdateInterval", &tmp_int))
    cmdo->refresh_interval = (tmp_int > 0) ? tmp_int : DEFAULT_UPDATE_INTERVAL;
}

static GtkWidget *
cmdo_configure (LXPanel *panel, GtkWidget *p)
{
  Cmdout *cmdo = lxpanel_plugin_get_data (p);
  GtkWidget *res = lxpanel_generic_config_dlg (_("Command output applet"), panel,
                                               cmdo_apply_configuration, p,
        _("Command"), &cmdo->cmd, CONF_TYPE_STR,
        _("Bold font"), &cmdo->bold_text, CONF_TYPE_BOOL,
        _("Center text"), &cmdo->center_text, CONF_TYPE_BOOL,
        _("Include stderr"), &cmdo->exec_cmd.redirect_stderr, CONF_TYPE_BOOL,
        _("Run with shell"), &cmdo->run_with_shell, CONF_TYPE_BOOL,
            _("run the given command with the default system shell;"
              " or as a system command;"), NULL, CONF_TYPE_TRIM,
        _("Maximum length"), &cmdo->exec_cmd.max_output_len, CONF_TYPE_INT,
            _("set it zero to get the maximum allowed length"), NULL, CONF_TYPE_TRIM,
        _("Update interval (s)"), &cmdo->refresh_interval, CONF_TYPE_INT,
        NULL);

  ensure_cfg (cmdo);
  return res;
}

static void
cmdo_on_panel_reconfigured (LXPanel *panel, GtkWidget *p)
{
  (void) panel;
  cmdo_apply_configuration(p);
}

/* Plugin descriptor */
__attribute__ ((visibility("default")))
LXPanelPluginInit fm_module_init_lxpanel_gtk =
  {
    .name = "Command output",
    .description = "Display output of system commands",
    .one_per_system = 0, /* Allow multiple instances */

    .new_instance        = cmdo_constructor,
    .config              = cmdo_configure,
    .reconfigure         = cmdo_on_panel_reconfigured,
  };
FM_DEFINE_MODULE (lxpanel_gtk, cmd_runner);
