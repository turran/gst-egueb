#ifndef GST_EGUEB_XML_SINK_H
#define GST_EGUEB_XML_SINK_H

#include <gst/gst.h>
#include <gst/base/gstadapter.h>
#include <gst/base/gstbasesink.h>

G_BEGIN_DECLS

#define GST_TYPE_EGUEB_XML_SINK            (gst_egueb_xml_sink_get_type())
#define GST_EGUEB_XML_SINK(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj),\
                                         GST_TYPE_EGUEB_XML_SINK, GstEguebXmlSink))
#define GST_EGUEB_XML_SINK_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass),\
                                         GST_TYPE_EGUEB_XML_SINK, GstEguebXmlSinkClass))
#define GST_EGUEB_XML_SINK_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),\
                                         GST_TYPE_EGUEB_XML_SINK, GstEguebXmlSinkClass))
#define GST_IS_EGUEB_XML_SINK(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj),\
                                         GST_TYPE_EGUEB_XML_SINK))
#define GST_IS_EGUEB_XML_SINK_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass),\
                                         GST_TYPE_EGUEB_XML_SINK))
typedef struct _GstEguebXmlSink GstEguebXmlSink;
typedef struct _GstEguebXmlSinkClass GstEguebXmlSinkClass;

struct _GstEguebXmlSink
{
  GstElement parent;
  GstAdapter *adapter;
};

struct _GstEguebXmlSinkClass
{
  GstElementClass parent_class;
};

GType gst_egueb_xml_get_type (void);

G_END_DECLS

#endif

