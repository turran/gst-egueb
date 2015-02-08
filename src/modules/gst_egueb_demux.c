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
/* The idea of this element is that it should produce image buffers
 * with the rendered svg per frame
 * as properties we might have:
 * - framerate The framerate of the svg
 * Should it work in pull and push mode?
 * It should use the enesim buffer instead of normal gstbuffers to create
 * the buffers
 */

/* TODO
 * For the multimedia feature we need to:
 * - Every (new)segment that travels on the pads must be synchronized
 *   with the actual segment
 * - What to do with the stream start events?
 * - We need to not forward seek events from a ghostpad into the decodebin
 *   branch
 */

#include "gst_egueb_demux.h"
#include "gst_egueb_video_bin.h"
#include <gst/app/gstappsink.h>

GST_DEBUG_CATEGORY_EXTERN (gst_egueb_demux_debug);
#define GST_CAT_DEFAULT gst_egueb_demux_debug
#define parent_class gst_egueb_demux_parent_class

#define DEFAULT_CONTAINER_WIDTH 256
#define DEFAULT_CONTAINER_HEIGHT 256

/* Forward declarations */
static void
gst_egueb_demux_video_loop (gpointer user_data);
static void
gst_egueb_demux_video_fixate_caps (GstPad * pad, GstCaps * caps);
static gboolean
gst_egueb_demux_video_set_caps (GstPad * pad, GstCaps * caps);

G_DEFINE_TYPE (GstEguebDemux, gst_egueb_demux, GST_TYPE_BIN);

static GstStaticPadTemplate gst_egueb_demux_video_factory =
GST_STATIC_PAD_TEMPLATE ("video",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (
#if HAVE_GST_1
        "video/x-raw, "
        "format = BGRx, "
#else
        "video/x-raw-rgb, "
        "depth = 24, "
        "bpp = 32, "
        "endianness = (int)4321, "
        "red_mask = 0x0000ff00, "
        "green_mask = 0x00ff0000, "
        "blue_mask = 0xff000000, "
#endif
        "framerate = (fraction) [ 1, MAX], "
        "pixel-aspect-ratio = (fraction) [ 1, MAX ], "
        "width = (int) [ 1, MAX ], "
        "height = (int) [ 1, MAX ]")
    );

static GstStaticPadTemplate gst_egueb_demux_other_factory =
GST_STATIC_PAD_TEMPLATE ("src_%u",
    GST_PAD_SRC,
    GST_PAD_SOMETIMES,
    GST_STATIC_CAPS_ANY);


enum
{
  PROP_0,
  PROP_CONTAINER_WIDTH,
  PROP_CONTAINER_HEIGHT,
  PROP_BACKGROUND_COLOR,
  PROP_URI,
  /* FILL ME */
};

/*----------------------------------------------------------------------------*
 *                             Video provider                                 *
 *----------------------------------------------------------------------------*/
typedef struct _GstEguebDemuxVideoProvider
{
  GstEguebDemux *demux;
  GstElement *bin;
} GstEguebDemuxVideoProvider;

static void *
gst_egueb_demux_video_provider_descriptor_create (void)
{
  GstEguebDemuxVideoProvider *thiz;

  thiz = g_new0 (GstEguebDemuxVideoProvider, 1);
  thiz->bin = g_object_new (GST_TYPE_EGUEB_VIDEO_BIN, NULL);

  return thiz;
}

static void
gst_egueb_demux_video_provider_descriptor_destroy (void * data)
{
  GstEguebDemuxVideoProvider *thiz = data;

  GST_ERROR ("Destroying the video provider");
  gst_element_set_state (thiz->bin, GST_STATE_NULL);
#if 0
  parent = gst_element_get_parent (thiz->bin);
  gst_bin_remove (GST_BIN (parent), thiz->bin);
  thiz->bin = NULL;
  gst_object_unref (parent);

  if (thiz->image) {
    enesim_renderer_unref (thiz->image);
    thiz->image = NULL;
  }
#endif
  g_free (thiz);
}

static void
gst_egueb_demux_video_provider_descriptor_open (void * data,
    Egueb_Dom_String * uri)
{
  GstEguebDemuxVideoProvider *thiz = data;
  const char *str;

  str = egueb_dom_string_string_get (uri);
  GST_ERROR ("Open URI %s", str);
  g_object_set (thiz->bin, "uri", str, NULL);
}

static void
gst_egueb_demux_video_provider_descriptor_close (void * data)
{
  GstEguebDemuxVideoProvider *thiz = data;

  GST_ERROR_OBJECT (thiz->bin, "Closing");
  gst_element_set_state (thiz->bin, GST_STATE_READY);
}

static void
gst_egueb_demux_video_provider_descriptor_play (void * data)
{
  GstEguebDemuxVideoProvider *thiz = data;

  GST_ERROR_OBJECT (thiz->bin, "Playing");
  gst_element_set_state (thiz->bin, GST_STATE_PLAYING);
}

static void
gst_egueb_demux_video_provider_descriptor_pause (void * data)
{
  GstEguebDemuxVideoProvider *thiz = data;

  GST_ERROR_OBJECT (thiz->bin, "Pausing");
  gst_element_set_state (thiz->bin, GST_STATE_PAUSED);
}

static Egueb_Dom_Video_Provider_Descriptor gst_egueb_demux_video_provider = {
  /* .version = */ EGUEB_DOM_VIDEO_PROVIDER_DESCRIPTOR_VERSION,
  /* .create  = */ gst_egueb_demux_video_provider_descriptor_create,
  /* .destroy = */ gst_egueb_demux_video_provider_descriptor_destroy,
  /* .open    = */ gst_egueb_demux_video_provider_descriptor_open,
  /* .close   = */ gst_egueb_demux_video_provider_descriptor_close,
  /* .play    = */ gst_egueb_demux_video_provider_descriptor_play,
  /* .pause   = */ gst_egueb_demux_video_provider_descriptor_pause
};

/*----------------------------------------------------------------------------*
 *                           Multimedia interface                             *
 *----------------------------------------------------------------------------*/
static GstFlowReturn
gst_egueb_demux_multimedia_video_chain (GstPad * pad, GstObject * obj,
    GstBuffer * buffer)
{
  /* TODO check if it is on time, discard otherwise */
  GST_ERROR ("Received buffers");
  return GST_FLOW_OK;
}

static gboolean
gst_egueb_demux_multimedia_video_query (GstPad * pad, GstObject * obj,
    GstQuery * query)
{
  gboolean ret = FALSE;

  GST_ERROR_OBJECT (obj, "Querying %s", GST_QUERY_TYPE_NAME (query));

  switch (GST_QUERY_TYPE (query)) {
#if HAVE_GST_1
    case GST_QUERY_ALLOCATION:
      /* TODO for the allocation, use the same allocator as our current sink? */
      break;

    case GST_QUERY_CAPS:
      {
        GstCaps *caps;

        caps = gst_caps_copy (gst_pad_get_pad_template_caps (pad));
        gst_query_set_caps_result (query, caps);
        gst_caps_unref (caps);
        ret = TRUE;
      }
      break;
#endif

    default:
      break;
  }

  return ret;
}

static gboolean
gst_egueb_demux_multimedia_video_event (GstPad * pad, GstObject * obj,
    GstEvent * event)
{
  gboolean ret = FALSE;

  GST_ERROR_OBJECT (obj, "Received event '%s'", GST_EVENT_TYPE_NAME (event));

  switch (GST_EVENT_TYPE (event)) {
    /* Make the EOS trigger the EOS on the video provider notifier */
    case GST_EVENT_EOS:
      ret = TRUE;
      break;

#if HAVE_GST_1
    case GST_EVENT_STREAM_START:
    case GST_EVENT_SEGMENT:
    /* We assume the caps event already tries to intersect with the
     * template caps
     */
    case GST_EVENT_CAPS:
      ret = TRUE;
      break;
#endif

    default:
      break;
  }

  gst_event_unref (event);

  return ret;
}

#if HAVE_GST_0
static GstFlowReturn
gst_egueb_demux_multimedia_video_chain_simple (GstPad * pad,
    GstBuffer * buffer)
{
  GstObject *object;
  GstFlowReturn ret;

  object = gst_pad_get_parent (pad);
  ret = gst_egueb_demux_multimedia_video_chain (pad, object, buffer);
  gst_object_unref (object);

  return ret;
}

