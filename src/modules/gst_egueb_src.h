#ifndef GST_EGUEB_SRC_H
#define GST_EGUEB_SRC_H

#include <gst/gst.h>
#include <gst/base/gstadapter.h>
#include <gst/base/gstbasesrc.h>
#include <gst/interfaces/navigation.h>

#include <Egueb_Dom.h>
#include <Egueb_Smil.h>

#include "Gst_Egueb.h"

G_BEGIN_DECLS

#define GST_TYPE_EGUEB_SVG_SRC            (gst_egueb_src_get_type())
#define GST_EGUEB_SRC(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj),\
                                         GST_TYPE_EGUEB_SVG_SRC, GstEguebSrc))
#define GST_EGUEB_SRC_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass),\
                                         GST_TYPE_EGUEB_SVG_SRC, GstEguebSrcClass))
#define GST_EGUEB_SRC_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),\
                                         GST_TYPE_EGUEB_SVG_SRC, GstEguebSrcClass))
#define GST_IS_EGUEB_SRC(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj),\
                                         GST_TYPE_EGUEB_SVG_SRC))
#define GST_IS_EGUEB_SRC_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass),\
                                         GST_TYPE_EGUEB_SVG_SRC))
typedef struct _GstEguebSrc GstEguebSrc;
typedef struct _GstEguebSrcClass GstEguebSrcClass;

struct _GstEguebSrc
{
  GstBaseSrc parent;
  /* properties */
  GstBuffer *xml;
  guint container_w;
  guint container_h;
  gchar *location;
  /* private */
  Egueb_Dom_Node *doc;
  Egueb_Dom_Feature *render;
  Egueb_Dom_Feature *window;
  Egueb_Dom_Feature *animation;
  Egueb_Dom_Feature *io;

  Egueb_Dom_Input *input;

  Gst_Egueb_Document *gdoc;

  GMutex *doc_lock;
  Enesim_Surface *s;
  Enesim_Renderer *background;
  Eina_List *damages;
  gboolean done;

  guint w;
  guint h;
  gint spf_n;
  gint spf_d;

  gint64 last_stop;
  guint64 seek;
  guint64 last_ts;
  gint64 duration;
  gint64 fps;
};

struct _GstEguebSrcClass
{
  GstBaseSrcClass parent_class;
};

GType gst_egueb_src_get_type (void);

G_END_DECLS

#endif

