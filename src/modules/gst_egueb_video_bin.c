/* Gst-Egueb - GStreamer integration for Egueb
 * Copyright (C) 2014 Jorge Luis Zapata
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library.
 * If not, see <http://www.gnu.org/licenses/>.
 */

#include "gst_egueb_video_bin.h"

GST_DEBUG_CATEGORY_EXTERN (gst_egueb_video_bin_debug);
#define GST_CAT_DEFAULT gst_egueb_video_bin_debug
#define parent_class gst_egueb_video_bin_parent_class

enum {
  PROP_0,
  PROP_RENDERER,
  PROP_URI,
  PROP_NOTIFIER,
};

static GstStaticPadTemplate gst_egueb_video_bin_other_factory =
GST_STATIC_PAD_TEMPLATE ("src_%u",
    GST_PAD_SRC,
    GST_PAD_SOMETIMES,
    GST_STATIC_CAPS_ANY);

G_DEFINE_TYPE (GstEguebVideoBin, gst_egueb_video_bin, GST_TYPE_BIN);

/*----------------------------------------------------------------------------*
 *                              Video surface                                 *
 *----------------------------------------------------------------------------*/
typedef struct _GstEguebVideoBinSurface
{
  GstBuffer *buffer;
#if HAVE_GST_1
  GstMapInfo mi;
#endif
} GstEguebVideoBinSurface;

static void
gst_egueb_video_bin_surface_free (void * data, void * user_data)
{
  GstEguebVideoBinSurface * thiz = user_data;

#if HAVE_GST_1
  gst_buffer_unmap (thiz->buffer, &thiz->mi);
#endif
  gst_buffer_unref (thiz->buffer);
  g_free (thiz);
}

static Enesim_Surface *
gst_egueb_video_bin_surface_new (GstBuffer * buffer, GstCaps * caps)
{
  GstEguebVideoBinSurface *thiz;
  GstStructure *s;
  Enesim_Surface *surface;
  guint8 * data;
  gint width;
  gint height;
  gint stride;

  s = gst_caps_get_structure (caps, 0);
  /* get the width and height */
  gst_structure_get_int (s, "width", &width);
  gst_structure_get_int (s, "height", &height);
  gst_caps_unref (caps);

  thiz = g_new0 (GstEguebVideoBinSurface, 1);
  thiz->buffer = buffer;

#if HAVE_GST_1
  gst_buffer_map (thiz->buffer, &thiz->mi, GST_MAP_READ);
  data = thiz->mi.data; 
  stride = GST_ROUND_UP_4 (width * 4);
#else
  data = GST_BUFFER_DATA (thiz->buffer);
  stride = GST_ROUND_UP_4 (width * 4);
#endif
  surface = enesim_surface_new_data_from (ENESIM_FORMAT_ARGB8888,
      width, height, EINA_FALSE, data, stride,
      gst_egueb_video_bin_surface_free, thiz);

  return surface;
}

static void
gst_egueb_video_bin_appsink_show (GstEguebVideoBin * thiz,
    GstBuffer * buffer, GstCaps * caps)
{
  Enesim_Surface *surface;

  GST_DEBUG_OBJECT (thiz, "Buffer received");

  surface = gst_egueb_video_bin_surface_new (buffer, caps);
  /* set the new surface on the renderer */
  enesim_renderer_lock (thiz->image);
  enesim_renderer_image_source_surface_set (thiz->image, surface);
  enesim_renderer_unlock (thiz->image);
}

static GstFlowReturn
gst_egueb_video_bin_appsink_preroll_cb (GstObject * sink, gpointer user_data)
{
  GstEguebVideoBin *thiz = user_data;
#if HAVE_GST_1
  GstSample *sample;
#endif
  GstBuffer *buffer;
  GstCaps *caps;

#if HAVE_GST_1
  g_signal_emit_by_name (sink, "pull-preroll", &sample);
  buffer = gst_sample_get_buffer (sample);
  caps = gst_caps_ref (gst_sample_get_caps (sample));
#else
  g_signal_emit_by_name (sink, "pull-preroll", &buffer);
  caps = gst_buffer_get_caps (buffer);
#endif

  gst_egueb_video_bin_appsink_show (thiz, buffer, caps);
  gst_caps_unref (caps);

  return GST_FLOW_OK;
}

