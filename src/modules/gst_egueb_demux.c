
/* The idea of this element is that it should produce image buffers
 * with the rendered svg per frame
 * as properties we might have:
 * - framerate The framerate of the svg
 * Should it work in pull and push mode?
 * It should use the enesim buffer instead of normal gstbuffers to create
 * the buffers
 */

#include "gst_egueb_demux.h"
#include "gst_egueb_type.h"

GST_DEBUG_CATEGORY_EXTERN (gst_egueb_demux_debug);
#define GST_CAT_DEFAULT gst_egueb_demux_debug
#define parent_class gst_egueb_demux_parent_class

#define DEFAULT_CONTAINER_WIDTH 256
#define DEFAULT_CONTAINER_HEIGHT 256


G_DEFINE_TYPE (GstEguebDemux, gst_egueb_demux, GST_TYPE_ELEMENT);

/* Later, whenever egueb supports more than svg, we can
 * add more templates here
 */
static GstStaticPadTemplate gst_egueb_demux_sink_factory =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (SVG_MIME)
    );

static GstStaticPadTemplate gst_egueb_demux_src_factory =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw-rgb, "
        "framerate = (fraction) [ 0, MAX ], "
        "depth = 24, "
        "bpp = 32, "
        "endianness = (int)4321, "
        "red_mask = 0x0000ff00, "
        "green_mask = 0x00ff0000, "
        "blue_mask = 0xff000000, "
        "framerate = (fraction) [ 1, MAX], "
        "width = (int) [ 1, MAX ], "
        "height = (int) [ 1, MAX ]")
    );

static GstElementDetails gst_egueb_demux_details = {
  "Egueb SVG Parser/Demuxer/Decoder",
  "Codec/Demuxer",
  "Generates buffers with the SVG content rendered",
  "<enesim-devel@googlegroups.com>",
};

