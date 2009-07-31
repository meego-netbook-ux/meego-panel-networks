/*
 * Carrick - a connection panel for the Moblin Netbook
 * Copyright (C) 2009 Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 *
 * Written by - Joshua Lock <josh@linux.intel.com>
 *
 */

#include "carrick-status-icon.h"

#include <config.h>
#include <gconnman/gconnman.h>
#include "carrick-icon-factory.h"

G_DEFINE_TYPE (CarrickStatusIcon, carrick_status_icon, GTK_TYPE_STATUS_ICON)

#define GET_PRIVATE(o) \
  (G_TYPE_INSTANCE_GET_PRIVATE ((o), CARRICK_TYPE_STATUS_ICON, CarrickStatusIconPrivate))

typedef struct _CarrickStatusIconPrivate CarrickStatusIconPrivate;

struct _CarrickStatusIconPrivate {
  CmManager          *manager;
  CarrickIconFactory *icon_factory;
  gboolean            active;
};

enum
{
  PROP_0,
  PROP_ICON_FACTORY,
  PROP_MANAGER
};

/* Forward declaration of private method */
static void
carrick_status_icon_update_manager (CarrickStatusIcon *icon,
                                    CmManager         *manager);
static void
carrick_status_icon_update (CarrickStatusIcon *icon);