#if HAVE_GST_1
static GstFlowReturn
gst_egueb_video_bin_appsink_sample_cb (GstElement * sink, gpointer user_data)
{
  GstEguebVideoBin *thiz = user_data;
  GstSample *sample;
  GstBuffer *buffer;
  GstCaps *caps;

  g_signal_emit_by_name (sink, "pull-sample", &sample);
  buffer = gst_sample_get_buffer (sample);
  caps = gst_caps_ref (gst_sample_get_caps (sample));

  gst_egueb_video_bin_appsink_show (thiz, buffer, caps);
  gst_caps_unref (caps);

  return GST_FLOW_OK;
}
#endif


#if HAVE_GST_0
static GstFlowReturn
gst_egueb_video_bin_appsink_buffer_cb (GstElement * sink, gpointer user_data)
{
  GstEguebVideoBin *thiz = user_data;
  GstBuffer *buffer;
  GstCaps *caps;

  g_signal_emit_by_name (appsink, "pull-buffer", &buffer);
  caps = gst_buffer_get_caps (buffer);

  gst_egueb_video_bin_appsink_show (thiz, buffer, caps);
  gst_caps_unref (caps);

  return GST_FLOW_OK;
}
#endif

static void
gst_egueb_video_bin_uridecodebin_pad_added_cb (GstElement * src,
    GstPad * pad, GstEguebVideoBin * thiz)
{
  GstElement *parent;
  GstPad  *gpad, *srcpad = NULL;
  GstCaps *caps;
  GstStructure *s;
  const gchar *name;

  GST_DEBUG_OBJECT (thiz, "Pad added");
  /* create the videoconvert ! capsfilter or ffmpegcolorspace ! capsfiler
   * for video based caps, otherwise expose the pad
   */
#if GST_CHECK_VERSION (1,0,0)
  caps = gst_pad_query_caps (pad, NULL);
#else
  caps = gst_pad_get_caps_reffed (pad);
#endif

  s = gst_caps_get_structure (caps, 0);
  name = gst_structure_get_name (s);
  if (!strncmp (name, "video", 5)) {
    GstElement *conv, *sink;
    GstPad *sinkpad;
    GstCaps *appsink_caps;

    GST_INFO_OBJECT (thiz, "Creating video pipeline for caps %" GST_PTR_FORMAT, caps);
#if GST_CHECK_VERSION (1,0,0)
    conv = gst_element_factory_make ("videoconvert", NULL);
#else
    conv = gst_element_factory_make ("ffmpegcolorspace", NULL);
#endif
    sink = gst_element_factory_make ("appsink", NULL);
    appsink_caps =  gst_caps_new_simple (
#if HAVE_GST_1
        "video/x-raw",
        "format", G_TYPE_STRING, "BGRx",
#else
        "video/x-raw-rgb",
        "depth", G_TYPE_INT, 24, "bpp", G_TYPE_INT, 32,
        "endianness", G_TYPE_INT, G_BIG_ENDIAN,
        "red_mask", G_TYPE_INT, 0x0000ff00,
        "green_mask", G_TYPE_INT, 0x00ff0000,
        "blue_mask", G_TYPE_INT, 0xff000000,
#endif
        NULL);
    /* Set the desired properties */
    g_object_set (G_OBJECT (sink), "caps", appsink_caps, NULL);
    g_object_set (G_OBJECT (sink), "emit-signals", TRUE, NULL);
    g_object_set (G_OBJECT (sink), "max-buffers", 1, NULL);
    gst_caps_unref (appsink_caps);
    /* Register the signals */
#if HAVE_GST_1
    g_signal_connect (G_OBJECT (sink), "new-preroll",
        G_CALLBACK (gst_egueb_video_bin_appsink_preroll_cb), thiz);
    g_signal_connect (G_OBJECT (sink), "new-sample",
        G_CALLBACK (gst_egueb_video_bin_appsink_sample_cb), thiz);
#else
    g_signal_connect (G_OBJECT (sink), "new-preroll",
        G_CALLBACK (gst_egueb_video_bin_appsink_preroll_cb), thiz);
    g_signal_connect (G_OBJECT (sink), "new-buffer",
        G_CALLBACK (gst_egueb_video_bin_appsink_buffer_cb), thiz);
#endif

    gst_bin_add_many (GST_BIN (thiz), conv, sink, NULL);
    gst_element_link (conv, sink);

    sinkpad = gst_element_get_static_pad (conv, "sink");
    gst_pad_link (pad, sinkpad);
    gst_object_unref (sinkpad);

    gst_element_sync_state_with_parent (sink);
    gst_element_sync_state_with_parent (conv);
  } else {
    /* ghost it on the bin */
    GST_INFO_OBJECT (thiz, "Exposing pad with caps %" GST_PTR_FORMAT, caps);
    gpad = gst_ghost_pad_new (name, pad);
    gst_pad_set_active (gpad, TRUE);
    gst_element_add_pad (GST_ELEMENT (thiz), gpad);
  }

  gst_caps_unref (caps);
}

