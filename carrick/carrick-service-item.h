/* carrick-service-item.h */

#ifndef _CARRICK_SERVICE_ITEM_H
#define _CARRICK_SERVICE_ITEM_H

#include <gtk/gtk.h>
#include <gconnman/gconnman.h>

G_BEGIN_DECLS

#define CARRICK_TYPE_SERVICE_ITEM carrick_service_item_get_type()

#define CARRICK_SERVICE_ITEM(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), \
  CARRICK_TYPE_SERVICE_ITEM, CarrickServiceItem))

#define CARRICK_SERVICE_ITEM_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), \
  CARRICK_TYPE_SERVICE_ITEM, CarrickServiceItemClass))

#define CARRICK_IS_SERVICE_ITEM(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), \
  CARRICK_TYPE_SERVICE_ITEM))

#define CARRICK_IS_SERVICE_ITEM_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), \
  CARRICK_TYPE_SERVICE_ITEM))

#define CARRICK_SERVICE_ITEM_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), \
  CARRICK_TYPE_SERVICE_ITEM, CarrickServiceItemClass))

typedef struct {
  GtkEventBox parent;
} CarrickServiceItem;

typedef struct {
  GtkEventBoxClass parent_class;
} CarrickServiceItemClass;

GType carrick_service_item_get_type (void);

GtkWidget* carrick_service_item_new (CmService *service);

G_END_DECLS

#endif /* _CARRICK_SERVICE_ITEM_H */