/* carrick-applet.c */

#include "carrick-applet.h"

#include <config.h>
#include <gtk/gtk.h>
#include <glib/gi18n.h>
#include <gconnman/gconnman.h>
#include <libnotify/notify.h>
#include <carrick/carrick-pane.h>
#include <carrick/carrick-status-icon.h>
#include <carrick/carrick-list.h>

G_DEFINE_TYPE (CarrickApplet, carrick_applet, G_TYPE_OBJECT)

#define GET_PRIVATE(o) \
  (G_TYPE_INSTANCE_GET_PRIVATE ((o), CARRICK_TYPE_APPLET, CarrickAppletPrivate))

typedef struct _CarrickAppletPrivate CarrickAppletPrivate;

struct _CarrickAppletPrivate {
  CmManager *manager;
  GtkWidget *icon;
  GtkWidget *pane;
  gchar     *state;
  gchar     *active_service_name;
  gchar     *active_service_type;
};

void
_notify_connection_changed (CarrickApplet *self)
{
  NotifyNotification *note;
  GError *error = NULL;
  gchar *title;
  gchar *message;
  gchar *icon; // filename
  CarrickAppletPrivate *priv = GET_PRIVATE (self);

  if (priv->state &&
      g_strcmp0 ("offline", priv->state) == 0)
  {
    title = g_strdup_printf (_("Offline"));
    message = g_strdup_printf (_("No active connection"));
  }
  else
  {
    title = g_strdup_printf (_("Active connection changed"));
    message = g_strdup_printf (_("Now connected to %s."),
                               priv->active_service_name);
  }

  icon = carrick_status_icon_get_icon_path (CARRICK_STATUS_ICON (priv->icon));

  note = notify_notification_new (title,
                                  message,
                                  icon,
                                  NULL);
  notify_notification_set_timeout (note,
                                   3000); // 3s

  notify_notification_show (note,
                            &error);
  if (error) {
    g_debug ("Error sending notification: %s",
             error->message);
  }
  g_object_unref (note);
}

static void
manager_state_changed_cb (CmManager *manager,
                          gpointer user_data)
{
  CarrickApplet *applet = CARRICK_APPLET (user_data);
  CarrickAppletPrivate *priv = GET_PRIVATE (applet);
  gchar *new_state = g_strdup (cm_manager_get_state (manager));
  gchar *new_name;
  gchar *new_type;

  if (!(g_strcmp0 (priv->state, new_state) == 0))
  {
    g_free (priv->state);
    priv->state = new_state;

    new_name = g_strdup (cm_manager_get_active_service_name (manager));
    if (!(g_strcmp0 (priv->active_service_name, new_name) == 0))
    {
      g_free (priv->active_service_name);
      priv->active_service_name = new_name;

      new_type = g_strdup (cm_manager_get_active_service_type (manager));
      if (!(g_strcmp0 (priv->active_service_type, new_type) == 0))
      {
        g_free (priv->active_service_type);
        priv->active_service_type = new_type;
      }
    }
  }

  /* FIXME: Update the whole UI */
  _notify_connection_changed (applet);
}

GtkWidget*
carrick_applet_get_pane (CarrickApplet *applet)
{
  CarrickAppletPrivate *priv = GET_PRIVATE (applet);

  return priv->pane;
}

GtkWidget*
carrick_applet_get_icon (CarrickApplet *applet)
{
  CarrickAppletPrivate *priv = GET_PRIVATE (applet);

  return priv->icon;
}

static void
carrick_applet_dispose (GObject *object)
{
  G_OBJECT_CLASS (carrick_applet_parent_class)->dispose (object);
}

static void
carrick_applet_finalize (GObject *object)
{
  G_OBJECT_CLASS (carrick_applet_parent_class)->finalize (object);
}

static void
carrick_applet_get_property (GObject    *object,
                             guint       property_id,
                             GValue     *value,
                             GParamSpec *pspec)
{
  switch (property_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
carrick_applet_set_property (GObject      *object,
                             guint         property_id,
                             const GValue *value,
                             GParamSpec   *pspec)
{
  switch (property_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
carrick_applet_class_init (CarrickAppletClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  g_type_class_add_private (klass, sizeof (CarrickAppletPrivate));

  object_class->dispose = carrick_applet_dispose;
  object_class->finalize = carrick_applet_finalize;
  object_class->get_property = carrick_applet_get_property;
  object_class->set_property = carrick_applet_set_property;
}

static void
carrick_applet_init (CarrickApplet *self)
{
  CarrickAppletPrivate *priv = GET_PRIVATE (self);
  GError *error = NULL;
  GtkWidget *scroll_view;

  notify_init ("Carrick");

  scroll_view = gtk_scrolled_window_new (NULL,
                                         NULL);
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scroll_view),
                                  GTK_POLICY_AUTOMATIC,
                                  GTK_POLICY_AUTOMATIC);

  priv->manager = cm_manager_new (&error);
  if (error) {
    g_debug ("Error initializing connman manager: %s\n",
             error->message);
    return;
  }
  cm_manager_refresh (priv->manager);
  priv->state = g_strdup (cm_manager_get_state (priv->manager));
  priv->active_service_name = g_strdup (cm_manager_get_active_service_name (priv->manager));
  if (!priv->active_service_name)
  {
    priv->active_service_name = g_strdup ("");
  }
  priv->active_service_type = g_strdup (cm_manager_get_active_service_type (priv->manager));
  priv->icon = carrick_status_icon_new (cm_manager_get_active_service (priv->manager));
  priv->pane = carrick_pane_new (priv->manager);

  g_signal_connect (priv->manager,
                    "state-changed",
                    G_CALLBACK (manager_state_changed_cb),
                    self);
}

CarrickApplet*
carrick_applet_new (void)
{
  return g_object_new (CARRICK_TYPE_APPLET, NULL);
}