static void
gst_egueb_video_bin_uridecodebin_pad_removed_cb (GstElement * src,
    GstPad * pad, GstEguebVideoBin * thiz)
{
  GST_DEBUG_OBJECT (thiz, "Pad removed");
  /* TODO remove the ghost pad from the bin in case it is not a video pad */
  /* TODO remove videoconvert / appsink in case it is a video pad */
}

static void
gst_egueb_video_bin_uridecodebin_no_more_pads_cb (GstElement * src,
    GstEguebVideoBin * thiz)
{
  GST_DEBUG_OBJECT (thiz, "No more pads");
  gst_element_no_more_pads (GST_ELEMENT (thiz));
}

/*----------------------------------------------------------------------------*
 *                              GstBin interface                              *
 *----------------------------------------------------------------------------*/
static void
gst_egueb_video_bin_handle_message (GstBin * bin, GstMessage * message)
{
  GstEguebVideoBin *thiz;
  gboolean handle = TRUE;

  thiz = GST_EGUEB_VIDEO_BIN (bin);
  GST_LOG_OBJECT (thiz, "Received message '%s'",
      GST_MESSAGE_TYPE_NAME (message));

  switch (GST_MESSAGE_TYPE (message)) {
    case GST_MESSAGE_ERROR:
      gst_message_unref (message);
      /* We can not change the state of the bin from the streaming thread
       * we need to enqueue this message for later
       */
      //gst_element_set_state (GST_ELEMENT (thiz), GST_STATE_READY);
      /* TODO Send a notification of the error async */
      handle = FALSE;
      break;

    default:
      break;
  }


  if (handle) {
    GST_BIN_CLASS (gst_egueb_video_bin_parent_class)->handle_message (bin, message);
  }
}

/*----------------------------------------------------------------------------*
 *                             GObject interface                              *
 *----------------------------------------------------------------------------*/