enum
{
  PROP_0,
  PROP_CONTAINER_WIDTH,
  PROP_CONTAINER_HEIGHT,
  PROP_BACKGROUND_COLOR,
  PROP_URI,
  /* FILL ME */
};

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
          gchar *absolute;
          absolute = g_build_filename (g_get_current_dir (), uri, NULL);
          uri = g_strdup_printf ("file://%s", absolute);
          g_free (absolute);
        } else {
          uri = g_strdup_printf ("file://%s", uri);
        }
      } else {
        uri = g_strdup (uri);
        g_free (scheme);
      }
    } else {
      GstPad *sink_pad;
      GstIterator *iter;

      /* iterate over the sink pads */
      iter = gst_element_iterate_sink_pads (GST_ELEMENT (parent));
      while (gst_iterator_next (iter,
              (gpointer) & sink_pad) != GST_ITERATOR_DONE) {
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

  pad = gst_element_get_static_pad (GST_ELEMENT (thiz), "src");
  peer = gst_pad_get_peer (pad);

  query = gst_query_new_uri ();
  if (gst_pad_query (peer, query)) {
    gst_query_parse_uri (query, &uri);
  }

  if (!uri) {
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

static void
gst_egueb_demux_buffer_free (void *data, void *user_data)
{
  Enesim_Buffer_Sw_Data *sdata = data;
  g_free (sdata->rgb888.plane0);
}

static Eina_Bool
gst_egueb_demux_damages_get_cb (Egueb_Dom_Feature *f EINA_UNUSED,
    Eina_Rectangle *area, void *data)
{
  GstEguebDemux * thiz = data;
  Eina_Rectangle *r;

  GST_LOG_OBJECT (thiz, "Damage added at %d %d -> %d %d", area->x, area->y,
      area->w, area->h);
  r = malloc (sizeof(Eina_Rectangle));
  *r = *area;
  thiz->damages = eina_list_append (thiz->damages, r);

  return EINA_TRUE;
}

static Eina_Bool
gst_egueb_demux_draw (GstEguebDemux * thiz)
{
  Eina_Rectangle *r;

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
    egueb_dom_feature_render_draw_list(thiz->render, thiz->s, ENESIM_ROP_BLEND,
        thiz->damages, 0, 0, NULL);
  } else {
    egueb_dom_feature_render_draw_list(thiz->render, thiz->s, ENESIM_ROP_FILL,
        thiz->damages, 0, 0, NULL);
  }

  g_mutex_unlock (thiz->doc_lock);

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
gst_egueb_demux_convert (GstEguebDemux * thiz, GstBuffer **buffer)
{
  Enesim_Buffer *eb;
  Eina_Rectangle clip;

  if (!*buffer) {
    Enesim_Buffer_Sw_Data sdata;
    GstBuffer *b;

    sdata.xrgb8888.plane0_stride = GST_ROUND_UP_4 (thiz->w * 4);
    sdata.xrgb8888.plane0 = (uint32_t *) g_new(guint8,
        sdata.xrgb8888.plane0_stride * thiz->h);

    eb = enesim_buffer_new_data_from (ENESIM_BUFFER_FORMAT_XRGB8888, thiz->w,
        thiz->h, EINA_FALSE, &sdata, gst_egueb_demux_buffer_free, NULL);
    enesim_buffer_ref (eb);

    b = gst_buffer_new ();
    GST_BUFFER_DATA (b) = (guint8 *)sdata.rgb888.plane0;
    GST_BUFFER_SIZE (b) = sdata.rgb888.plane0_stride * thiz->h;

    /* unref the buffer when done */
    GST_BUFFER_MALLOCDATA (b) = (guint8 *) eb;
    GST_BUFFER_FREE_FUNC (b) = (GFreeFunc ) enesim_buffer_unref;
    *buffer = b;
  } else {
    Enesim_Buffer_Sw_Data sdata;

    sdata.xrgb8888.plane0_stride = GST_ROUND_UP_4 (thiz->w * 4);
    sdata.xrgb8888.plane0 = (uint32_t *) GST_BUFFER_DATA (*buffer);
    eb = enesim_buffer_new_data_from (ENESIM_BUFFER_FORMAT_XRGB8888, thiz->w,
        thiz->h, EINA_FALSE, &sdata, NULL, NULL);
  }

  /* convert it to a buffer and send it */
  enesim_converter_surface (thiz->s, eb);
  enesim_buffer_unref (eb);
}


static void
gst_egueb_demux_loop (gpointer user_data)
{
  GstEguebDemux *thiz;
  GstFlowReturn ret;
  GstClockID id;
  GstBuffer *buf = NULL;
  GstPad *pad;
  GstClockTime next;
  Enesim_Buffer *eb;
  Enesim_Buffer_Sw_Data sw_data;
  gulong buffer_size;
  gulong new_buffer_size;
  gint fps;
  
  thiz = GST_EGUEB_DEMUX (user_data);
  pad = gst_element_get_static_pad (GST_ELEMENT (thiz), "src");

  GST_DEBUG_OBJECT (thiz, "Creating buffer at %" GST_TIME_FORMAT,
      GST_TIME_ARGS (thiz->last_ts));

  /* check if we need to update the new segment */
  if (thiz->animation) {
    Egueb_Smil_Clock clock;

    if (!egueb_smil_feature_animation_has_animations(thiz->animation)) {
      if (thiz->last_ts > 0) {
        GST_DEBUG ("No animations found, nothing else to push");
        goto eos;
      }
    } else if (egueb_smil_feature_animation_duration_get(thiz->animation, &clock)) {
      if (thiz->last_stop < clock) {
        //gst_base_src_new_seamless_segment (src, 0, clock, thiz->last_ts); 
      } else if (thiz->last_stop > clock) {
        GST_DEBUG ("EOS");
        goto eos;
      }
      thiz->last_stop = clock;
    }
  }

  /* check if we need to send the EOS */
  if (thiz->last_ts >= thiz->last_stop) {
    GST_DEBUG ("EOS reached, current: %" GST_TIME_FORMAT " stop: %" GST_TIME_FORMAT,
        GST_TIME_ARGS (thiz->last_ts), GST_TIME_ARGS (thiz->last_stop));
    goto eos;
  }

#if 0
  /* check if we need to seek */
  if (GST_CLOCK_TIME_IS_VALID (thiz->seek)) {
    gdouble seconds;

    seconds = thiz->seek / 1000000000.0;
    egueb_svg_element_svg_time_set (thiz->svg, seconds);
    thiz->last_ts = thiz->seek;
    thiz->seek = GST_CLOCK_TIME_NONE;
  }
#endif

  buffer_size = gst_egueb_demux_get_size (thiz);

  /* We need to check downstream if the caps have changed so we can
   * allocate an optimus size of surface
   */
  ret = gst_pad_alloc_buffer_and_set_caps (pad, thiz->last_ts,
      buffer_size, GST_PAD_CAPS (pad), &buf);
  if (ret == GST_FLOW_OK) {
    new_buffer_size = GST_BUFFER_SIZE (buf);
    buffer_size = gst_egueb_demux_get_size (thiz);
    if (new_buffer_size != buffer_size) {
      GST_ERROR_OBJECT (thiz, "different size %d %d", new_buffer_size, buffer_size);
      gst_buffer_unref (buf);
      buf = NULL;
    }
  } else {
    buf = NULL;
  }

  gst_egueb_demux_draw (thiz);

  if (thiz->animation) {
    egueb_smil_feature_animation_tick (thiz->animation);
  }

#if 0
  /* TODO add a property to inform when to send an EOS, like after
   * every animation has ended
   */

  /* we exit because we are done */
  if (!eb) {
    return GST_FLOW_UNEXPECTED;
  }
#endif

  gst_egueb_demux_convert (thiz, &buf);
  GST_DEBUG_OBJECT (thiz, "Sending buffer with ts: %" GST_TIME_FORMAT,
      GST_TIME_ARGS (thiz->last_ts));

  /* set the duration for the next buffer, this must be done after the tick in case
   * the QoS changed the fps on the svg
   */
  thiz->duration = gst_util_uint64_scale (GST_SECOND, 1, thiz->fps);
  /* set the timestamp and duration baesed on the last timestamp set */
  GST_BUFFER_DURATION (buf) = thiz->duration;
  GST_BUFFER_TIMESTAMP (buf) = thiz->last_ts;
  thiz->last_ts += GST_BUFFER_DURATION (buf);

  ret = gst_pad_push (pad, buf);
  if (ret != GST_FLOW_OK) {
    GST_ERROR ("wrong %d", ret);
  }

  goto done;

  /* TODO check return value */
  return;

eos:
  GST_DEBUG_OBJECT (thiz, "Sending EOS");
  gst_pad_push_event (pad, gst_event_new_eos ());
  gst_pad_pause_task (pad);

done:
  gst_object_unref (pad);
  return;
}


static gboolean
gst_egueb_demux_setup (GstEguebDemux * thiz, GstBuffer * buf)
{
  Enesim_Stream *s;
  Egueb_Dom_Node *doc = NULL;
  Egueb_Dom_Node *topmost;
  Egueb_Dom_Feature *render, *window, *ui;
  GstPad *pad;
  GstCaps *caps;
  gboolean ret = FALSE;
  gchar *data;

  /* check if we have a valid xml */
  if (!buf) {
    GST_ERROR_OBJECT (thiz, "No buffer received");
    goto beach;
  }

  /* create a buffer stream based on the input buffer */
  data = malloc(GST_BUFFER_SIZE (buf));
  memcpy (data, GST_BUFFER_DATA (buf), GST_BUFFER_SIZE (buf));
  /* the stream will free the data */
  s = enesim_stream_buffer_new (data, GST_BUFFER_SIZE (buf));
  gst_buffer_unref (buf);

  /* parse the document */
  egueb_dom_parser_parse (s, &doc);
  if (!doc) {
    GST_ERROR_OBJECT (thiz, "Failed parsing the document");
    goto beach;
  }

  /* The features are on the topmost element */
  /* TODO add events to know whenever the topmost element has changed */
  topmost = egueb_dom_document_document_element_get(doc);
  if (!topmost) {
    GST_ERROR_OBJECT (thiz, "Topmost element not found");
    goto no_topmost;
  }

  render = egueb_dom_node_feature_get(topmost, EGUEB_DOM_FEATURE_RENDER_NAME,
      NULL);
  if (!render)
  {
    GST_ERROR_OBJECT (thiz, "No 'render' feature found, nothing to do");
    goto no_render;
  }

  window = egueb_dom_node_feature_get(topmost, EGUEB_DOM_FEATURE_WINDOW_NAME,
      NULL);
  if (!window)
  {
    GST_ERROR_OBJECT (thiz, "No 'window' feature found, nothing to do");
    goto no_window;
  }

  thiz->doc = egueb_dom_node_ref(doc);
  thiz->topmost = egueb_dom_node_ref(topmost);
  thiz->render = egueb_dom_feature_ref(render);
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
  ui = egueb_dom_node_feature_get(thiz->topmost,
      EGUEB_DOM_FEATURE_UI_NAME, NULL);
  if (ui) {
    egueb_dom_feature_ui_input_get(ui, &thiz->input);
    egueb_dom_feature_unref(ui);
  }

  thiz->animation = egueb_dom_node_feature_get(thiz->topmost,
      EGUEB_SMIL_FEATURE_ANIMATION_NAME, NULL);

  /* setup our own gst egueb document */
  thiz->gdoc = gst_egueb_document_new (egueb_dom_node_ref(thiz->doc));
  gst_egueb_document_feature_io_setup (thiz->gdoc);
  ret = TRUE;

  /* set the caps, and start running */
  pad = gst_element_get_static_pad (GST_ELEMENT (thiz), "src");
  caps = gst_caps_copy (gst_pad_get_caps (pad));
  gst_pad_fixate_caps (pad, caps);
  gst_pad_set_caps (pad, caps);
  /* start pushing buffers */
  gst_pad_start_task (pad, gst_egueb_demux_loop, thiz);
  gst_object_unref (pad);

no_window:
  egueb_dom_feature_unref(render);
no_render:
  egueb_dom_node_unref(topmost);
no_topmost:
  egueb_dom_node_unref(doc);
beach:

  return ret;
}

static void
gst_egueb_demux_cleanup (GstEguebDemux * thiz)
{
  if (thiz->input) {
    egueb_dom_input_unref(thiz->input);
    thiz->input = NULL;
  }

  /* optional features */
  if (thiz->animation) {
    egueb_dom_feature_unref(thiz->animation);
    thiz->animation = NULL;
  }

  if (thiz->render) {
    egueb_dom_feature_unref(thiz->render);
    thiz->render = NULL;
  }

  if (thiz->window) {
    egueb_dom_feature_unref(thiz->window);
    thiz->window = NULL;
  }

  if (thiz->topmost) {
    egueb_dom_node_unref(thiz->topmost);
    thiz->doc = NULL;
  }

  if (thiz->doc) {
    egueb_dom_node_unref(thiz->doc);
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
}

static gboolean
gst_egueb_svg_parse_naviation (GstEguebDemux * thiz, GstEvent * event)
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
      //esvg_element_svg_feed_mouse_down(thiz->e, 0);
      break;
    case GST_NAVIGATION_EVENT_MOUSE_BUTTON_RELEASE:
      //esvg_element_svg_feed_mouse_up(thiz->e, 0);
      break;
    case GST_NAVIGATION_EVENT_MOUSE_MOVE:{
        gdouble x;
        gdouble y;

        gst_navigation_event_parse_mouse_move_event (event, &x, &y);
        GST_LOG_OBJECT (thiz, "Sending mouse at %g %g", x, y);
	egueb_dom_input_feed_mouse_move(thiz->input, x, y);
      }
      break;
    case GST_NAVIGATION_EVENT_COMMAND:
      break;
  }

  return TRUE;
}

static gboolean
gst_egueb_demux_sink_event (GstPad * pad, GstEvent * event)
{
  gboolean ret = FALSE;

  /* TODO later we need to handle FLUSH_START to flush the adapter too */
  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_EOS:
      {
        GstEguebDemux *thiz;
        GstBuffer *buf;
        gint len;

        thiz = GST_EGUEB_DEMUX (gst_pad_get_parent (pad));
        GST_DEBUG_OBJECT (thiz, "Received EOS");
        /* The EOS should come from upstream whenever the xml file
         * has been readed completely
         */
        len = gst_adapter_available (thiz->adapter);
        buf = gst_adapter_take_buffer (thiz->adapter, len);

        /* get the location */
        gst_egueb_demux_setup (thiz, buf);
        gst_event_unref (event);
        gst_object_unref (thiz);
        ret = TRUE;
      }
      break;

    default:
      break;
  }

  return ret;
}

static GstFlowReturn
gst_egueb_demux_sink_chain (GstPad * pad, GstBuffer * buffer)
{
  GstEguebDemux *thiz;
  GstFlowReturn ret = GST_FLOW_OK;

  thiz = GST_EGUEB_DEMUX (gst_pad_get_parent (pad));
  GST_DEBUG_OBJECT (thiz, "Received buffer");
  gst_adapter_push (thiz->adapter, gst_buffer_ref (buffer));
  gst_object_unref (thiz);

  return ret;
}

static gboolean
gst_egueb_demux_src_event (GstPad * pad, GstEvent * event)
{
  GstEguebDemux *thiz;
  gboolean ret = FALSE;

  thiz = GST_EGUEB_DEMUX (gst_pad_get_parent (pad));
  GST_DEBUG_OBJECT (thiz, "Received event '%s'", GST_EVENT_TYPE_NAME (event));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_QOS:{
    GstQOSType type;
    GstClockTimeDiff diff;
    GstClockTime timestamp;
    gdouble proportion;
    gint fps_n;
    gint fps_d;
    gint fps;

    /* Whenever we receive a QoS we can decide to increase the fps
     * on egueb and send more intermediate frames
     */
    gst_event_parse_qos_full (event, &type, &proportion, &diff, &timestamp);
    fps_d = thiz->spf_n;
    fps_n = thiz->spf_d;

    if (proportion > 1.0) {
      fps_d = fps_d * proportion;
    } else {
      fps_n = fps_n + ((1 - proportion) * fps_n);
    }

    fps = gst_util_uint64_scale (1, fps_n, fps_d);
    if (fps < 1) fps = 1;
    GST_DEBUG_OBJECT (thiz, "Updating framerate to %d/%d", fps_n, fps_d);
    thiz->fps = fps;
    egueb_smil_feature_animation_fps_set(thiz->animation, fps);
    }
    break;

    case GST_EVENT_NAVIGATION:
    g_mutex_lock (thiz->doc_lock);
    ret = gst_egueb_svg_parse_naviation (thiz, event);
    g_mutex_unlock (thiz->doc_lock);
    break;

    default:
    break;
  }

  gst_object_unref (thiz);

  return ret;
}

