#ifndef GST_EGUEB_SVG_H
#define GST_EGUEB_SVG_H

#include <gst/gst.h>

G_BEGIN_DECLS

#define GST_TYPE_EGUEB_SVG            (gst_egueb_svg_get_type())
#define GST_EGUEB_SVG(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj),\
                                         GST_TYPE_EGUEB_SVG, GstEguebSvg))
#define GST_EGUEB_SVG_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass),\
                                         GST_TYPE_EGUEB_SVG, GstEguebSvgClass))
#define GST_EGUEB_SVG_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),\
                                         GST_TYPE_EGUEB_SVG, GstEguebSvgClass))
#define GST_IS_EGUEB_SVG(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj),\
                                         GST_TYPE_EGUEB_SVG))
#define GST_IS_EGUEB_SVG_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass),\
                                         GST_TYPE_EGUEB_SVG))
typedef struct _GstEguebSvg GstEguebSvg;
typedef struct _GstEguebSvgClass GstEguebSvgClass;

struct _GstEguebSvg
{
  GstBin parent;
  GstElement *sink;
  GstElement *src;
  GstBuffer *xml;
  guint w;
  guint h;
};

struct _GstEguebSvgClass
{
  GstBinClass parent_class;
  void (*handle_message) (GstBin * bin, GstMessage * message);
};

GType gst_egueb_svg_get_type (void);

G_END_DECLS

#endif