static gboolean
gst_egueb_demux_multimedia_video_query_simple (GstPad * pad, GstQuery * query)
{
  GstObject *object;
  gboolean ret;

  object = gst_pad_get_parent (pad);
  ret = gst_egueb_demux_multimedia_video_query (pad, object, query);
  gst_object_unref (object);
  return ret;
}

static gboolean
gst_egueb_demux_multimedia_video_event_simple (GstPad * pad, GstEvent * event)
{
  GstObject *object;
  gboolean ret;

  object = gst_pad_get_parent (pad);
  ret = gst_egueb_demux_multimedia_video_event (pad, object, event);
  gst_object_unref (object);
  return ret;
}
#endif

static void
gst_egueb_demux_multimedia_pad_removed_cb (GstElement * src,
    GstPad * pad, GstEguebDemux * thiz)
{
  GST_INFO_OBJECT (thiz, "Pad removed");
}

static void
gst_egueb_demux_multimedia_no_more_pads_cb (GstElement * src,
    GstEguebDemux * thiz)
{
  GST_INFO_OBJECT (thiz, "No more pads");
}

static void
gst_egueb_demux_multimedia_pad_added_cb (GstElement * src,
    GstPad * pad, GstEguebDemux * thiz)
{
  GST_INFO_OBJECT (thiz, "Pad added");
  /* For video pads, we need to blend ourselves */
  if (!strcmp (gst_object_get_name (GST_OBJECT (pad)), "video")) {
    GstPad *proxy;

    proxy = GST_PAD (gst_proxy_pad_get_internal (GST_PROXY_PAD (pad)));
#if HAVE_GST_1
    gst_pad_set_chain_function (proxy,
        GST_DEBUG_FUNCPTR (gst_egueb_demux_multimedia_video_chain));
    gst_pad_set_query_function (proxy,
        GST_DEBUG_FUNCPTR (gst_egueb_demux_multimedia_video_query));
    gst_pad_set_event_function (proxy,
        GST_DEBUG_FUNCPTR (gst_egueb_demux_multimedia_video_event));
#else
    gst_pad_set_chain_function (proxy,
        GST_DEBUG_FUNCPTR (gst_egueb_demux_multimedia_video_chain_simple));
    gst_pad_set_query_function (proxy,
        GST_DEBUG_FUNCPTR (gst_egueb_demux_multimedia_video_query_simple));
    gst_pad_set_event_function (proxy,
        GST_DEBUG_FUNCPTR (gst_egueb_demux_multimedia_video_event_simple));
#endif
    gst_object_unref (proxy);
  }
}

static void 
gst_egueb_demux_multimedia_video_cb(
		Egueb_Dom_Event *ev, void *data)
{
  GstEguebDemux *thiz;
  GstEguebDemuxVideoProvider *dvp;
  Egueb_Dom_Video_Provider *vp = NULL;
  Egueb_Dom_Node *n;
  Enesim_Renderer *r;
  const Egueb_Dom_Video_Provider_Notifier *notifier = NULL;
  gchar *name;

  thiz = GST_EGUEB_DEMUX (data);
  GST_ERROR_OBJECT (thiz, "Multimedia event received");

  n = egueb_dom_event_target_get (ev);
  r = egueb_dom_event_multimedia_video_renderer_get (ev);

  /* create our video provider */
  vp = egueb_dom_video_provider_new (&gst_egueb_demux_video_provider,
      notifier, enesim_renderer_ref (r), n);
  dvp = egueb_dom_video_provider_data_get (vp);
  g_object_set (dvp->bin, "renderer", enesim_renderer_ref (r), NULL);
  g_object_set (dvp->bin, "notifier", notifier, NULL);

  /* add the pad added/removed callbacks */
  g_signal_connect (G_OBJECT (dvp->bin), "pad-added",
      G_CALLBACK (gst_egueb_demux_multimedia_pad_added_cb), thiz);
  g_signal_connect (G_OBJECT (dvp->bin), "pad-removed",
      G_CALLBACK (gst_egueb_demux_multimedia_pad_removed_cb), thiz);
  g_signal_connect (G_OBJECT (dvp->bin), "no-more-pads",
      G_CALLBACK (gst_egueb_demux_multimedia_no_more_pads_cb), thiz);
  /* set a new name */
  name = g_strdup_printf ("videoprovider%d", ++thiz->videoproviders);
  gst_element_set_name (dvp->bin, name);
  g_free (name);
  /* add the video pipeline to our own bin */
  gst_element_set_locked_state (dvp->bin, TRUE);
  gst_bin_add (GST_BIN (thiz), dvp->bin);

  /* finally set the video provider on the event */
  egueb_dom_event_multimedia_video_provider_set (ev, vp);
  egueb_dom_node_unref (n);
}

static void
gst_egueb_demux_multimedia_setup (GstEguebDemux * thiz)
{
  Egueb_Dom_Feature *feature;

  if (thiz->multimedia) return;

  feature = egueb_dom_node_feature_get (thiz->topmost,
      EGUEB_DOM_FEATURE_MULTIMEDIA_NAME, NULL);
  if (!feature) return;

  egueb_dom_node_event_listener_add (thiz->topmost,
  EGUEB_DOM_EVENT_MULTIMEDIA_VIDEO,
      gst_egueb_demux_multimedia_video_cb, EINA_TRUE, thiz);
  thiz->multimedia = feature;
}

static void
gst_egueb_demux_multimedia_cleanup (GstEguebDemux * thiz)
{
  if (thiz->multimedia) {
    egueb_dom_node_event_listener_remove (thiz->topmost,
        EGUEB_DOM_EVENT_MULTIMEDIA_VIDEO,
        gst_egueb_demux_multimedia_video_cb, EINA_TRUE, thiz);
    egueb_dom_feature_unref (thiz->multimedia);
    thiz->multimedia = NULL;
  }
}

/* Add the templates based on the implementation mime */
static GstPadTemplate *
gst_egueb_demux_get_sink_template (void)
{
  Egueb_Dom_List *mimes;
  Egueb_Dom_String *mime;
  Eina_Iterator *it;
  GstCaps *caps;

  mimes = egueb_dom_registry_mime_get ();
  it = egueb_dom_list_iterator_new (mimes);
  caps = gst_caps_new_empty ();
  EINA_ITERATOR_FOREACH (it, mime) {
    GstCaps *cap;

    cap = gst_caps_from_string (egueb_dom_string_string_get(mime));
    gst_caps_append (caps, cap);
  }

  eina_iterator_free (it);
  egueb_dom_list_unref (mimes);

  /* get every mime type and create a cap based on it */
  return gst_pad_template_new ("sink", GST_PAD_SINK, GST_PAD_ALWAYS, caps);
}