static gboolean
gst_egueb_demux_src_query (GstPad * pad, GstQuery * query)
{
  GstEguebDemux *thiz;
  gboolean ret = FALSE;

  thiz = GST_EGUEB_DEMUX (gst_pad_get_parent (pad));
  GST_DEBUG_OBJECT (thiz, "Received query '%s'", GST_QUERY_TYPE_NAME (query));

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_DURATION:
    if (thiz->last_stop >= 0) {
      gst_query_set_duration(query, GST_FORMAT_TIME, thiz->last_stop); 
      ret = TRUE;
    }
    break;

    default:
    break;
  }

  gst_object_unref (thiz);

  return ret;
}

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

static void
gst_egueb_demux_src_fixate_caps (GstPad * pad, GstCaps * caps)
{
  GstEguebDemux *thiz;
  GstStructure *structure;
  gint i;

  thiz = GST_EGUEB_DEMUX (gst_pad_get_parent (pad));

  GST_DEBUG_OBJECT (thiz, "Fixating caps");
  for (i = 0; i < gst_caps_get_size (caps); ++i) {
    structure = gst_caps_get_structure (caps, i);

    /* in case the width or height are still not-fixed use the default size */
    gst_structure_fixate_field_nearest_int (structure, "width",
        thiz->container_w);
    gst_structure_fixate_field_nearest_int (structure, "height",
        thiz->container_h);
    /* fixate the framerate in case nobody has set it */
    gst_structure_fixate_field_nearest_fraction (structure, "framerate", 30, 1);
  }

  gst_object_unref (thiz);
}

