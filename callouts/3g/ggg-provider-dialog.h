#ifndef __GGG_PROVIDER_DIALOG_H__
#define __GGG_PROVIDER_DIALOG_H__

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define GGG_TYPE_PROVIDER_DIALOG (ggg_provider_dialog_get_type())
#define GGG_PROVIDER_DIALOG(obj)                                         \
   (G_TYPE_CHECK_INSTANCE_CAST ((obj),                                  \
                                GGG_TYPE_PROVIDER_DIALOG,                \
                                GggProviderDialog))
#define GGG_PROVIDER_DIALOG_CLASS(klass)                                 \
   (G_TYPE_CHECK_CLASS_CAST ((klass),                                   \
                             GGG_TYPE_PROVIDER_DIALOG,                   \
                             GggProviderDialogClass))
#define IS_GGG_PROVIDER_DIALOG(obj)                                      \
   (G_TYPE_CHECK_INSTANCE_TYPE ((obj),                                  \
                                GGG_TYPE_PROVIDER_DIALOG))
#define IS_GGG_PROVIDER_DIALOG_CLASS(klass)                              \
   (G_TYPE_CHECK_CLASS_TYPE ((klass),                                   \
                             GGG_TYPE_PROVIDER_DIALOG))
#define GGG_PROVIDER_DIALOG_GET_CLASS(obj)                               \
   (G_TYPE_INSTANCE_GET_CLASS ((obj),                                   \
                               GGG_TYPE_PROVIDER_DIALOG,                 \
                               GggProviderDialogClass))

typedef struct _GggProviderDialogPrivate GggProviderDialogPrivate;
typedef struct _GggProviderDialog      GggProviderDialog;
typedef struct _GggProviderDialogClass GggProviderDialogClass;

struct _GggProviderDialog {
  GtkDialog parent;
  GggProviderDialogPrivate *priv;
};

struct _GggProviderDialogClass {
  GtkDialogClass parent_class;
};

GType ggg_provider_dialog_get_type (void) G_GNUC_CONST;

RestXmlNode *ggg_provider_dialog_get_selected (GggProviderDialog *dialog);

G_END_DECLS

#endif /* __GGG_PROVIDER_DIALOG_H__ */