static gchar *
gst_egueb_demux_location_get (GstPad * pad)
{
  GstObject *parent;
  gchar *uri = NULL;

  parent = gst_pad_get_parent (pad);
  if (!parent)
    return NULL;

  if (GST_IS_GHOST_PAD (parent)) {
    GstPad *peer;

    peer = gst_pad_get_peer (GST_PAD (parent));
    uri = gst_egueb_demux_location_get (peer);
    gst_object_unref (peer);

  } else {
    GstElementFactory *f;

    f = gst_element_get_factory (GST_ELEMENT (parent));
    if (gst_element_factory_list_is_type (f, GST_ELEMENT_FACTORY_TYPE_SRC)) {
      gchar *scheme;

      /* try to get the location property */
      g_object_get (G_OBJECT (parent), "location", &uri, NULL);
      scheme = g_uri_parse_scheme (uri);
      if (!scheme) {
        if (!g_path_is_absolute (uri)) {
          gchar *absolute, *cwd;

          cwd = g_get_current_dir ();
          absolute = g_build_filename (cwd, uri, NULL);
          g_free (cwd);
          g_free (uri);

          uri = g_strdup_printf ("file://%s", absolute);
          g_free (absolute);
        } else {
          gchar *tmp = uri;
          uri = g_strdup_printf ("file://%s", tmp);
          g_free (tmp);
        }
      } else {
        g_free (scheme);
      }
    } else {
      GstPad *sink_pad = NULL;
      GstIterator *iter;
      GstIteratorResult iter_res;

      /* iterate over the sink pads */
      iter = gst_element_iterate_sink_pads (GST_ELEMENT (parent));
      while ((iter_res = gst_iterator_next (iter,
              (gpointer) & sink_pad)) != GST_ITERATOR_DONE) {
        GstPad *peer;

        peer = gst_pad_get_peer (sink_pad);
        uri = gst_egueb_demux_location_get (peer);
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

static gchar *
gst_egueb_demux_uri_get (GstEguebDemux * thiz)
{
  GstPad *pad, *peer;
  GstQuery *query;
  gchar *uri = NULL;
  gboolean ret;

  pad = gst_element_get_static_pad (GST_ELEMENT (thiz), "sink");
  peer = gst_pad_get_peer (pad);

  query = gst_query_new_uri ();
  if (gst_pad_query (peer, query)) {
    gst_query_parse_uri (query, &uri);
    GST_DEBUG_OBJECT (thiz, "Querying the URI succeeded '%s'", uri);
  }

  if (!uri) {
    GST_DEBUG_OBJECT (thiz, "No URI found, looking for a location");
    uri = gst_egueb_demux_location_get (peer);
  }
  gst_query_unref (query);
  gst_object_unref (peer);
  gst_object_unref (pad);

  return uri;
}

static void
gst_egueb_demux_uri_set (GstEguebDemux * thiz, const gchar * uri)
{
  if (thiz->location) {
    g_free (thiz->location);
    thiz->location = NULL;
  }

  thiz->location = g_strdup (uri);
}

static Eina_Bool
gst_egueb_demux_damages_get_cb (Egueb_Dom_Feature *f EINA_UNUSED,
    Eina_Rectangle *area, void *data)
{
  GstEguebDemux * thiz = data;
  Eina_Rectangle *r;

  GST_DEBUG_OBJECT (thiz, "Damage added at %d %d -> %d %d", area->x, area->y,
      area->w, area->h);
  r = malloc (sizeof(Eina_Rectangle));
  *r = *area;
  thiz->damages = eina_list_append (thiz->damages, r);

  return EINA_TRUE;
}

static Eina_Bool
gst_egueb_demux_draw (GstEguebDemux * thiz)
{
  Enesim_Log *log = NULL;
  Eina_Rectangle *r;
  Eina_Bool ret;

  /* draw with the document locked */
  g_mutex_lock (thiz->doc_lock);

  egueb_dom_document_process(thiz->doc);
  egueb_dom_feature_render_damages_get(thiz->render, thiz->s,
				gst_egueb_demux_damages_get_cb, thiz);
  /* in case we dont have any damage, just send again the previous surface converted */
  if (!thiz->damages) {
    g_mutex_unlock (thiz->doc_lock);
    return FALSE;
  }

  if (enesim_renderer_background_color_get (thiz->background) != 0) {
    enesim_renderer_draw_list(thiz->background, thiz->s, ENESIM_ROP_FILL,
        thiz->damages, 0, 0, NULL);
    ret = egueb_dom_feature_render_draw_list(thiz->render, thiz->s, ENESIM_ROP_BLEND,
        thiz->damages, 0, 0, &log);
  } else {
    ret = egueb_dom_feature_render_draw_list(thiz->render, thiz->s, ENESIM_ROP_FILL,
        thiz->damages, 0, 0, &log);
  }

  g_mutex_unlock (thiz->doc_lock);

  if (!ret) {
    enesim_log_dump (log);
    if (log) {
      enesim_log_delete (log);
    }
  }


  EINA_LIST_FREE (thiz->damages, r)
    free(r);

  return TRUE;
}

static gint
gst_egueb_demux_get_size (GstEguebDemux * thiz)
{
  return GST_ROUND_UP_4 (thiz->w * thiz->h * 4);
}

static void
gst_egueb_demux_convert (GstEguebDemux * thiz, GstBuffer * buffer)
{
  Enesim_Buffer *eb;
  Enesim_Buffer_Sw_Data sdata;
  guint8 *data;
#if HAVE_GST_1
  GstMapInfo mi;
#endif

#if HAVE_GST_1
  gst_buffer_map (buffer, &mi, GST_MAP_READWRITE);
  data = mi.data;
#else
  data = GST_BUFFER_DATA (buffer);
#endif

  sdata.xrgb8888.plane0_stride = GST_ROUND_UP_4 (thiz->w * 4);
  sdata.xrgb8888.plane0 = (uint32_t *) data;
  eb = enesim_buffer_new_data_from (ENESIM_BUFFER_FORMAT_XRGB8888, thiz->w,
      thiz->h, EINA_FALSE, &sdata, NULL, NULL);

  /* convert it to a buffer and send it */
  enesim_converter_surface (thiz->s, eb);
  enesim_buffer_unref (eb);
#if HAVE_GST_1
  gst_buffer_unmap (buffer, &mi);
#endif
}

#if HAVE_GST_1
static void
gst_egueb_demux_start (GstEguebDemux * thiz)
{
  GstPad *pad;
  GstEvent *event;
  gchar *stream_id;

  pad = gst_element_get_static_pad (GST_ELEMENT (thiz), "video");
  stream_id =
      gst_pad_create_stream_id (pad, GST_ELEMENT_CAST (thiz), NULL);
  GST_DEBUG_OBJECT (thiz, "Sending STREAM START with id '%s'", stream_id);
  gst_pad_push_event (pad, gst_event_new_stream_start (stream_id));
  gst_object_unref (pad);
  g_free (stream_id);
}
#endif

static gboolean
gst_egueb_demux_setup (GstEguebDemux * thiz, GstBuffer * buf)
{
  Enesim_Stream *s;
  Egueb_Dom_Node *doc = NULL;
  Egueb_Dom_Node *topmost;
  Egueb_Dom_Feature *render, *window, *ui;
  GstPad *pad;
  GstCaps *caps;
#if HAVE_GST_1
  GstMapInfo mi;
#endif
  gboolean ret = FALSE;
  gchar *data;
  guint8 *sdata;
  gint ssize;
  const gchar *reason = NULL;

  /* check if we have a valid xml */
  if (!buf) {
    reason = "No buffer received";
    goto beach;
  }

#if HAVE_GST_1
  gst_buffer_map (buf, &mi, GST_MAP_READ);
  sdata = mi.data;
  ssize = mi.size;
#else
  sdata = GST_BUFFER_DATA (buf);
  ssize = GST_BUFFER_SIZE (buf);
#endif

  /* create a buffer stream based on the input buffer */
  data = malloc (ssize);
  memcpy (data, sdata, ssize);
#if HAVE_GST_1
  gst_buffer_unmap (buf, &mi);
#endif

  /* the stream will free the data */
  s = enesim_stream_buffer_new (data, ssize);
  gst_buffer_unref (buf);

  /* parse the document */
  egueb_dom_parser_parse (s, &doc);
  if (!doc) {
    reason = "Failed parsing the document";
    goto beach;
  }

  /* The features are on the topmost element */
  /* TODO add events to know whenever the topmost element has changed */
  topmost = egueb_dom_document_document_element_get(doc);
  if (!topmost) {
    reason = "Topmost element not found";
    goto no_topmost;
  }

  render = egueb_dom_node_feature_get (topmost, EGUEB_DOM_FEATURE_RENDER_NAME,
      NULL);
  if (!render)
  {
    reason = "No 'render' feature found, nothing to do";
    goto no_render;
  }

  window = egueb_dom_node_feature_get (topmost, EGUEB_DOM_FEATURE_WINDOW_NAME,
      NULL);
  if (!window)
  {
    reason = "No 'window' feature found, nothing to do";
    goto no_window;
  }

  thiz->doc = egueb_dom_node_ref (doc);
  thiz->topmost = egueb_dom_node_ref (topmost);
  thiz->render = egueb_dom_feature_ref (render);
  thiz->window = window;

  /* set the uri */
  if (!thiz->location) {
    thiz->location = gst_egueb_demux_uri_get (thiz);
  }

  if (thiz->location) {
    Egueb_Dom_String *uri;

    uri = egueb_dom_string_new_with_string (thiz->location);
    egueb_dom_document_uri_set (thiz->doc, uri);
  }

  /* optional features */
  ui = egueb_dom_node_feature_get (thiz->topmost,
      EGUEB_DOM_FEATURE_UI_NAME, NULL);
  if (ui) {
    egueb_dom_feature_ui_input_get (ui, &thiz->input);
    egueb_dom_feature_unref (ui);
  }

  thiz->animation = egueb_dom_node_feature_get (thiz->topmost,
      EGUEB_SMIL_FEATURE_ANIMATION_NAME, NULL);

  gst_egueb_demux_multimedia_setup (thiz);

  /* setup our own gst egueb document */
  thiz->gdoc = gst_egueb_document_new (egueb_dom_node_ref (thiz->doc));
  gst_egueb_document_feature_io_setup (thiz->gdoc);
  ret = TRUE;

  /* set the caps, and start running */
  pad = gst_element_get_static_pad (GST_ELEMENT (thiz), "video");
#if HAVE_GST_1
  /* send the start stream */
  gst_egueb_demux_start (thiz);
  caps = gst_pad_query_caps (pad, NULL);
  gst_caps_make_writable (caps);
  gst_egueb_demux_video_fixate_caps (pad, caps);
  caps = gst_caps_fixate (caps);
#else
  caps = gst_caps_copy (gst_pad_get_caps (pad));
  gst_pad_fixate_caps (pad, caps);
#endif
  gst_pad_set_caps (pad, caps);
  gst_caps_unref (caps);

  /* start pushing buffers */
  GST_INFO_OBJECT (thiz, "Starting streaming task");
  /* TODO pass the pad itself */
#if HAVE_GST_1
  gst_pad_start_task (pad, gst_egueb_demux_video_loop, thiz, NULL);
#else
  gst_pad_start_task (pad, gst_egueb_demux_video_loop, thiz);
#endif
  gst_object_unref (pad);

no_window:
  egueb_dom_feature_unref (render);
no_render:
  egueb_dom_node_unref (topmost);
no_topmost:
  egueb_dom_node_unref (doc);
beach:

  if (!ret) {
    GST_ELEMENT_ERROR (thiz, RESOURCE, FAILED,
        ("Impossible to setup the streaming task."),
        ("%s", reason));
  }

  return ret;
}

#if HAVE_GST_1
static gboolean
gst_egueb_demux_set_allocation (GstEguebDemux * thiz,
    GstBufferPool * pool, GstAllocator * allocator,
    GstAllocationParams * params)
{
  GstAllocator *oldalloc;
  GstBufferPool *oldpool;

  oldpool = thiz->pool;
  thiz->pool = pool;

  oldalloc = thiz->allocator;
  thiz->allocator = allocator;

  if (params)
    thiz->params = *params;
  else
    gst_allocation_params_init (&thiz->params);

  if (oldpool) {
    GST_DEBUG_OBJECT (thiz, "deactivating old pool %p", oldpool);
    gst_buffer_pool_set_active (oldpool, FALSE);
    gst_object_unref (oldpool);
  }
  if (oldalloc) {
    gst_object_unref (oldalloc);
  }
  if (pool) {
    GST_DEBUG_OBJECT (thiz, "activating new pool %p", pool);
    gst_buffer_pool_set_active (pool, TRUE);
  }
  return TRUE;
}
#endif

static void
gst_egueb_demux_cleanup (GstEguebDemux * thiz)
{
  GST_DEBUG_OBJECT (thiz, "Cleaning up");

#if HAVE_GST_1
  gst_egueb_demux_set_allocation (thiz, NULL, NULL, NULL);
#endif

  gst_egueb_demux_multimedia_cleanup (thiz);

  if (thiz->input) {
    egueb_dom_input_unref (thiz->input);
    thiz->input = NULL;
  }

  /* optional features */
  if (thiz->animation) {
    egueb_dom_feature_unref (thiz->animation);
    thiz->animation = NULL;
  }

  if (thiz->render) {
    egueb_dom_feature_unref (thiz->render);
    thiz->render = NULL;
  }

  if (thiz->window) {
    egueb_dom_feature_unref (thiz->window);
    thiz->window = NULL;
  }

  if (thiz->topmost) {
    egueb_dom_node_unref (thiz->topmost);
    thiz->topmost = NULL;
  }

  if (thiz->doc) {
    egueb_dom_node_unref (thiz->doc);
    thiz->doc = NULL;
  }

  if (thiz->gdoc) {
    gst_egueb_document_free (thiz->gdoc);
    thiz->gdoc = NULL;
  }

  if (thiz->location) {
    g_free (thiz->location);
    thiz->location = NULL;
  }

  /* set the counters */
  thiz->videoproviders = 0;
}

static gboolean
gst_egueb_demux_sink_event_eos (GstPad * pad, GstEguebDemux * thiz,
    GstEvent * event)
{
  gint len;

  GST_DEBUG_OBJECT (thiz, "Received EOS");
  /* The EOS should come from upstream whenever the xml file
   * has been readed completely
   */
  len = gst_adapter_available (thiz->adapter);
  if (len) {
    GstBuffer *buf;

    buf = gst_adapter_take_buffer (thiz->adapter, len);
    gst_egueb_demux_setup (thiz, buf);
  }
  return TRUE;
}

static gboolean
gst_egueb_demux_sink_event (GstPad * pad, GstObject * obj, GstEvent * event)
{
  GstEguebDemux *thiz;
  gboolean ret = FALSE;

  thiz = GST_EGUEB_DEMUX (obj);

  GST_DEBUG_OBJECT (thiz, "Received event '%s'", GST_EVENT_TYPE_NAME (event));
  /* TODO later we need to handle FLUSH_START to flush the adapter too */
  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_EOS:
      ret = gst_egueb_demux_sink_event_eos (pad, thiz, event);
      break;

#if HAVE_GST_1
    case GST_EVENT_STREAM_START:
    case GST_EVENT_SEGMENT:
    /* We assume the caps event already tries to intersect with the
     * template caps
     */
    case GST_EVENT_CAPS:
      ret = TRUE;
      break;
#endif

    default:
      break;
  }

  gst_event_unref (event);

  return ret;
}

static GstFlowReturn
gst_egueb_demux_sink_chain (GstPad * pad, GstObject * obj, GstBuffer * buffer)
{
  GstEguebDemux *thiz;
  GstFlowReturn ret = GST_FLOW_OK;

  thiz = GST_EGUEB_DEMUX (obj);
  GST_DEBUG_OBJECT (thiz, "Received buffer");
  gst_adapter_push (thiz->adapter, gst_buffer_ref (buffer));

  return ret;
}

static void
gst_egueb_demux_video_fixate_caps (GstPad * pad, GstCaps * caps)
{
  GstEguebDemux *thiz;
  GstStructure *structure;
  gint i;

  thiz = GST_EGUEB_DEMUX (gst_pad_get_parent (pad));

  GST_DEBUG_OBJECT (thiz, "Fixating caps %" GST_PTR_FORMAT, caps);
  for (i = 0; i < gst_caps_get_size (caps); ++i) {
    structure = gst_caps_get_structure (caps, i);

    /* in case the width or height are still not-fixed use the default size */
    gst_structure_fixate_field_nearest_int (structure, "width",
        thiz->container_w);
    gst_structure_fixate_field_nearest_int (structure, "height",
        thiz->container_h);
    /* fixate the framerate in case nobody has set it */
    gst_structure_fixate_field_nearest_fraction (structure, "framerate", 30, 1);
    /* fixate the framerate in case nobody has set it */
    gst_structure_fixate_field_nearest_fraction (structure, "pixel-aspect-ratio", 1, 1);
  }

  GST_DEBUG_OBJECT (thiz, "Fixed caps are %" GST_PTR_FORMAT, caps);

  gst_object_unref (thiz);
}

static GstCaps *
gst_egueb_demux_video_get_caps (GstPad * pad)
{
  GstEguebDemux *thiz;
  GstCaps *caps;
  Egueb_Dom_Feature_Window_Type type;
  gint i;
  int cw, ch;

  thiz = GST_EGUEB_DEMUX (gst_pad_get_parent (pad));

  GST_DEBUG_OBJECT (thiz, "Getting caps");

  caps = gst_caps_copy (gst_pad_get_pad_template_caps (pad));

  g_mutex_lock (thiz->doc_lock);
  if (!thiz->doc) {
    GST_DEBUG_OBJECT (thiz, "Using template caps");
    goto beach;
  }

  if (!egueb_dom_feature_window_type_get (thiz->window, &type)) {
    GST_WARNING_OBJECT (thiz, "Impossible to get the type of the window");
    goto beach;
  }

  if (type == EGUEB_DOM_FEATURE_WINDOW_TYPE_SLAVE) {
    GST_ERROR_OBJECT (thiz, "Not supported yet");
    goto beach;
  } else {
    int old_cw, old_ch;

    /* FIXME setting the content leads to an undefined behaviour if a set_caps
     * does not come after this one
     */
    egueb_dom_feature_window_content_size_get(thiz->window, &old_cw, &old_ch);

    egueb_dom_feature_window_content_size_set(thiz->window, 0, 0);
    egueb_dom_feature_window_content_size_get(thiz->window, &cw, &ch);

    /* restore */
    egueb_dom_feature_window_content_size_set(thiz->window, old_cw, old_ch);
  }

  for (i = 0; i < gst_caps_get_size (caps); i++) {
    GstStructure *s;

    s = gst_caps_get_structure (caps, i);
    if (cw > 0)
      gst_structure_fixate_field_nearest_int (s, "width", cw);
    if (ch > 0)
      gst_structure_fixate_field_nearest_int (s, "height", ch);
  }

beach:
  g_mutex_unlock (thiz->doc_lock);
  GST_DEBUG_OBJECT (thiz, "Returning caps %" GST_PTR_FORMAT, caps);
  gst_object_unref (thiz);

  return caps;
}

static gboolean
gst_egueb_demux_video_set_caps (GstPad * pad, GstCaps * caps)
{
  GstEguebDemux * thiz;
  GstStructure *s;
  const GValue *framerate;
  gint width;
  gint height;

  thiz = GST_EGUEB_DEMUX (gst_pad_get_parent (pad));
  GST_DEBUG_OBJECT (thiz, "Setting caps %" GST_PTR_FORMAT, caps);

  width = thiz->w;
  height = thiz->h;

  /* get what downstream can change */
  s = gst_caps_get_structure (caps, 0);

  /* the framerate */
  framerate = gst_structure_get_value (s, "framerate");
  if (framerate) {

    /* Store this FPS for use when generating buffers */
    thiz->spf_n = gst_value_get_fraction_denominator (framerate);
    thiz->spf_d = gst_value_get_fraction_numerator (framerate);

    GST_DEBUG_OBJECT (thiz, "Setting framerate to %d/%d", thiz->spf_d, thiz->spf_n);
    thiz->duration = gst_util_uint64_scale (GST_SECOND, thiz->spf_n, thiz->spf_d);
    thiz->fps = gst_util_uint64_scale (1, thiz->spf_d, thiz->spf_n);
    
    if (thiz->animation) {
      egueb_smil_feature_animation_fps_set(thiz->animation, thiz->fps);
    }
  }

  /* the size */
  gst_structure_get_int (s, "width", &width);
  gst_structure_get_int (s, "height", &height);
  if (width != thiz->w || height != thiz->h) {
    if (thiz->s) {
      enesim_surface_unref (thiz->s);
      thiz->s = NULL;
    }

    GST_INFO_OBJECT (thiz, "Creating surface of size %dx%d", width, height);
    thiz->s = enesim_surface_new (ENESIM_FORMAT_ARGB8888, width, height);
    thiz->w = width;
    thiz->h = height;

    if (thiz->window) {
      GST_INFO_OBJECT (thiz, "Setting size to %dx%d", width, height);
      egueb_dom_feature_window_content_size_set (thiz->window, width, height);
    }
  }

  gst_object_unref (thiz);

  return TRUE;
}

#if 0
static gboolean
gst_egueb_demux_do_seek (GstBaseSrc *src, GstSegment *segment)
{
  GstEguebDemux *thiz = GST_EGUEB_DEMUX (src);

  g_mutex_lock (thiz->doc_lock);
  /* TODO find the nearest keyframe based on the fps */
  GST_DEBUG_OBJECT (src, "do seek at %" GST_TIME_FORMAT, GST_TIME_ARGS (segment->start));
  /* do the seek on the svg element */
  thiz->seek = segment->start;
  g_mutex_unlock (thiz->doc_lock);

  return TRUE;
}
#endif

#if HAVE_GST_1
static gboolean
gst_egueb_demux_decide_allocation (GstEguebDemux * thiz, GstQuery * query)
{
  GstBufferPool *pool;
  GstAllocator *allocator;
  GstAllocationParams params;
  GstStructure *config;
  GstCaps *caps = NULL;
  gulong buffer_size;
  guint size, min, max;

  buffer_size = gst_egueb_demux_get_size (thiz);
  gst_query_parse_allocation (query, &caps, NULL);

  if (gst_query_get_n_allocation_params (query) > 0) {
    gst_query_parse_nth_allocation_param (query, 0, &allocator, &params);
  } else {
    allocator = NULL;
    gst_allocation_params_init (&params);
  }

  if (gst_query_get_n_allocation_pools (query) > 0) {
    gst_query_parse_nth_allocation_pool (query, 0, &pool, &size, &min, &max);
    size = MAX (buffer_size, size);
  } else {
    pool = NULL;
    size = buffer_size;
    min = 0;
    max = 0;
  }

  if (pool == NULL) {
    /* no pool, we can make our own */
    GST_DEBUG_OBJECT (thiz, "no pool, making new pool");
    pool = gst_video_buffer_pool_new ();
  }

  /* now configure */
  config = gst_buffer_pool_get_config (pool);
  gst_buffer_pool_config_set_params (config, caps, size, min, max);
  gst_buffer_pool_config_set_allocator (config, allocator, &params);
  gst_buffer_pool_config_add_option (config, GST_BUFFER_POOL_OPTION_VIDEO_META);
  gst_buffer_pool_set_config (pool, config);

  /* now store */
  return gst_egueb_demux_set_allocation (thiz, pool, allocator, &params);
}

static gboolean
gst_egueb_demux_video_negotiate_caps (GstPad * pad)
{
  GstEguebDemux *thiz;
  GstBuffer *buf = NULL;
  GstCaps *caps, *ourcaps, *peercaps;
  gboolean ret = FALSE;

  thiz = GST_EGUEB_DEMUX (gst_pad_get_parent (pad));
  ourcaps = gst_pad_query_caps (pad, NULL);
  GST_DEBUG_OBJECT (thiz, "Our caps are %" GST_PTR_FORMAT, ourcaps);
  peercaps = gst_pad_peer_query_caps (pad, ourcaps);
  GST_DEBUG_OBJECT (thiz, "Peer caps are %" GST_PTR_FORMAT, peercaps);
  if (peercaps) {
    caps = peercaps;
    gst_caps_unref (ourcaps);
  } else {
    caps = ourcaps;
  }

  if (caps && !gst_caps_is_empty (caps)) {
    /* fixate the caps and try to set it */
    gst_egueb_demux_video_fixate_caps (pad, caps);
    caps = gst_caps_fixate (caps);
    GST_DEBUG_OBJECT (thiz, "Negotiated caps %" GST_PTR_FORMAT, caps);
    ret = gst_egueb_demux_video_set_caps (pad, caps);
    gst_pad_set_caps (pad, caps);
    gst_caps_unref (caps);
  } else {
    if (caps)
      gst_caps_unref (caps);
    GST_DEBUG_OBJECT (thiz, "No common caps");
  }

  gst_object_unref (thiz);

  return ret;
}
#endif

static GstBuffer * gst_egueb_demux_video_allocate_buffer (GstPad * pad)
{
  GstEguebDemux *thiz;
  GstBuffer *buf = NULL;
#if !HAVE_GST_1
  gulong buffer_size;
#endif

  thiz = GST_EGUEB_DEMUX (gst_pad_get_parent (pad));

#if HAVE_GST_1
  if (gst_pad_check_reconfigure (pad)) {
    GstQuery *query;
    GstCaps *caps;

    /* renegotiate with downstream */
    if (!gst_egueb_demux_video_negotiate_caps (pad)) {
      GST_ERROR_OBJECT (thiz, "Failed negotiating caps");
      goto done;
    }

    /* set the pool/allocator/etc to do zero-copy */
    caps = gst_pad_get_current_caps (pad);
    query = gst_query_new_allocation (caps, TRUE);
    if (!gst_pad_peer_query (pad, query)) {
      GST_DEBUG_OBJECT (thiz, "Failed to query allocation");
    }

    gst_egueb_demux_decide_allocation (thiz, query);
    gst_query_unref (query);

    if (caps) {
      gst_caps_unref (caps);
    }
  }

  if (thiz->pool)
    gst_buffer_pool_acquire_buffer (thiz->pool, &buf, NULL);

#else
  /* We need to check downstream if the caps have changed so we can
   * allocate an optimum size of surface
   */
  buffer_size = gst_egueb_demux_get_size (thiz);
  gst_pad_alloc_buffer_and_set_caps (pad, thiz->next_ts,
      buffer_size, GST_PAD_CAPS (pad), &buf);
#endif

done:
  gst_object_unref (thiz);

  return buf;
}

static void
gst_egueb_demux_video_loop (gpointer user_data)
{
  GstEguebDemux *thiz;
  GstFlowReturn ret;
  GstClockID id;
  GstBuffer *buf = NULL;
  GstPad *pad;
  GstClockTime next;
  Enesim_Buffer *eb;
  Enesim_Buffer_Sw_Data sw_data;
  gboolean send_eos = FALSE;
  gulong new_buffer_size;
  gint fps;

  thiz = GST_EGUEB_DEMUX (user_data);
  pad = gst_element_get_static_pad (GST_ELEMENT (thiz), "video");

  GST_DEBUG_OBJECT (thiz, "Creating buffer at %" GST_TIME_FORMAT,
      GST_TIME_ARGS (thiz->next_ts));

  /* check if we need to update the duration */
  g_mutex_lock (thiz->doc_lock);
  if (thiz->animation) {
    Egueb_Smil_Clock clock;
    gint64 new_duration = GST_CLOCK_TIME_NONE;

    if (!egueb_smil_feature_animation_has_animations (thiz->animation))
      send_eos = TRUE;

    if (egueb_smil_feature_animation_duration_get (thiz->animation, &clock))
      new_duration = clock;

    /* Create the new segment */
    if (new_duration != thiz->segment->duration) {
      GstMessage *msg;

      /* inform the application that a new duration is available */

      thiz->segment->duration = new_duration;
      g_mutex_unlock (thiz->doc_lock);
#if HAVE_GST_1
      msg = gst_message_new_duration_changed (GST_OBJECT (thiz));
#else
      msg = gst_message_new_duration (GST_OBJECT (thiz), GST_FORMAT_TIME,
          new_duration);
#endif
      gst_element_post_message (GST_ELEMENT (thiz), msg);
      g_mutex_lock (thiz->doc_lock);
    }
  } else {
    send_eos = TRUE;
  }
  g_mutex_unlock (thiz->doc_lock);

  /* apply the pending segment */
  if (thiz->pending_segment) {
    gint64 new_start, new_stop;

    if (gst_segment_clip (thiz->segment, GST_FORMAT_TIME,
        thiz->pending_segment->start, thiz->pending_segment->stop,
        &new_start, &new_stop)) {

      /* close previous segment and send a new one of we need to */
      if (thiz->segment->start != new_start || thiz->segment->stop !=
          new_stop) {
        GstEvent *event;

        /* close the segment */
        GST_DEBUG_OBJECT (thiz, "Closing running segment %" GST_SEGMENT_FORMAT,
            thiz->segment);

#if HAVE_GST_0
        gst_pad_push_event (pad, gst_event_new_new_segment (TRUE,
            thiz->segment->rate, thiz->segment->format,
            thiz->segment->start, thiz->segment->stop,
            GST_SEGMENT_GET_POSITION (thiz->segment)));
#endif

        thiz->segment->start = new_start;
        thiz->segment->stop = new_stop;
        thiz->send_ns = TRUE;
#if 0
        /* do the seek on the animation feature */
        egueb_smil_feature_animation_time_set (thiz->animation,
            thiz->segment->start);
#endif
        thiz->next_ts = new_start;
        GST_SEGMENT_SET_POSITION (thiz->segment, new_start);
      }
    }

    gst_segment_free (thiz->pending_segment);
    thiz->pending_segment = NULL;
  }

  if (thiz->send_ns) {
    GstEvent *event;

    GST_DEBUG_OBJECT (thiz, "Sending new segment %" GST_SEGMENT_FORMAT,
        thiz->segment);
#if HAVE_GST_1
    event = gst_event_new_segment (thiz->segment);
#else
    event = gst_event_new_new_segment (FALSE,
        thiz->segment->rate, thiz->segment->format,
        thiz->segment->start, thiz->segment->stop,
        GST_SEGMENT_GET_POSITION (thiz->segment));
#endif
    gst_pad_push_event (pad, event);
    thiz->send_ns = FALSE;
  }

  /* check if the current ts is out of segment */
  if (thiz->next_ts > thiz->segment->duration ||
      (GST_CLOCK_TIME_IS_VALID (thiz->segment->stop) &&
      thiz->next_ts > thiz->segment->stop)) {
    GST_DEBUG ("EOS reached, current: %" GST_TIME_FORMAT " stop: %" GST_TIME_FORMAT,
        GST_TIME_ARGS (thiz->next_ts), GST_TIME_ARGS (thiz->segment->stop));
    send_eos = TRUE;
    goto pause;
  }

  if (thiz->next_ts < thiz->segment->start) {
    GST_DEBUG ("Skipping out of segment buffer %" GST_TIME_FORMAT,
        GST_TIME_ARGS (thiz->next_ts));
    goto done;
  }

  /* allocate our buffer */
  buf = gst_egueb_demux_video_allocate_buffer (pad);
  if (!buf) {
    GST_ELEMENT_ERROR (thiz, STREAM, FAILED,
        ("Internal data flow error."), ("Failed to allocate buffer."));
    send_eos = TRUE;
    goto pause;
  }

  gst_egueb_demux_draw (thiz);
  gst_egueb_demux_convert (thiz, buf);
  GST_DEBUG_OBJECT (thiz, "Sending buffer with ts: %" GST_TIME_FORMAT,
      GST_TIME_ARGS (thiz->next_ts));

  /* set the duration for the next buffer, this must be done after the tick in case
   * the QoS changed the fps on the svg
   */
  thiz->duration = gst_util_uint64_scale (GST_SECOND, 1, thiz->fps);
  /* set the timestamp and duration baesed on the last timestamp set */
  GST_BUFFER_DURATION (buf) = thiz->duration;
  GST_BUFFER_TIMESTAMP (buf) = thiz->next_ts;

  /* TODO for every pad added that belongs to a video provider which is
   * PAUSED, make sure to send GAP events or empty buffers to make sure
   * the pipeline will preroll
   */
  ret = gst_pad_push (pad, buf);
  if (ret != GST_FLOW_OK) {
    if (ret == GST_FLOW_NOT_LINKED || ret < GST_FLOW_UNEXPECTED) {
      GST_ELEMENT_ERROR (thiz, STREAM, FAILED,
          ("Internal data flow error."),
          ("Failed pushing, reason %s (%d)", gst_flow_get_name (ret),
          ret));
      send_eos = TRUE;
    }
    goto pause;
  }

  /* Prepare next buffer */
  g_mutex_lock (thiz->doc_lock);
  if (thiz->animation) {
    egueb_smil_feature_animation_tick (thiz->animation);
  }

  thiz->next_ts += thiz->duration;
  GST_SEGMENT_SET_POSITION (thiz->segment, thiz->next_ts);
  g_mutex_unlock (thiz->doc_lock);

done:
  gst_object_unref (pad);
  return;

pause:
  {
    GST_DEBUG_OBJECT (thiz, "Pausing pad task");
    gst_pad_pause_task (pad);
    if (send_eos) {
      GST_DEBUG_OBJECT (thiz, "Sending EOS");
      gst_pad_push_event (pad, gst_event_new_eos ());
    }
  }
  goto done;
}

static gboolean
gst_egueb_demux_video_event_qos (GstPad * pad, GstEguebDemux * thiz,
    GstEvent * event)
{
  GstQOSType type;
  GstClockTimeDiff diff;
  GstClockTime timestamp;
  gdouble proportion;
  gint fps_n;
  gint fps_d;
  gint fps;

  if (!thiz->animation) {
    return FALSE;
  }

  /* Whenever we receive a QoS we can decide to increase the fps
   * on egueb and send more intermediate frames
   */
#if HAVE_GST_1
  gst_event_parse_qos (event, &type, &proportion, &diff, &timestamp);
#else
  gst_event_parse_qos_full (event, &type, &proportion, &diff, &timestamp);
#endif
  fps_d = thiz->spf_n;
  fps_n = thiz->spf_d;

  if (proportion > 1.0) {
    fps_d = fps_d * proportion;
  } else {
    fps_n = fps_n + ((1 - proportion) * fps_n);
  }

  fps = gst_util_uint64_scale (1, fps_n, fps_d);
  if (fps < 1)
    fps = 1;

  GST_DEBUG_OBJECT (thiz, "Updating framerate to %d/%d", fps_n, fps_d);
  thiz->fps = fps;
  egueb_smil_feature_animation_fps_set (thiz->animation, fps);

  return TRUE;
}

static gboolean
gst_egueb_demux_video_event_navigation (GstPad * pad, GstEguebDemux * thiz,
    GstEvent * event)
{
  if (!thiz->input)
    return FALSE;

  switch (gst_navigation_event_get_type (event)) {
    case GST_NAVIGATION_EVENT_INVALID:
      break;
    case GST_NAVIGATION_EVENT_KEY_PRESS:
      break;
    case GST_NAVIGATION_EVENT_KEY_RELEASE:
      break;
    case GST_NAVIGATION_EVENT_MOUSE_BUTTON_PRESS:
      {
        gint button;

        gst_navigation_event_parse_mouse_button_event (event, &button, NULL,
            NULL);
        egueb_dom_input_feed_mouse_down (thiz->input, button);
      }
      break;
    case GST_NAVIGATION_EVENT_MOUSE_BUTTON_RELEASE:
      {
        gint button;

        gst_navigation_event_parse_mouse_button_event (event, &button, NULL,
            NULL);
        egueb_dom_input_feed_mouse_up (thiz->input, button);
      }
      break;
    case GST_NAVIGATION_EVENT_MOUSE_MOVE:
      {
        gdouble x;
        gdouble y;

        gst_navigation_event_parse_mouse_move_event (event, &x, &y);
        GST_LOG_OBJECT (thiz, "Sending mouse at %g %g", x, y);
	egueb_dom_input_feed_mouse_move (thiz->input, x, y);
      }
      break;
    case GST_NAVIGATION_EVENT_COMMAND:
      break;
  }

  return TRUE;
}


static gboolean
gst_egueb_demux_video_event (GstPad * pad, GstObject * obj,
    GstEvent * event)
{
  GstEguebDemux *thiz;
  gboolean ret = FALSE;

  thiz = GST_EGUEB_DEMUX (obj);
  GST_DEBUG_OBJECT (thiz, "Received event '%s'", GST_EVENT_TYPE_NAME (event));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_QOS:
      g_mutex_lock (thiz->doc_lock);
      ret = gst_egueb_demux_video_event_qos (pad, thiz, event);
      g_mutex_unlock (thiz->doc_lock);
      break;

    case GST_EVENT_NAVIGATION:
      g_mutex_lock (thiz->doc_lock);
      ret = gst_egueb_demux_video_event_navigation (pad, thiz, event);
      g_mutex_unlock (thiz->doc_lock);
    break;

    default:
    break;
  }

  gst_event_unref (event);

  return ret;
}

static gboolean
gst_egueb_demux_video_query_duration (GstPad * pad, GstEguebDemux * thiz,
    GstQuery * query)
{
  GstFormat format;
  gboolean ret = FALSE;

  gst_query_parse_duration (query, &format, NULL);
  if (format != GST_FORMAT_TIME)
    return FALSE;

  g_mutex_lock (thiz->doc_lock);
  if (GST_CLOCK_TIME_IS_VALID (thiz->segment->duration)) {
    gst_query_set_duration (query, GST_FORMAT_TIME, thiz->segment->duration);
    ret = TRUE;
  }
  g_mutex_unlock (thiz->doc_lock);
  return ret;
}

static gboolean
gst_egueb_demux_video_query_position (GstPad * pad, GstEguebDemux * thiz,
    GstQuery * query)
{
  GstFormat format;

  gst_query_parse_position (query, &format, NULL);
  if (format != GST_FORMAT_TIME)
    return FALSE;

  g_mutex_lock (thiz->doc_lock);
  gst_query_set_position (query, GST_FORMAT_TIME,
      GST_SEGMENT_GET_POSITION (thiz->segment)); 
  g_mutex_unlock (thiz->doc_lock);
  return TRUE;
}

static gboolean
gst_egueb_demux_video_query (GstPad * pad, GstObject * obj,
    GstQuery * query)
{
  GstEguebDemux *thiz;
  gboolean ret = FALSE;

  thiz = GST_EGUEB_DEMUX (obj);
  GST_DEBUG_OBJECT (thiz, "Received query '%s'", GST_QUERY_TYPE_NAME (query));

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_DURATION:
      ret = gst_egueb_demux_video_query_duration (pad, thiz, query);
      break;

    case GST_QUERY_POSITION:
      ret = gst_egueb_demux_video_query_position (pad, thiz, query);
      break;

#if HAVE_GST_1
    case GST_QUERY_CAPS:
      {
        GstCaps *caps;

        caps = gst_egueb_demux_video_get_caps (pad);
        /* TODO handle the filter */
        gst_query_set_caps_result (query, caps);
        gst_caps_unref (caps);
        ret = TRUE;
      }
      break;
#endif

    default:
    break;
  }

  return ret;
}


#if !HAVE_GST_1
static gboolean
gst_egueb_demux_sink_event_simple (GstPad * pad, GstEvent * event)
{
  GstObject *object;
  gboolean ret;

  object = gst_pad_get_parent (pad);
  ret = gst_egueb_demux_sink_event (pad, object, event);
  gst_object_unref (object);
  return ret;
}

static GstFlowReturn
gst_egueb_demux_sink_chain_simple (GstPad * pad, GstBuffer * buffer)
{
  GstObject *object;
  GstFlowReturn ret;

  object = gst_pad_get_parent (pad);
  ret = gst_egueb_demux_sink_chain (pad, object, buffer);
  gst_object_unref (object);

  return ret;
}

static gboolean
gst_egueb_demux_video_event_simple (GstPad * pad, GstEvent * event)
{
  GstObject *object;
  gboolean ret;

  object = gst_pad_get_parent (pad);
  ret = gst_egueb_demux_video_event (pad, object, event);
  gst_object_unref (object);
  return ret;
}


static gboolean
gst_egueb_demux_video_query_simple (GstPad * pad, GstQuery * query)
{
  GstObject *object;
  gboolean ret;

  object = gst_pad_get_parent (pad);
  ret = gst_egueb_demux_video_query (pad, object, query);
  gst_object_unref (object);
  return ret;
}
#endif

static void
gst_egueb_demux_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstEguebDemux * thiz = GST_EGUEB_DEMUX (object);

  switch (prop_id) {
    case PROP_CONTAINER_WIDTH:
      g_value_set_uint (value, thiz->container_w);
      break;
    case PROP_CONTAINER_HEIGHT:
      g_value_set_uint (value, thiz->container_h);
      break;
    case PROP_BACKGROUND_COLOR:
      g_value_set_uint (value,
          enesim_renderer_background_color_get (thiz->background));
      break;
    case PROP_URI:
      g_value_set_string (value, thiz->location);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_egueb_demux_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstEguebDemux * thiz = GST_EGUEB_DEMUX (object);

  switch (prop_id) {
    case PROP_CONTAINER_WIDTH:
      thiz->container_w = g_value_get_uint (value);
      break;
    case PROP_CONTAINER_HEIGHT:
      thiz->container_h = g_value_get_uint (value);
      break;
    case PROP_BACKGROUND_COLOR:
      enesim_renderer_background_color_set (thiz->background,
          g_value_get_uint (value));
      break;
    case PROP_URI:{
      gst_egueb_demux_uri_set (thiz, g_value_get_string (value));
      break;
    }
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_egueb_demux_downgrade_state (GstEguebDemux * thiz,
    GstState from, GstState to)
{
#if 0
  GstIterator *it;
  GstElement *child;
#if HAVE_GST_1
  GValue value = G_VALUE_INIT;
#endif

  GST_DEBUG_OBJECT (thiz, "Downgrading state from %s to %s",
      gst_element_state_get_name (from),
      gst_element_state_get_name (to));
  it = gst_bin_iterate_elements (GST_BIN (thiz));
#if HAVE_GST_1
  while (GST_ITERATOR_OK == gst_iterator_next (it, &value)) {
#else
  while (GST_ITERATOR_OK == gst_iterator_next (it, (gpointer *)&child)) {
#endif
    const gchar *name;
#if HAVE_GST_1
    child = g_value_get_object (&value);
#endif

    name = gst_element_get_name (child);
    if (!strncmp (name, "videoprovider", 13)) {
      GstState state;

      gst_element_get_state (child, &state, NULL, 0);
      GST_ERROR ("child state is %s", gst_element_state_get_name (state));
      if (state == from) {
        GstStateChangeReturn ret;

        GST_ERROR_OBJECT (thiz, "Downgrading child '%s' state to %s",
            gst_element_get_name (child), gst_element_state_get_name (to));
        ret = gst_element_set_state (child, to);
        GST_ERROR ("ret = %d", ret);
      }
    }
    gst_object_unref(child);
  }
  gst_iterator_free(it);
#endif
}

static void
gst_egueb_demux_upgrade_state (GstEguebDemux * thiz, GstState to)
{
#if 0
  GstIterator *it;
  GstElement *child;
#if HAVE_GST_1
  GValue value = G_VALUE_INIT;
#endif

  GST_DEBUG_OBJECT (thiz, "Upgrading state to %s",
      gst_element_state_get_name (to));
  it = gst_bin_iterate_elements (GST_BIN (thiz));
#if HAVE_GST_1
  while (GST_ITERATOR_OK == gst_iterator_next (it, &value)) {
#else
  while (GST_ITERATOR_OK == gst_iterator_next (it, (gpointer *)&child)) {
#endif
    const gchar *name;
#if HAVE_GST_1
    child = g_value_get_object (&value);
#endif

    name = gst_element_get_name (child);
    if (!strncmp (name, "videoprovider", 13)) {
      GstEguebDemuxVideoProvider *vp;
      GstState state;

      vp = g_object_get_data (G_OBJECT (child), "videoprovider");
      if (vp->pending != GST_STATE_VOID_PENDING && vp->pending >= to) {
        GstStateChangeReturn ret;

        GST_ERROR_OBJECT (thiz, "Upgrading child '%s' state to %s",
            gst_element_get_name (child), gst_element_state_get_name (to));
        vp->pending = GST_STATE_VOID_PENDING;
        ret = gst_element_set_state (child, to);
        GST_ERROR ("ret = %d", ret);
      }
    }
    gst_object_unref(child);
  }
  gst_iterator_free(it);
#endif
}


static GstStateChangeReturn
gst_egueb_demux_change_state (GstElement * element, GstStateChange transition)
{
  GstEguebDemux *thiz = GST_EGUEB_DEMUX (element);
  GstStateChangeReturn ret;

  GST_ERROR ("Changing state from %s to %s",
      gst_element_state_get_name (GST_STATE_TRANSITION_CURRENT (transition)),
      gst_element_state_get_name (GST_STATE_TRANSITION_NEXT (transition)));

  switch (transition) {
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      gst_egueb_demux_downgrade_state (thiz, GST_STATE_PLAYING,
          GST_STATE_PAUSED);
      break;

    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_egueb_demux_downgrade_state (thiz, GST_STATE_PAUSED,
          GST_STATE_READY);
      break;

    case GST_STATE_CHANGE_READY_TO_NULL:
      gst_egueb_demux_downgrade_state (thiz, GST_STATE_READY,
          GST_STATE_NULL);
      break;

    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (gst_egueb_demux_parent_class)->change_state (element, transition);
  GST_ERROR ("ret = %d", ret);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      gst_egueb_demux_upgrade_state (thiz, GST_STATE_PAUSED);
      break;

    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      gst_egueb_demux_upgrade_state (thiz, GST_STATE_PLAYING);
      break;

    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_egueb_demux_cleanup (thiz);
      break;

    default:
      break;
  }

  return ret;
}

static void
gst_egueb_demux_dispose (GObject * object)
{
  GstEguebDemux *thiz = GST_EGUEB_DEMUX (object);

  GST_DEBUG_OBJECT (thiz, "Disposing");
  gst_egueb_demux_cleanup (thiz);

  enesim_renderer_unref(thiz->background);

  if (thiz->doc_lock)
    g_mutex_free (thiz->doc_lock);

  if (thiz->adapter) {
    g_object_unref (thiz->adapter);
    thiz->adapter = NULL;
  }

  GST_CALL_PARENT (G_OBJECT_CLASS, dispose, (object));

  /* Do not close the libs, some of its dependencies
   * do not close correctly (fontconfig for example)
   * egueb_smil_shutdown ();
   * egueb_dom_shutdown ();
   */
}

static void
gst_egueb_demux_init (GstEguebDemux * thiz)
{
  GstEguebDemuxClass *klass;
  GstPad *pad;

  /* Create the pads */
  klass = GST_EGUEB_DEMUX_GET_CLASS (thiz);
  pad = gst_pad_new_from_template (gst_element_class_get_pad_template
      (GST_ELEMENT_CLASS (klass), "sink"), "sink");

#if HAVE_GST_1
  gst_pad_set_event_function (GST_PAD (pad),
      GST_DEBUG_FUNCPTR (gst_egueb_demux_sink_event));
  gst_pad_set_chain_function (GST_PAD (pad),
      GST_DEBUG_FUNCPTR (gst_egueb_demux_sink_chain));
#else
  gst_pad_set_event_function (GST_PAD (pad),
      GST_DEBUG_FUNCPTR (gst_egueb_demux_sink_event_simple));
  gst_pad_set_chain_function (GST_PAD (pad),
      GST_DEBUG_FUNCPTR (gst_egueb_demux_sink_chain_simple));
#endif
  gst_element_add_pad (GST_ELEMENT (thiz), pad);

  pad = gst_pad_new_from_static_template (&gst_egueb_demux_video_factory,
      "video");
#if HAVE_GST_1
  gst_pad_set_event_function (pad,
      GST_DEBUG_FUNCPTR (gst_egueb_demux_video_event));
  gst_pad_set_query_function (pad,
      GST_DEBUG_FUNCPTR (gst_egueb_demux_video_query));
#else
  gst_pad_set_fixatecaps_function (pad,
      GST_DEBUG_FUNCPTR (gst_egueb_demux_video_fixate_caps));
  gst_pad_set_setcaps_function (pad,
      GST_DEBUG_FUNCPTR (gst_egueb_demux_video_set_caps));
  gst_pad_set_getcaps_function (pad,
      GST_DEBUG_FUNCPTR (gst_egueb_demux_video_get_caps));
  gst_pad_set_event_function (pad,
      GST_DEBUG_FUNCPTR (gst_egueb_demux_video_event_simple));
  gst_pad_set_query_function (pad,
      GST_DEBUG_FUNCPTR (gst_egueb_demux_video_query_simple));
#endif
  gst_element_add_pad (GST_ELEMENT (thiz), pad);

  /* our internal members */
  thiz->adapter = gst_adapter_new ();
  thiz->segment = gst_segment_new ();
  gst_segment_init (thiz->segment, GST_FORMAT_TIME);
  thiz->doc_lock = g_mutex_new ();
  /* initial seek segment position */
  thiz->seek = GST_CLOCK_TIME_NONE;
  thiz->next_ts = 0;
  thiz->send_ns = TRUE;
  /* set default properties */
  thiz->container_w = DEFAULT_CONTAINER_WIDTH;
  thiz->container_h = DEFAULT_CONTAINER_HEIGHT;
  thiz->background = enesim_renderer_background_new();
  enesim_renderer_background_color_set (thiz->background, 0xffffffff);
}

static void
gst_egueb_demux_class_init (GstEguebDemuxClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstBinClass *gstbin_class = GST_BIN_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstPadTemplate *sink_template;

  egueb_dom_init ();
  egueb_smil_init ();

  gobject_class->dispose = GST_DEBUG_FUNCPTR (gst_egueb_demux_dispose);
  gobject_class->set_property =
      GST_DEBUG_FUNCPTR (gst_egueb_demux_set_property);
  gobject_class->get_property =
      GST_DEBUG_FUNCPTR (gst_egueb_demux_get_property);

  gst_egueb_demux_parent_class = g_type_class_peek_parent (klass);

  element_class->change_state =
      GST_DEBUG_FUNCPTR (gst_egueb_demux_change_state);

  /* Properties */
  g_object_class_install_property (gobject_class, PROP_CONTAINER_WIDTH,
      g_param_spec_uint ("width", "Width",
          "Container width", 1, G_MAXUINT, 256,
          G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, PROP_CONTAINER_HEIGHT,
      g_param_spec_uint ("height", "Height",
          "Container height", 1, G_MAXUINT, 256,
          G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, PROP_URI,
      g_param_spec_string ("uri", "URI",
          "URI where the XML buffer was taken from",
          NULL, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, PROP_BACKGROUND_COLOR,
      g_param_spec_uint ("background-color", "Background Color",
          "Background color to use (big-endian ARGB)", 0, G_MAXUINT32,
          0, G_PARAM_READWRITE));

  /* initialize the element class */
  sink_template = gst_egueb_demux_get_sink_template ();
  gst_element_class_add_pad_template (element_class, sink_template);
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_egueb_demux_video_factory));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_egueb_demux_other_factory));

  gst_element_class_set_details_simple (element_class,
      "Egueb Parser/Demuxer/Decoder",
      "Codec/Demuxer",
      "Generates buffers with the egueb content rendered",
      "<enesim-devel@googlegroups.com>");
}