static GstCaps *
gst_egueb_demux_src_get_caps (GstPad * pad)
{
  GstEguebDemux *thiz;
  GstCaps *caps;
  Egueb_Dom_Feature_Window_Type type;
  gint i;
  int cw, ch;

  thiz = GST_EGUEB_DEMUX (gst_pad_get_parent (pad));

  GST_DEBUG_OBJECT (thiz, "Getting caps");

  caps = gst_caps_copy (gst_pad_get_pad_template_caps (pad));

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
    egueb_dom_feature_window_content_size_set(thiz->window, 0, 0);
    egueb_dom_feature_window_content_size_get(thiz->window, &cw, &ch);
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
  GST_DEBUG_OBJECT (thiz, "Returning caps %" GST_PTR_FORMAT, caps);
  gst_object_unref (thiz);

  return caps;
}

static gboolean
gst_egueb_demux_src_set_caps (GstPad * pad, GstCaps * caps)
{
  GstEguebDemux * thiz;
  GstStructure *s;
  const GValue *framerate;
  gint width;
  gint height;

  thiz = GST_EGUEB_DEMUX (gst_pad_get_parent (pad));
  GST_DEBUG_OBJECT (thiz, "Setting caps");

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

static GstStateChangeReturn
gst_egueb_demux_change_state (GstElement * element, GstStateChange transition)
{
  GstEguebDemux *thiz = GST_EGUEB_DEMUX (element);
  GstStateChangeReturn ret;

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      break;

    /* before calling the parent descriptor for this, be sure to unlock
     * the create function
     */
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      thiz->done = TRUE;
      break;

    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    default:
      break;
  }

beach:
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
  GST_CALL_PARENT (G_OBJECT_CLASS, dispose, (object));

  if (thiz->adapter) {
    g_object_unref (thiz->adapter);
    thiz->adapter = NULL;
  }

  egueb_smil_shutdown ();
  egueb_dom_shutdown ();
}

