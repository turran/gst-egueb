#ifndef GST_EGUEB_DEMUX_H
#define GST_EGUEB_DEMUX_H

#include <gst/gst.h>

G_BEGIN_DECLS

#define GST_TYPE_EGUEB_SVG            (gst_egueb_demux_get_type())
#define GST_EGUEB_DEMUX(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj),\
                                         GST_TYPE_EGUEB_SVG, GstEguebDemux))
#define GST_EGUEB_DEMUX_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass),\
                                         GST_TYPE_EGUEB_SVG, GstEguebDemuxClass))
#define GST_EGUEB_DEMUX_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),\
                                         GST_TYPE_EGUEB_SVG, GstEguebDemuxClass))
#define GST_IS_EGUEB_DEMUX(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj),\
                                         GST_TYPE_EGUEB_SVG))
#define GST_IS_EGUEB_DEMUX_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass),\
                                         GST_TYPE_EGUEB_SVG))
typedef struct _GstEguebDemux GstEguebDemux;
typedef struct _GstEguebDemuxClass GstEguebDemuxClass;

struct _GstEguebDemux
{
  GstBin parent;
  GstElement *sink;
  GstElement *src;
  GstBuffer *xml;
  guint w;
  guint h;
};

struct _GstEguebDemuxClass
{
  GstBinClass parent_class;
  void (*handle_message) (GstBin * bin, GstMessage * message);
};

GType gst_egueb_demux_get_type (void);

G_END_DECLS

#endif

