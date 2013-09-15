#include "gst_egueb_xml_sink.h"
#include "gst_egueb_type.h"
#include <string.h>

GST_DEBUG_CATEGORY_EXTERN (egueb_xml_sink_debug);
#define GST_CAT_DEFAULT egueb_xml_sink_debug

GST_BOILERPLATE (GstEguebXmlSink, gst_egueb_xml_sink, GstElement,
    GST_TYPE_ELEMENT);

/* Later, whenever egueb supports more than svg, we can
 * add more templates here
 */
static GstStaticPadTemplate gst_egueb_xml_sink_sink_factory =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (SVG_MIME)
    );

static GstElementDetails gst_egueb_xml_sink_details = {
  "Egueb XML Sink",
  "Sink",
  "Receives XML buffers and triggers a message when is complete",
  "<enesim-devel@googlegroups.com>",
};

enum
{
  PROP_0
  /* FILL ME */
};

static void
gst_egueb_xml_sink_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_egueb_xml_sink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstFlowReturn
gst_egueb_xml_sink_sink_chain (GstPad * pad, GstBuffer * buffer)
{
  GstEguebXmlSink *thiz;
  GstFlowReturn ret = GST_FLOW_OK;

  thiz = GST_EGUEB_XML_SINK (gst_pad_get_parent (pad));
  GST_DEBUG_OBJECT (thiz, "Received buffer");
  gst_adapter_push (thiz->adapter, gst_buffer_ref (buffer));
  gst_object_unref (thiz);

  return ret;
}

static GstStateChangeReturn
gst_egueb_xml_sink_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret;

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  return ret;
}

static gchar *
gst_egueb_xml_sink_location_get (GstPad * pad)
{
  GstObject *parent;
  gchar *uri = NULL;

  parent = gst_pad_get_parent (pad);
  if (!parent)
    return NULL;

  if (GST_IS_GHOST_PAD (parent)) {
    GstPad *peer;

    peer = gst_pad_get_peer (GST_PAD (parent));
    uri = gst_egueb_xml_sink_location_get (peer);
    gst_object_unref (peer);

  } else {
    GstElementFactory *f;

    f = gst_element_get_factory (GST_ELEMENT (parent));
    if (gst_element_factory_list_is_type (f, GST_ELEMENT_FACTORY_TYPE_SRC)) {

      /* try to get the location property */
      g_object_get (G_OBJECT (parent), "location", &uri, NULL);

    } else {
      GstPad *sink_pad;
      GstIterator *iter;

      /* iterate over the sink pads */
      iter = gst_element_iterate_sink_pads (GST_ELEMENT (parent));
      while (gst_iterator_next (iter,
              (gpointer) & sink_pad) != GST_ITERATOR_DONE) {
        GstPad *peer;

        peer = gst_pad_get_peer (sink_pad);
        uri = gst_egueb_xml_sink_location_get (peer);
        gst_object_unref (sink_pad);
        gst_object_unref (peer);
        if (uri)
          break;
      }
      gst_iterator_free (iter);
    }
  }
  gst_object_unref (parent);

  return uri;
}

static gboolean
gst_egueb_xml_sink_sink_event (GstPad * pad, GstEvent * event)
{
  GstEguebXmlSink *thiz;
  GstPad *peer;
  GstBuffer *buf;
  GstMessage *message;
  gint len;
  gchar *location;
  gchar *baselocation = NULL;

  thiz = GST_EGUEB_XML_SINK (gst_pad_get_parent (pad));

  /* TODO later we need to handle FLUSH_START to flush the adapter too */
  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_EOS:
      GST_DEBUG_OBJECT (thiz, "Received EOS");
      /* The EOS should come from upstream whenever the xml file
       * has been readed completely
       */
      len = gst_adapter_available (thiz->adapter);
      buf = gst_adapter_take_buffer (thiz->adapter, len);
      /* get the location */
      peer = gst_pad_get_peer (pad);
      location = gst_egueb_xml_sink_location_get (peer);
      gst_object_unref (peer);

      if (location) {
        char *slash;
        /* get the base url only */
        slash = strrchr (location, '/');
        baselocation = g_strndup (location, slash - location + 1);
      }

      message = gst_message_new_element (GST_OBJECT (thiz),
          gst_structure_new ("xml-received",
              "xml", GST_TYPE_BUFFER, buf,
              "base-location", G_TYPE_STRING, baselocation,
              NULL));
      gst_element_post_message (GST_ELEMENT_CAST (thiz), message);
      gst_event_unref (event);
      break;

    default:
      break;
  }

  gst_object_unref (thiz);

  return TRUE;
}


static void
gst_egueb_xml_sink_dispose (GObject * object)
{
  GstEguebXmlSink *thiz = GST_EGUEB_XML_SINK (object);

  GST_DEBUG_OBJECT (thiz, "Disposing XML sink");
  if (thiz->adapter) {
    g_object_unref (thiz->adapter);
    thiz->adapter = NULL;
  }
  GST_CALL_PARENT (G_OBJECT_CLASS, dispose, (object));
}

static void
gst_egueb_xml_sink_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_egueb_xml_sink_sink_factory));
  gst_element_class_set_details (element_class, &gst_egueb_xml_sink_details);
}

static void
gst_egueb_xml_sink_init (GstEguebXmlSink * thiz,
    GstEguebXmlSinkClass * g_class)
{
  GstPad *sinkpad;

  sinkpad = gst_pad_new_from_static_template (&gst_egueb_xml_sink_sink_factory,
      "sink");

  gst_pad_set_event_function (sinkpad,
      GST_DEBUG_FUNCPTR (gst_egueb_xml_sink_sink_event));
  gst_pad_set_chain_function (sinkpad,
      GST_DEBUG_FUNCPTR (gst_egueb_xml_sink_sink_chain));

  gst_element_add_pad (GST_ELEMENT (thiz), sinkpad);
  /* our internal members */
  thiz->adapter = gst_adapter_new ();
}

static void
gst_egueb_xml_sink_class_init (GstEguebXmlSinkClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);

  parent_class = g_type_class_peek_parent (klass);

  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_egueb_xml_sink_change_state);

  gobject_class->dispose = GST_DEBUG_FUNCPTR (gst_egueb_xml_sink_dispose);
  gobject_class->set_property =
      GST_DEBUG_FUNCPTR (gst_egueb_xml_sink_set_property);
  gobject_class->get_property =
      GST_DEBUG_FUNCPTR (gst_egueb_xml_sink_get_property);
}

