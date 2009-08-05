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

#include "carrick-network-model.h"

#include <config.h>
#include <dbus/dbus.h>

G_DEFINE_TYPE (CarrickNetworkModel, carrick_network_model, GTK_TYPE_LIST_STORE)

#define NETWORK_MODEL_PRIVATE(o) \
  (G_TYPE_INSTANCE_GET_PRIVATE ((o), CARRICK_TYPE_NETWORK_MODEL, CarrickNetworkModelPrivate))

struct _CarrickNetworkModelPrivate
{
  DBusGConnection *connection;
  DBusGProxy *manager;
  GSList *services;
};

enum
{
  PROP_0,
  PROP_CONNECTION,
  PROP_MANAGER,
};

enum
{
  SIGNAL_LOADED,
  NUM_SIGNALS
};

static guint signals[NUM_SIGNALS];

/* forward declaration of private methods */
static gint network_model_sort_cb (GtkTreeModel *self, GtkTreeIter *a, GtkTreeIter *b, gpointer user_data);

static void network_model_set_manager (CarrickNetworkModel *self, DBusGProxy *manager);

static void
carrick_network_model_get_property (GObject    *object,
				    guint       property_id,
                                    GValue     *value,
				    GParamSpec *pspec)
{
  CarrickNetworkModel *self = (CarrickNetworkModel *)object;
  CarrickNetworkModelPrivate *priv = self->priv;

  switch (property_id)
  {
  case PROP_MANAGER:
    g_value_set_object (value,
			priv->manager);
    break;
  case PROP_CONNECTION:
    g_value_set_object (value,
			priv->connection);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
carrick_network_model_set_property (GObject      *object,
				    guint         property_id,
				    const GValue *value,
				    GParamSpec   *pspec)
{
  CarrickNetworkModel *self = (CarrickNetworkModel *)object;
  CarrickNetworkModelPrivate *priv = self->priv;

  switch (property_id)
  {
  case PROP_MANAGER:
    network_model_set_manager (self,
			       DBUS_G_PROXY (g_value_get_object (value)));
    break;
  case PROP_CONNECTION:
    priv->connection = (DBusGConnection *) g_value_get_object (value);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
carrick_network_model_class_init (CarrickNetworkModelClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  g_type_class_add_private (klass, sizeof (CarrickNetworkModelPrivate));

  object_class->get_property = carrick_network_model_get_property;
  object_class->set_property = carrick_network_model_set_property;

  g_object_class_install_property (object_class,
                                   PROP_MANAGER,
                                   g_param_spec_object ("manager",
                                                        "manager",
                                                        "Proxy to the manager",
                                                        G_TYPE_OBJECT,
                                                        G_PARAM_READWRITE |
                                                        G_PARAM_CONSTRUCT));

  signals[SIGNAL_LOADED] = g_signal_new ("loaded",
                                         G_TYPE_FROM_CLASS (klass),
                                         G_SIGNAL_RUN_FIRST,
                                         0, NULL, NULL,
                                         g_cclosure_marshal_VOID__VOID,
                                         G_TYPE_NONE, 0);
}

static void
carrick_network_model_init (CarrickNetworkModel *self)
{
  GError *error = NULL;
  self->priv = NETWORK_MODEL_PRIVATE (self);
  self->priv->services = NULL;
  self->priv->manager = NULL;
  self->priv->connection = dbus_g_bus_get (DBUS_BUS_SYSTEM, &error);
  if (error)
  {
    g_debug ("Error connecting to bus: %s",
	     error->message);
    g_clear_error (&error);
    /* FIXME: Do better? */
  }

  const GType column_types[] = { G_TYPE_OBJECT, /* proxy */
                                 G_TYPE_UINT, /* index */
                                 G_TYPE_STRING, /* name */
                                 G_TYPE_STRING, /* type */
                                 G_TYPE_STRING, /* state */
                                 G_TYPE_BOOLEAN, /* favourite */
                                 G_TYPE_UINT, /* strength */
                                 G_TYPE_STRING, /* security */
                                 G_TYPE_BOOLEAN, /* passphrase required */
                                 G_TYPE_STRING /* passphrase */
  };

  gtk_list_store_set_column_types (GTK_LIST_STORE (self),
                                   G_N_ELEMENTS (column_types),
                                   (GType *) column_types);

  gtk_tree_sortable_set_default_sort_func (GTK_TREE_SORTABLE (self),
                                           network_model_sort_cb,
                                           NULL, NULL);

  gtk_tree_sortable_set_sort_column_id
    (GTK_TREE_SORTABLE (self),
     GTK_TREE_SORTABLE_DEFAULT_SORT_COLUMN_ID,
     GTK_SORT_DESCENDING);
}

static void
network_model_iterate_services (const GValue *value,
                                gpointer      user_data)
{
  GSList **services = user_data;
  gchar *path = g_value_dup_boxed (value);

  if (path != NULL)
    *services = g_slist_append (*services, path);
}

static gint
network_model_compare_path (gconstpointer a,
                            gconstpointer b)
{
  return g_strcmp0 (a, b);
}

static gboolean
network_model_have_service_in_store (GtkListStore *store,
                                     GtkTreeIter  *iter,
                                     const gchar  *path)
{
  gboolean cont, found = FALSE;
  DBusGProxy *service;

  cont = gtk_tree_model_get_iter_first (GTK_TREE_MODEL (store),
                                        iter);

  while (cont)
  {
    gtk_tree_model_get (GTK_TREE_MODEL (store),
                        iter,
                        CARRICK_COLUMN_PROXY, &service,
                        -1);

    if (!service)
      break;

    found = g_str_equal (path,
                         dbus_g_proxy_get_path (service));
    g_object_unref (service);

    if (found)
      break;

    cont = gtk_tree_model_iter_next (GTK_TREE_MODEL (store),
                                     iter);
  }

  return found;
}

  gchar *icon = NULL;
static gboolean
network_model_have_service_by_path (GtkListStore *store,
				    GtkTreeIter  *iter,
				    const gchar  *path)
{
  if (!path)
    return FALSE;

  return network_model_have_service_in_store (store, iter, path);
}

static gboolean
network_model_have_service_by_proxy (GtkListStore *store,
                                     GtkTreeIter  *iter,
                                     DBusGProxy   *service)
{
  const gchar *path;

  if (!service)
    return FALSE;

  path = dbus_g_proxy_get_path (service);

  return network_model_have_service_by_path (store, iter, path);
}

static void
network_model_service_get_properties_cb (DBusGProxy     *service,
                                         DBusGProxyCall *call,
                                         gpointer        user_data)
{
  CarrickNetworkModel *self = user_data;
  GtkListStore *store = GTK_LIST_STORE (self);
  GError *error = NULL;
  GHashTable *properties;
  guint strength = 0;
  const gchar *name = NULL;
  const gchar *state = NULL;
  const gchar *security = NULL;
  const gchar *type = NULL;
  gboolean favorite = FALSE;
  gboolean passphrase_required = FALSE;
  const gchar *passphrase = NULL;
  GValue *value;
  GtkTreeIter iter;

  dbus_g_proxy_end_call (service,
		         call,
			 &error,
			 dbus_g_type_get_map ("GHashTable",
				 G_TYPE_STRING,
				 G_TYPE_VALUE),
			 &properties,
			 G_TYPE_INVALID);

  if (error)
  {
    g_debug ("Error: couldn't end get properties call - %s",
	      error->message);
    g_clear_error (&error);
    /* FIXME: Do something here too */
  }

  value = g_hash_table_lookup (properties, "Name");
  if (value)
    name = g_value_get_string (value);
  else
    name = g_strdup ("");

  value = g_hash_table_lookup (properties, "Type");
  type = g_value_get_string (value);

  value = g_hash_table_lookup (properties, "State");
  state = g_value_get_string (value);

  value = g_hash_table_lookup (properties, "Favorite");
  favorite = g_value_get_boolean (value);

  value = g_hash_table_lookup (properties, "Strength");
  if (value)
    strength = g_value_get_uchar (value);

  value = g_hash_table_lookup (properties, "Security");
  if (value)
    security = g_value_get_string (value);

  value = g_hash_table_lookup (properties, "PassphraseRequired");
  if (value)
    passphrase_required = g_value_get_boolean (value);

  if (passphrase_required)
  {
    value = g_hash_table_lookup (properties, "Passphrase");
    if (value)
      passphrase = g_value_get_string (value);
  }

  if (network_model_have_service_by_proxy (store,
					   &iter,
					   service))
  {
    gtk_list_store_set (store, &iter,
			CARRICK_COLUMN_NAME, name,
			CARRICK_COLUMN_TYPE, type,
			CARRICK_COLUMN_STATE, state,
			CARRICK_COLUMN_FAVORITE, favorite,
			CARRICK_COLUMN_STRENGTH, strength,
			CARRICK_COLUMN_SECURITY, security,
			CARRICK_COLUMN_PASSPHRASE_REQUIRED, passphrase_required,
			CARRICK_COLUMN_PASSPHRASE, passphrase,
			-1);
  }
  else
  {
    gtk_list_store_insert_with_values
      (store, &iter, -1,
       CARRICK_COLUMN_PROXY, service,
       CARRICK_COLUMN_NAME, name,
       CARRICK_COLUMN_TYPE, type,
       CARRICK_COLUMN_STATE, state,
       CARRICK_COLUMN_FAVORITE, favorite,
       CARRICK_COLUMN_STRENGTH, strength,
       CARRICK_COLUMN_SECURITY, security,
       CARRICK_COLUMN_PASSPHRASE_REQUIRED, passphrase,
       CARRICK_COLUMN_PASSPHRASE, passphrase,
       -1);
  }
}

static void
network_model_service_changed_cb (DBusGProxy  *service,
				  const gchar *property,
				  GValue      *value,
				  gpointer     user_data)
{
  CarrickNetworkModel *self  = user_data;
  GtkListStore *store = GTK_LIST_STORE (self);
  GtkTreeIter iter;
  DBusGProxyCall *call;

  if (property == NULL || value == NULL)
    return;

  if (network_model_have_service_by_proxy (store, &iter, service) == FALSE)
    return;

  if (g_str_equal (property, "State"))
  {
    gtk_list_store_set (store, &iter,
			CARRICK_COLUMN_STATE, g_value_get_string (value),
			-1);
  }
  else if (g_str_equal (property, "Favorite"))
  {
    gtk_list_store_set (store, &iter,
			CARRICK_COLUMN_FAVORITE, g_value_get_boolean (value),
			-1);
  }
  else if (g_str_equal (property, "Strength"))
  {
    gtk_list_store_set (store, &iter,
			CARRICK_COLUMN_STRENGTH, g_value_get_uchar (value),
			-1);
  }
  else if (g_str_equal (property, "PassphraseRequired"))
  {
    /* Rather than store this property we're just going to trigger
     * GetProperties to pull the up-to-date passphrase
     */
    call = dbus_g_proxy_begin_call (service,
				    "GetProperties",
				    network_model_service_get_properties_cb,
				    self,
				    NULL,
				    G_TYPE_INVALID);
  }
}

static void
network_model_update_property (const gchar *property,
                               GValue      *value,
                               gpointer     user_data)
{
  CarrickNetworkModel *self = user_data;
  CarrickNetworkModelPrivate *priv = self->priv;
  GtkListStore *store = GTK_LIST_STORE (self);
  GSList *new_services = NULL;
  GSList *old_services = NULL;
  GSList *list_iter = NULL;
  GSList *tmp = NULL;
  GtkTreeIter iter;
  DBusGProxyCall *call = NULL;
  gchar *path = NULL;
  DBusGProxy *service;
  guint index = 0;

  if (g_str_equal (property, "Services"))
  {
    old_services = priv->services;

    dbus_g_type_collection_value_iterate (value,
					  network_model_iterate_services,
					  &new_services);

    priv->services = new_services;

    for (list_iter = new_services;
         list_iter != NULL;
         list_iter = list_iter->next)
    {
      path = list_iter->data;

      /* Remove from old list, if present.
       * We only want stale services in old list
       */
      tmp = g_slist_find_custom (old_services,
                                 path,
                                 network_model_compare_path);
      if (tmp)
      {
        old_services = g_slist_delete_link (old_services, tmp);
      }

      service = dbus_g_proxy_new_for_name (priv->connection,
                                           CONNMAN_SERVICE, path,
                                           CONNMAN_SERVICE_INTERFACE);

      /* if we don't have the service in the model, add it*/
      if (network_model_have_service_by_proxy (store, &iter, service) == FALSE)
      {
        gtk_list_store_insert_with_values (store, &iter, -1,
                                           CARRICK_COLUMN_PROXY, service,
                                           CARRICK_COLUMN_INDEX, index,
                                           -1);

        dbus_g_proxy_add_signal (service,
                                 "PropertyChanged",
                                 G_TYPE_STRING,
                                 G_TYPE_VALUE,
                                 G_TYPE_INVALID);

        dbus_g_proxy_connect_signal (service,
                                     "PropertyChanged",
                                     G_CALLBACK
                                     (network_model_service_changed_cb),
                                     self,
                                     NULL);

        call = dbus_g_proxy_begin_call (service,
                                        "GetProperties",
					network_model_service_get_properties_cb,
					self,
					NULL,
					G_TYPE_INVALID);
      }
      /* else update it */
      else
      {
        gtk_list_store_set (store, &iter,
                            CARRICK_COLUMN_INDEX, index,
                            -1);
      }


      index++;
    }

    /* Old list only contains items not in new list.
     * Remove stale services */
    for (list_iter = old_services;
         list_iter != NULL;
         list_iter = list_iter->next)
    {
      path = list_iter->data;

      if (network_model_have_service_by_path (store, &iter, path) == TRUE)
        gtk_list_store_remove (store, &iter);

      g_free (path);
    }
    g_slist_free (old_services);
  }
}

static gint
network_model_sort_cb (GtkTreeModel *model,
                       GtkTreeIter  *a,
                       GtkTreeIter  *b,
                       gpointer      user_data)
{
  guint ia, ib;

  gtk_tree_model_get (model, a, CARRICK_COLUMN_INDEX, &ia, -1);
  gtk_tree_model_get (model, b, CARRICK_COLUMN_INDEX, &ib, -1);

  return (gint) (ib - ia);
}

static void
network_model_manager_changed_cb (DBusGProxy  *proxy,
                                  const gchar *property,
                                  GValue      *value,
                                  gpointer     user_data)
{
  CarrickNetworkModel *self = user_data;

  network_model_update_property (property, value, self);
}

static void
network_model_manager_get_properties_cb (DBusGProxy     *manager,
                                         DBusGProxyCall *call,
                                         gpointer        user_data)
{
  CarrickNetworkModel *self = user_data;
  GError *error = NULL;
  GHashTable *properties;

  dbus_g_proxy_end_call (manager,
                         call,
			 &error,
			 dbus_g_type_get_map ("GHashTable",
				 G_TYPE_STRING,
				 G_TYPE_VALUE),
			 &properties,
			 G_TYPE_INVALID);
  if (error)
  {
    g_debug ("Error: Couldn't end get properties call - %s",
             error->message);
    g_clear_error (&error);
    /* FIXME: Do something here */
  }

  g_hash_table_foreach (properties,
                        (GHFunc) network_model_update_property,
                        self);
  g_hash_table_unref (properties);
}

static void
network_model_set_manager (CarrickNetworkModel *self,
                           DBusGProxy          *manager)
{
  CarrickNetworkModelPrivate *priv = self->priv;
  DBusGProxyCall *call;

  if (priv->manager)
  {
    dbus_g_proxy_disconnect_signal (priv->manager,
                                    "PropertyChanged",
                                    G_CALLBACK
                                    (network_model_manager_changed_cb),
                                    self);
    g_object_unref (priv->manager);
    priv->manager = NULL;
  }

  if (manager)
  {
    priv->manager = g_object_ref (manager);

    /* Connect to the Manager interface */
    dbus_g_proxy_add_signal (priv->manager,
                             "PropertyChanged",
                             G_TYPE_STRING,
                             G_TYPE_VALUE,
                             G_TYPE_INVALID);

    dbus_g_proxy_connect_signal (priv->manager,
                                 "PropertyChanged",
                                 G_CALLBACK (network_model_manager_changed_cb),
                                 self,
                                 NULL);

    call = dbus_g_proxy_begin_call (priv->manager,
                                    "GetProperties",
                                    network_model_manager_get_properties_cb,
                                    self,
                                    NULL,
                                    G_TYPE_INVALID);
  }
}

/*
 * Public methods
 */

GtkTreeModel *
carrick_network_model_new (DBusGProxy *manager)
{
  return g_object_new (CARRICK_TYPE_NETWORK_MODEL,
		       "manager",
                       manager,
                       NULL);
}
