#ifndef GST_EGUEB_SVG_SRC_H
#define GST_EGUEB_SVG_SRC_H

#include <gst/gst.h>
#include <gst/base/gstadapter.h>
#include <gst/base/gstbasesrc.h>
#include <gst/interfaces/navigation.h>

#include <Egueb_Svg.h>

G_BEGIN_DECLS

#define GST_TYPE_EGUEB_SVG_SRC            (gst_egueb_svg_src_get_type())
#define GST_EGUEB_SVG_SRC(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj),\
                                         GST_TYPE_EGUEB_SVG_SRC, GstEguebSvgSrc))
#define GST_EGUEB_SVG_SRC_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass),\
                                         GST_TYPE_EGUEB_SVG_SRC, GstEguebSvgSrcClass))
#define GST_EGUEB_SVG_SRC_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),\
                                         GST_TYPE_EGUEB_SVG_SRC, GstEguebSvgSrcClass))
#define GST_IS_EGUEB_SVG_SRC(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj),\
                                         GST_TYPE_EGUEB_SVG_SRC))
#define GST_IS_EGUEB_SVG_SRC_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass),\
                                         GST_TYPE_EGUEB_SVG_SRC))
typedef struct _GstEguebSvgSrc GstEguebSvgSrc;
typedef struct _GstEguebSvgSrcClass GstEguebSvgSrcClass;

struct _GstEguebSvgSrc
{
  GstBaseSrc parent;
  /* properties */
  GstBuffer *xml;
  guint default_w;
  guint default_h;
  gchar *location;
  /* private */
  Egueb_Dom_Node *doc;
  Egueb_Dom_Node *svg;
  GMutex *doc_lock;
  Enesim_Surface *s;
  Eina_List *damages;
  gboolean done;

  guint w;
  guint h;
  gint spf_n;
  gint spf_d;

  guint64 seek;
  guint64 last_ts;
  gint64 duration;
};

struct _GstEguebSvgSrcClass
{
  GstBaseSrcClass parent_class;
};

GType gst_egueb_svg_src_get_type (void);

G_END_DECLS

#endif

