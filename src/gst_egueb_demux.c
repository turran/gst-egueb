#include "gst_egueb_demux.h"
#include "gst_egueb_type.h"

GST_DEBUG_CATEGORY_EXTERN (gst_egueb_demux_debug);
#define GST_CAT_DEFAULT gst_egueb_demux_debug

/* The idea of this element is that it should produce image buffers
 * with the rendered svg per frame
 * as properties we might have:
 * - width/height The container size
 * - framerate The framerate of the svg
 * Should it work in pull and push mode?
 * It should use the enesim buffer instead of normal gstbuffers to create
 * the buffers
 */

static GstStaticPadTemplate gst_egueb_demux_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (SVG_MIME)
    );

static GstStaticPadTemplate gst_egueb_demux_src_video_template =
GST_STATIC_PAD_TEMPLATE ("video",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw-rgb, "
        "framerate = (fraction) [ 0, MAX ], "
        "depth = 24, bpp = 32, "
        "width = (int) [ 1, MAX ], height = (int) [ 1, MAX ]")
    );

static GstElementDetails gst_egueb_demux_details = {
  "Egueb SVG Parser/Demuxer/Decoder",
  "Codec/Demuxer",
  "Generates buffers with the SVG content rendered",
  "<enesim-devel@googlegroups.com>",
};

GST_BOILERPLATE (GstEguebDemux, gst_egueb_demux, GstBin, GST_TYPE_BIN);

static gboolean
gst_egueb_demux_handle_sink_message_sync (GstEguebDemux * thiz,
    GstMessage * message)
{
  const GstStructure *s;
  const GValue *value;
  const gchar *uri;
  GstBuffer *buffer;

  GST_DEBUG_OBJECT (thiz, "XML buffer received");
  s = gst_message_get_structure (message);
  value = gst_structure_get_value (s, "xml");
  buffer = gst_value_get_buffer (value);
  uri = gst_structure_get_string (s, "uri");

  /* now we have received the whole XML, is time to create our own
   * svg instance and start sending buffers!
   */
  g_object_set (G_OBJECT (thiz->src), "xml", buffer, "uri", uri, NULL);
  gst_element_set_locked_state (thiz->src, FALSE);
  gst_element_sync_state_with_parent (thiz->src);

  return TRUE;
}


static void
gst_egueb_demux_handle_message (GstBin * bin, GstMessage * message)
{
  GstEguebDemux *thiz = GST_EGUEB_DEMUX (bin);
  GstEguebDemuxClass *klass = GST_EGUEB_DEMUX_GET_CLASS (thiz);
  GstObject *src;
  const gchar *src_name;
  gboolean handled = FALSE;

  src = GST_MESSAGE_SRC (message);
  src_name = src ? GST_OBJECT_NAME (src) : "(NULL)";

  GST_DEBUG_OBJECT (thiz, "Received sync message '%s' from '%s'",
      GST_MESSAGE_TYPE_NAME (message), src_name);

  switch (GST_MESSAGE_TYPE (message)) {
    case GST_MESSAGE_ELEMENT:
      /* message from the parser */
      if (!g_strcmp0 (src_name, "sink")) {
        gst_egueb_demux_handle_sink_message_sync (thiz, message);
      }
      handled = TRUE;
      break;

    default:
      break;
  }

  if (handled)
    gst_message_unref (message);
  else
    klass->handle_message (bin, message);
}

static GstStateChangeReturn
gst_egueb_demux_change_state (GstElement * element, GstStateChange transition)
{
  GstEguebDemux *thiz;
  GstStateChangeReturn ret;

  thiz = GST_EGUEB_DEMUX (element);

  switch (transition) {
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
  if (ret == GST_STATE_CHANGE_FAILURE)
    return ret;

  switch (transition) {
    default:
      break;
  }

  return ret;
}

static void
gst_egueb_demux_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstEguebDemux *thiz = NULL;

  g_return_if_fail (GST_IS_EGUEB_DEMUX (object));

  thiz = GST_EGUEB_DEMUX (object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_egueb_demux_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstEguebDemux *thiz = NULL;

  g_return_if_fail (GST_IS_EGUEB_DEMUX (object));

  thiz = GST_EGUEB_DEMUX (object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_egueb_demux_dispose (GObject * object)
{
  GstEguebDemux *thiz = GST_EGUEB_DEMUX (object);

  GST_DEBUG_OBJECT (thiz, "disposing SVG demuxer");

  /* dispose our own implementation */
  if (thiz->xml) {
    gst_buffer_unref (thiz->xml);
    thiz->xml = NULL;
  }

  GST_CALL_PARENT (G_OBJECT_CLASS, dispose, (object));
}

static void
gst_egueb_demux_class_init (GstEguebDemuxClass * klass)
{
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);
  GstBinClass *gstbin_class = GST_BIN_CLASS (klass);
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  parent_class = g_type_class_peek_parent (klass);

  /* Register functions */
  gobject_class->dispose = GST_DEBUG_FUNCPTR (gst_egueb_demux_dispose);
  gobject_class->set_property =
      GST_DEBUG_FUNCPTR (gst_egueb_demux_set_property);
  gobject_class->get_property =
      GST_DEBUG_FUNCPTR (gst_egueb_demux_get_property);

  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_egueb_demux_change_state);

  klass->handle_message = gstbin_class->handle_message;
  gstbin_class->handle_message = gst_egueb_demux_handle_message;

  /* Register signals */
  /* Register properties */
}

static void
gst_egueb_demux_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_egueb_demux_sink_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_egueb_demux_src_video_template));
  gst_element_class_set_details (element_class, &gst_egueb_demux_details);
}

static void
gst_egueb_demux_init (GstEguebDemux * thiz, GstEguebDemuxClass * g_class)
{
  GstPad *pad;
  GstPad *ghost_pad;

  /* Create the sink */
  thiz->sink = gst_element_factory_make ("eguebxmlsink", "sink");
  if (!thiz->sink) {
    GST_ERROR_OBJECT (thiz, "Unable to create 'eguebxmlsink' element");
    goto error_parser;
  }

  /* Create svg source */
  thiz->src = gst_element_factory_make ("eguebsrc", "src");
  if (!thiz->src) {
    GST_ERROR_OBJECT (thiz, "Unable to create 'eguebsrc' element");
    goto error_src;
  }
  /* until the element does not receive the xml, lock its state */
  gst_element_set_locked_state (thiz->src, TRUE);

  /* Add it to the bin */
  gst_bin_add_many (GST_BIN (thiz), thiz->src, thiz->sink, NULL);

  /* Create ghost pad and link it to the sink */
  pad = gst_element_get_static_pad (thiz->sink, "sink");
  ghost_pad = gst_ghost_pad_new ("sink", pad);
  gst_pad_set_active (ghost_pad, TRUE);
  gst_element_add_pad (GST_ELEMENT (thiz), ghost_pad);
  gst_object_unref (pad);
  
  /* Create ghost pad and link it to the src */
  pad = gst_element_get_static_pad (thiz->src, "src");
  ghost_pad = gst_ghost_pad_new ("video", pad);
  gst_pad_set_active (ghost_pad, TRUE);
  gst_element_add_pad (GST_ELEMENT (thiz), ghost_pad);
  gst_object_unref (pad);

  /* Initialize our internal members */
  /* Set default property values */

  return;

error_src:
  gst_object_unref (thiz->sink);
error_parser:
  return;
}