static void
carrick_status_icon_get_property (GObject *object, guint property_id,
                                  GValue *value, GParamSpec *pspec)
{
  CarrickStatusIconPrivate *priv = GET_PRIVATE (object);

  switch (property_id) {
    case PROP_ICON_FACTORY:
      g_value_set_object (value,
                          priv->icon_factory);
      break;
    case PROP_MANAGER:
      g_value_set_object (value,
                          priv->manager);
      break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
carrick_status_icon_set_property (GObject *object, guint property_id,
                                  const GValue *value, GParamSpec *pspec)
{
  CarrickStatusIcon *icon = (CarrickStatusIcon *)object;
  CarrickStatusIconPrivate *priv = GET_PRIVATE (icon);
  CmManager *manager;

  switch (property_id) {
    case PROP_ICON_FACTORY:
      priv->icon_factory = CARRICK_ICON_FACTORY (g_value_get_object (value));
      break;
    case PROP_MANAGER:
      manager = CM_MANAGER (g_value_get_object (value));
      carrick_status_icon_update_manager (icon,
                                          manager);
      break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
carrick_status_icon_dispose (GObject *object)
{
  carrick_status_icon_update_manager (CARRICK_STATUS_ICON (object),
                                      NULL);

  G_OBJECT_CLASS (carrick_status_icon_parent_class)->dispose (object);
}

static void
carrick_status_icon_finalize (GObject *object)
{
  G_OBJECT_CLASS (carrick_status_icon_parent_class)->finalize (object);
}

static void
carrick_status_icon_class_init (CarrickStatusIconClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GParamSpec *pspec;

  g_type_class_add_private (klass, sizeof (CarrickStatusIconPrivate));

  object_class->get_property = carrick_status_icon_get_property;
  object_class->set_property = carrick_status_icon_set_property;
  object_class->dispose = carrick_status_icon_dispose;
  object_class->finalize = carrick_status_icon_finalize;

  pspec = g_param_spec_object ("icon-factory",
                               "Icon factory.",
                               "The icon factory",
                               CARRICK_TYPE_ICON_FACTORY,
                               G_PARAM_READWRITE | G_PARAM_CONSTRUCT);
  g_object_class_install_property (object_class,
                                   PROP_ICON_FACTORY,
                                   pspec);

  pspec = g_param_spec_object ("manager",
                               "Manager.",
                               "The gconnman CmManager to represent.",
                               CM_TYPE_MANAGER,
                               G_PARAM_READWRITE | G_PARAM_CONSTRUCT);
  g_object_class_install_property (object_class,
                                   PROP_MANAGER,
                                   pspec);
}

static void
carrick_status_icon_init (CarrickStatusIcon *self)
{
  gtk_status_icon_set_from_stock (GTK_STATUS_ICON (self),
                                  GTK_STOCK_NETWORK);
}

static void
_service_updated_cb (CmService         *service,
                     CarrickStatusIcon *icon)
{
  g_signal_handlers_disconnect_by_func (service,
                                        _service_updated_cb,
                                        icon);
  carrick_status_icon_update (icon);
}

static void
carrick_status_icon_update (CarrickStatusIcon *icon)
{
  CarrickStatusIconPrivate *priv = GET_PRIVATE (icon);
  CarrickIconState icon_state = ICON_OFFLINE;
  GdkPixbuf *pixbuf;
  guint strength;
  const gchar *type = NULL;
  CmService *service = NULL;
  const GList *services = NULL;

  if (priv->manager)
  {
    services = cm_manager_get_services (priv->manager);
    if (services)
      service  = services->data;
  }

  if (service)
  {
    type = cm_service_get_type (service);

    if (g_strcmp0 (type, "ethernet") == 0)
    {
        icon_state = ICON_ACTIVE;
    }
    else if (g_strcmp0 (type, "wifi") == 0)
    {
        strength = cm_service_get_strength (service);
        if (strength > 70)
        {
          icon_state = ICON_WIRELESS_STRONG;
        }
        else if (strength > 35)
        {
          icon_state = ICON_WIRELESS_GOOD;
        }
        else
        {
          icon_state = ICON_WIRELESS_WEAK;
        }
    }
    else if (g_strcmp0 (type, "wimax") == 0)
    {
      strength = cm_service_get_strength (service);
      if (strength > 50)
	icon_state = ICON_WIMAX_STRONG;
      else
	icon_state = ICON_WIMAX_WEAK;
    }
    else if (g_strcmp0 (type, "cellular") == 0)
    {
      strength = cm_service_get_strength (service);
      if (strength > 50)
	icon_state = ICON_3G_STRONG;
      else
	icon_state = ICON_3G_WEAK;
    }
    else
    {
        icon_state = ICON_ERROR;
        g_signal_connect (service,
                          "service-updated",
                          G_CALLBACK (_service_updated_cb),
                          icon);
    }

    /* We've now set the icon for the top service, if this service
     * is not connected we should set the icon to the offline one.
     */
    if (!cm_service_get_connected (service))
      icon_state = ICON_OFFLINE;
  }
  else
  {
    icon_state = ICON_OFFLINE;
  }

  if (priv->active)
    icon_state++;

  pixbuf = carrick_icon_factory_get_pixbuf_for_state (priv->icon_factory,
                                                      icon_state);

  gtk_status_icon_set_from_pixbuf (GTK_STATUS_ICON (icon),
                                   pixbuf);
}

void
carrick_status_icon_set_active (CarrickStatusIcon *icon,
                                gboolean           active)
{
  CarrickStatusIconPrivate *priv = GET_PRIVATE (icon);

  priv->active = active;
  carrick_status_icon_update (icon);
}

static void
_manager_change_cb (CmManager         *manager,
                   CarrickStatusIcon *icon)
{
  carrick_status_icon_update (icon);
}

static void
carrick_status_icon_update_manager (CarrickStatusIcon *icon,
                                    CmManager         *manager)
{
  CarrickStatusIconPrivate *priv = GET_PRIVATE (icon);

  if (priv->manager)
  {
    g_signal_handlers_disconnect_by_func (priv->manager,
                                          _manager_change_cb,
                                          icon);

    g_object_unref (priv->manager);
    priv->manager= NULL;
  }

  if (manager)
  {
    priv->manager= g_object_ref (manager);

    g_signal_connect (priv->manager,
                      "services-changed",
                      G_CALLBACK (_manager_change_cb),
                      icon);
    g_signal_connect (priv->manager,
                      "state-changed",
                      G_CALLBACK (_manager_change_cb),
                      icon);
  }

  carrick_status_icon_update (icon);
}

GtkWidget*
carrick_status_icon_new (CarrickIconFactory *factory,
                         CmManager          *manager)
{
  return g_object_new (CARRICK_TYPE_STATUS_ICON,
                       "icon-factory",
                       factory,
                       "manager",
                       manager,
                       NULL);
}