static void
gst_egueb_video_bin_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstEguebVideoBin * thiz = GST_EGUEB_VIDEO_BIN (object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_egueb_video_bin_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstEguebVideoBin * thiz = GST_EGUEB_VIDEO_BIN (object);

  switch (prop_id) {
    case PROP_URI:
      g_object_set (thiz->uridecodebin, "uri", g_value_get_string (value),
          NULL);
      break;

    case PROP_RENDERER:
      thiz->image = g_value_get_pointer (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_egueb_video_bin_dispose (GObject * object)
{
  GstEguebVideoBin *thiz = GST_EGUEB_VIDEO_BIN (object);

  enesim_renderer_unref (thiz->image);

  GST_CALL_PARENT (G_OBJECT_CLASS, dispose, (object));
}

/*----------------------------------------------------------------------------*
 *                              GType interface                               *
 *----------------------------------------------------------------------------*/
static void
gst_egueb_video_bin_init (GstEguebVideoBin * thiz)
{
  thiz->uridecodebin = gst_element_factory_make ("uridecodebin", NULL);
  gst_bin_add (GST_BIN (thiz), thiz->uridecodebin);

  /* Our signal handlers */
  g_signal_connect (G_OBJECT (thiz->uridecodebin), "pad-added",
      G_CALLBACK (gst_egueb_video_bin_uridecodebin_pad_added_cb), thiz);
  g_signal_connect (G_OBJECT (thiz->uridecodebin), "pad-removed",
      G_CALLBACK (gst_egueb_video_bin_uridecodebin_pad_removed_cb), thiz);
  g_signal_connect (G_OBJECT (thiz->uridecodebin), "no-more-pads",
      G_CALLBACK (gst_egueb_video_bin_uridecodebin_no_more_pads_cb), thiz);
}

static void
gst_egueb_video_bin_class_init (GstEguebVideoBinClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstBinClass *gstbin_class = GST_BIN_CLASS (klass);

  gst_egueb_video_bin_parent_class = g_type_class_peek_parent (klass);
  gobject_class->dispose = GST_DEBUG_FUNCPTR (gst_egueb_video_bin_dispose);
  gobject_class->set_property =
      GST_DEBUG_FUNCPTR (gst_egueb_video_bin_set_property);
  gobject_class->get_property =
      GST_DEBUG_FUNCPTR (gst_egueb_video_bin_get_property);

  /* Properties */
  g_object_class_install_property (gobject_class, PROP_URI,
      g_param_spec_string ("uri", "URI", "URI of the video content", NULL,
        G_PARAM_WRITABLE));
  g_object_class_install_property (gobject_class, PROP_RENDERER,
      g_param_spec_pointer ("renderer", "Renderer", "Image renderer",
        G_PARAM_WRITABLE));

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_egueb_video_bin_other_factory));

  gst_element_class_set_details_simple (element_class,
      "Egueb Video Provider", "Sink",
      "Implements the video provider interface",
      "<enesim-devel@googlegroups.com>");
#if 0
  element_class->change_state =
      GST_DEBUG_FUNCPTR (gst_egueb_video_bin_change_state);
#endif
  gstbin_class->handle_message = gst_egueb_video_bin_handle_message;
}

#if 0
static void
gst_egueb_video_bin_set_state (GstEguebVideoBin * thiz,
    GstState to)
{
  GstObject *parent;
  GstState parent_state;

  if (!thiz->bin) {
    return;
  }

  parent = gst_element_get_parent (thiz->bin);
  GST_STATE_LOCK (parent);
  gst_element_get_state (GST_ELEMENT (parent), &parent_state, NULL, 0);
  GST_INFO_OBJECT (thiz->bin, "Trying to set state to %s with parent state %s",
      gst_element_state_get_name (to),
      gst_element_state_get_name (parent_state));
  if (parent_state > to) {
    GstStateChangeReturn ret;

    GST_INFO_OBJECT (thiz->bin, "Setting state to %s",
        gst_element_state_get_name (to));
    thiz->pending  = GST_STATE_VOID_PENDING;
    ret = gst_element_set_state (thiz->bin, to);
    GST_ERROR ("ret = %d", ret);
  } else {
    GST_INFO_OBJECT (thiz->bin, "Keeping state %s for later",
        gst_element_state_get_name (to));
    thiz->pending = to;
  }
  GST_STATE_UNLOCK (parent);
  gst_object_unref (parent);
}

static void
gst_egueb_video_bin_lock (GstEguebVideoBin * thiz)
{
  g_mutex_lock (thiz->lock);
}

static void
gst_egueb_video_bin_unlock (GstEguebVideoBin * thiz)
{
  g_mutex_unock (thiz->lock);
}

static void
gst_egueb_video_bin_reset (GstEguebVideoBin * thiz)
{
  gst_egueb_video_bin_lock (thiz);
  if (thiz->uridecodebin) {
    gst_element_set_state (thiz->uridecodebin, GST_STATE_NULL);
    gst_bin_remove (thiz->bin, thiz->uridecodebin);
    gst_object_unref (thiz->uridecodebin);
    thiz->uridecodebin = NULL;
  }

  if (thiz->convert) {
    gst_element_set_state (thiz->convert, GST_STATE_NULL);
    gst_bin_remove (thiz->bin, thiz->convert);
    gst_object_unref (thiz->convert);
    thiz->convert = NULL;
  }

  if (thiz->capsfilter) {
    gst_element_set_state (thiz->capsfilter, GST_STATE_NULL);
    gst_bin_remove (thiz->bin, thiz->capsfilter);
    gst_object_unref (thiz->capsfilter);
    thiz->capsfilter = NULL;
  }
  gst_egueb_video_bin_unlock (thiz);
}
#endif