static void
gst_egueb_demux_init (GstEguebDemux * thiz)
{
  GstPad *pad;

  egueb_dom_init ();
  egueb_smil_init ();

  /* Create the pads */
  pad = gst_pad_new_from_static_template (&gst_egueb_demux_sink_factory,
      "sink");
  gst_pad_set_event_function (pad,
      GST_DEBUG_FUNCPTR (gst_egueb_demux_sink_event));
  gst_pad_set_chain_function (pad,
      GST_DEBUG_FUNCPTR (gst_egueb_demux_sink_chain));
  gst_element_add_pad (GST_ELEMENT (thiz), pad);

  pad = gst_pad_new_from_static_template (&gst_egueb_demux_src_factory,
      "src");
  gst_pad_set_setcaps_function (pad,
      GST_DEBUG_FUNCPTR (gst_egueb_demux_src_set_caps));
  gst_pad_set_getcaps_function (pad,
      GST_DEBUG_FUNCPTR (gst_egueb_demux_src_get_caps));
  gst_pad_set_fixatecaps_function (pad,
      GST_DEBUG_FUNCPTR (gst_egueb_demux_src_fixate_caps));
  gst_pad_set_event_function (pad,
      GST_DEBUG_FUNCPTR (gst_egueb_demux_src_event));
  gst_pad_set_query_function (pad,
      GST_DEBUG_FUNCPTR (gst_egueb_demux_src_query));
  gst_element_add_pad (GST_ELEMENT (thiz), pad);

  /* our internal members */
  thiz->adapter = gst_adapter_new ();
  thiz->doc_lock = g_mutex_new ();
  /* initial seek segment position */
  thiz->seek = GST_CLOCK_TIME_NONE;
  thiz->last_ts = 0;
  thiz->last_stop = -1;
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
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gobject_class->dispose = GST_DEBUG_FUNCPTR (gst_egueb_demux_dispose);
  gobject_class->set_property =
      GST_DEBUG_FUNCPTR (gst_egueb_demux_set_property);
  gobject_class->get_property =
      GST_DEBUG_FUNCPTR (gst_egueb_demux_get_property);

  parent_class = g_type_class_peek_parent (klass);

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
  /* TODO add the templates based on the implementation mime */
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_egueb_demux_sink_factory));
  gst_element_class_set_details (element_class, &gst_egueb_demux_details);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_egueb_demux_src_factory));
  gst_element_class_set_details (element_class, &gst_egueb_demux_details);
}
