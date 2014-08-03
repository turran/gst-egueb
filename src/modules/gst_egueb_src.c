#include "gst_egueb_src.h"
#include "gst_egueb_type.h"

GST_DEBUG_CATEGORY_EXTERN (gst_egueb_src_debug);
#define GST_CAT_DEFAULT gst_egueb_src_debug

GST_BOILERPLATE (GstEguebSrc, gst_egueb_src, GstBaseSrc,
    GST_TYPE_BASE_SRC);

/* Later, whenever egueb supports more than svg, we can
 * add more templates here
 */
static GstStaticPadTemplate gst_egueb_src_src_factory =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw-rgb, "
        "framerate = (fraction) [ 0, MAX ], "
        "depth = 24, bpp = 32, "
        "width = (int) [ 1, MAX ], height = (int) [ 1, MAX ]")
    );

static GstElementDetails gst_egueb_src_details = {
  "Egueb SVG Source",
  "Sink",
  "Renders SVG content using Egueb",
  "<enesim-devel@googlegroups.com>",
};

enum
{
  PROP_0,
  PROP_XML,
  PROP_CONTAINER_WIDTH,
  PROP_CONTAINER_HEIGHT,
  PROP_BACKGROUND_COLOR,
  PROP_URI,
  /* FILL ME */
};

static void
gst_egueb_src_buffer_free (void *data, void *user_data)
{
  Enesim_Buffer_Sw_Data *sdata = data;
  g_free (sdata->rgb888.plane0);
}

static gboolean
gst_egueb_src_setup (GstEguebSrc * thiz)
{
  Enesim_Stream *s;
  Egueb_Dom_Node *doc = NULL;
  Egueb_Dom_Feature *render, *window, *ui;
  Egueb_Dom_String *uri;
  gchar *data;

  /* check if we have a valid xml */
  if (!thiz->xml) {
    GST_WARNING_OBJECT (thiz, "No XML set");
    return FALSE;
  }

  /* parse the document */
  data = malloc(GST_BUFFER_SIZE (thiz->xml));
  memcpy (data, GST_BUFFER_DATA (thiz->xml), GST_BUFFER_SIZE (thiz->xml));
  /* the stream will free the data */
  s = enesim_stream_buffer_new (data, GST_BUFFER_SIZE (thiz->xml));

  egueb_dom_parser_parse (s, &doc);
  enesim_stream_unref (s);

  if (!doc) {
    GST_ERROR_OBJECT (thiz, "Failed parsing the document");
    return FALSE;
  }

  render = egueb_dom_node_feature_get(doc, EGUEB_DOM_FEATURE_RENDER_NAME,
      NULL);
  if (!render)
  {
    GST_ERROR_OBJECT (thiz, "No 'render' feature found, nothing to do");
    egueb_dom_node_unref(doc);
    return FALSE;
  }

  window = egueb_dom_node_feature_get(doc, EGUEB_DOM_FEATURE_WINDOW_NAME,
      NULL);
  if (!window)
  {
    GST_ERROR_OBJECT (thiz, "No 'window' feature found, nothing to do");
    egueb_dom_feature_unref(render);
    egueb_dom_node_unref(doc);
    return FALSE;
  }

  thiz->doc = doc;
  thiz->render = render;
  thiz->window = window;
  /* set the uri */
  if (thiz->location) {
    uri = egueb_dom_string_new_with_string(thiz->location);
    egueb_dom_document_uri_set(thiz->doc, uri);
  }

  /* optional features */
  ui = egueb_dom_node_feature_get(thiz->doc,
      EGUEB_DOM_FEATURE_UI_NAME, NULL);
  if (ui) {
    egueb_dom_feature_ui_input_get(ui, &thiz->input);
    egueb_dom_feature_unref(ui);
  }

  thiz->animation = egueb_dom_node_feature_get(thiz->doc,
      EGUEB_SMIL_FEATURE_ANIMATION_NAME, NULL);
  thiz->io = egueb_dom_node_feature_get(thiz->doc,
      EGUEB_DOM_FEATURE_IO_NAME, NULL);
  /* TODO for now we make the feature itself handle the io */
  if (thiz->io)
    egueb_dom_feature_io_default_enable(thiz->io, EINA_TRUE);

  return TRUE;
}


static void
gst_egueb_src_cleanup (GstEguebSrc * thiz)
{
  if (thiz->input) {
    egueb_dom_input_unref(thiz->input);
    thiz->input = NULL;
  }

  /* optional features */
  if (thiz->io) {
    egueb_dom_feature_unref(thiz->io);
    thiz->io = NULL;
  }

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

  if (thiz->doc) {
    egueb_dom_node_unref(thiz->doc);
    thiz->doc = NULL;
  }


  if (thiz->location) {
    g_free (thiz->location);
    thiz->location = NULL;
  }

  if (thiz->xml) {
    gst_buffer_unref (thiz->xml);
    thiz->xml = NULL;
  }
}

static Eina_Bool
gst_egueb_src_damages_get_cb (Egueb_Dom_Feature *f EINA_UNUSED,
    Eina_Rectangle *area, void *data)
{
  GstEguebSrc * thiz = data;
  Eina_Rectangle *r;

  GST_LOG_OBJECT (thiz, "Damage added at %d %d -> %d %d", area->x, area->y,
      area->w, area->h);
  r = malloc (sizeof(Eina_Rectangle));
  *r = *area;
  thiz->damages = eina_list_append (thiz->damages, r);

  return EINA_TRUE;
}

static Eina_Bool
gst_egueb_src_draw (GstEguebSrc * thiz)
{
  Eina_Rectangle *r;

  /* draw with the document locked */
  g_mutex_lock (thiz->doc_lock);

  egueb_dom_document_process(thiz->doc);
  egueb_dom_feature_render_damages_get(thiz->render, thiz->s,
				gst_egueb_src_damages_get_cb, thiz);
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
gst_egueb_src_get_size (GstEguebSrc * thiz)
{
  return GST_ROUND_UP_4 (thiz->w * thiz->h * 4);
}

static void
gst_egueb_src_convert (GstEguebSrc * thiz, GstBuffer **buffer)
{
  Enesim_Buffer *eb;
  Eina_Rectangle clip;

  if (!*buffer) {
    Enesim_Buffer_Sw_Data sdata;
    GstBuffer *b;

    sdata.xrgb8888.plane0_stride = GST_ROUND_UP_4 (thiz->w * 4);
    sdata.xrgb8888.plane0 = g_new(guint8, sdata.xrgb8888.plane0_stride * thiz->h);

    eb = enesim_buffer_new_data_from (ENESIM_BUFFER_FORMAT_XRGB8888, thiz->w,
        thiz->h, EINA_FALSE, &sdata, gst_egueb_src_buffer_free, NULL);
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
    sdata.xrgb8888.plane0 = GST_BUFFER_DATA (*buffer);
    eb = enesim_buffer_new_data_from (ENESIM_BUFFER_FORMAT_XRGB8888, thiz->w,
        thiz->h, EINA_FALSE, &sdata, NULL, NULL);
  }

  /* convert it to a buffer and send it */
  enesim_converter_surface (thiz->s, eb);
  enesim_buffer_unref (eb);
}

static gboolean
gst_egueb_svg_parse_naviation (GstEguebSrc * thiz, GstEvent * event)
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

/*----------------------------------------------------------------------------*
 *                           BaseSrc interface                                *
 *----------------------------------------------------------------------------*/
static gboolean
gst_egueb_src_event (GstBaseSrc * src, GstEvent * event)
{
  GstEguebSrc *thiz = GST_EGUEB_SRC (src);
  gboolean ret = FALSE;

  GST_DEBUG_OBJECT (thiz, "event %s", GST_EVENT_TYPE_NAME (event));
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

  /* pass to the base class */
  if (!ret)
    ret = GST_BASE_SRC_CLASS (parent_class)->event (src, event);

  return ret;
}

static gboolean
gst_egueb_src_query (GstBaseSrc * src, GstQuery * query)
{
  GstEguebSrc *thiz = GST_EGUEB_SRC (src);
  gboolean ret = FALSE;

  GST_DEBUG ("query %s", GST_QUERY_TYPE_NAME (query));
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

  if (!ret)
    ret = GST_BASE_SRC_CLASS (parent_class)->query (src, query);

  return ret;
}

static gboolean
gst_egueb_src_prepare_seek_segment (GstBaseSrc *src, GstEvent *seek,
    GstSegment *segment)
{
  GST_ERROR ("prepare seek");
  return TRUE;
}

static gboolean
gst_egueb_src_do_seek (GstBaseSrc *src, GstSegment *segment)
{
  GstEguebSrc *thiz = GST_EGUEB_SRC (src);

  g_mutex_lock (thiz->doc_lock);
  /* TODO find the nearest keyframe based on the fps */
  GST_DEBUG_OBJECT (src, "do seek at %" GST_TIME_FORMAT, GST_TIME_ARGS (segment->start));
  /* do the seek on the svg element */
  thiz->seek = segment->start;
  g_mutex_unlock (thiz->doc_lock);

  return TRUE;
}

static gboolean
gst_egueb_src_is_seekable (GstBaseSrc *src)
{
  return TRUE;
}

static void
gst_egueb_src_fixate (GstBaseSrc * src, GstCaps * caps)
{
  GstEguebSrc *thiz = GST_EGUEB_SRC (src);
  gint i;
  GstStructure *structure;

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
}

static GstCaps *
gst_egueb_src_get_caps (GstBaseSrc * src)
{
  GstEguebSrc * thiz = GST_EGUEB_SRC (src);
  GstCaps *caps;
  GstStructure *s;
  Egueb_Dom_Feature_Window_Type type;
  int cw, ch;

  if (!thiz->doc) {
    GST_DEBUG_OBJECT (thiz, "Can not get caps without a parsed document");
    return gst_caps_copy (gst_pad_get_pad_template_caps (src->srcpad));
  }

#if 0
  /* For ARGB888 */
      "alpha_mask", G_TYPE_INT, 0x000000ff,
      "red_mask", G_TYPE_INT, 0x0000ff00,
      "green_mask", G_TYPE_INT, 0x00ff0000,
      "blue_mask", G_TYPE_INT, 0xff000000,
#endif

  /* create our own structure */
  s = gst_structure_new ("video/x-raw-rgb",
      "bpp", G_TYPE_INT, 32,
      "depth", G_TYPE_INT, 24,
      "endianness", G_TYPE_INT, G_BIG_ENDIAN,
      "red_mask", G_TYPE_INT, 0x0000ff00,
      "green_mask", G_TYPE_INT, 0x00ff0000,
      "blue_mask", G_TYPE_INT, 0xff000000,
      "framerate", GST_TYPE_FRACTION_RANGE, 1, G_MAXINT, G_MAXINT, 1,
      NULL);

  if (!egueb_dom_feature_window_type_get (thiz->window, &type)) {
    GST_WARNING_OBJECT (thiz, "Impossible to get the type of the window");
    return gst_caps_copy (gst_pad_get_pad_template_caps (src->srcpad));
  }

  if (type == EGUEB_DOM_FEATURE_WINDOW_TYPE_SLAVE) {
    GST_ERROR_OBJECT (thiz, "Not supported yet");
    return gst_caps_copy (gst_pad_get_pad_template_caps (src->srcpad));
  } else {
    egueb_dom_feature_window_content_size_set(thiz->window, thiz->container_w,
        thiz->container_h);
    egueb_dom_feature_window_content_size_get(thiz->window, &cw, &ch);
  }

  if (cw <= 0 || ch <= 0) {
    GST_WARNING_OBJECT (thiz, "Invalid size of the window %d %d", cw, ch);
    return gst_caps_copy (gst_pad_get_pad_template_caps (src->srcpad));
  }

  gst_structure_set (s, "width", G_TYPE_INT, cw, NULL);
  gst_structure_set (s, "height", G_TYPE_INT, ch, NULL);

#if 0
  GST_DEBUG_OBJECT (thiz, "Using a range for the height");
  gst_structure_set (s, "height", GST_TYPE_INT_RANGE, 1, G_MAXINT, NULL);
  GST_DEBUG_OBJECT (thiz, "Using a range for the width");
  gst_structure_set (s, "width", GST_TYPE_INT_RANGE, 1, G_MAXINT, NULL);
#endif

  caps = gst_caps_new_full (s, NULL);

  return caps;
}

static gboolean
gst_egueb_src_set_caps (GstBaseSrc * src, GstCaps * caps)
{
  GstEguebSrc * thiz = GST_EGUEB_SRC (src);
  GstStructure *s;
  const GValue *framerate;
  gint width = thiz->w;
  gint height = thiz->h;

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

    thiz->s = enesim_surface_new (ENESIM_FORMAT_ARGB8888, width, height);
    thiz->w = width;
    thiz->h = height;

    GST_INFO_OBJECT (thiz, "Setting size to %dx%d", width, height);
    egueb_dom_feature_window_content_size_set (thiz->window, width, height);
  }

  return TRUE;
}

static GstFlowReturn
gst_egueb_src_create (GstBaseSrc * src, guint64 offset, guint size,
    GstBuffer ** buf)
{
  GstEguebSrc *thiz = GST_EGUEB_SRC (src);
  GstFlowReturn ret;
  GstClockID id;
  GstBuffer *outbuf = NULL;
  GstClockTime next;
  Enesim_Buffer *eb;
  Enesim_Buffer_Sw_Data sw_data;
  gulong buffer_size;
  gulong new_buffer_size;
  gint fps;
  
  GST_DEBUG_OBJECT (thiz, "Creating %" GST_TIME_FORMAT, GST_TIME_ARGS (offset));

  /* check if we need to update the new segment */
  if (thiz->animation) {
    Egueb_Smil_Clock clock;

    if (!egueb_smil_feature_animation_has_animations(thiz->animation)) {
      if (thiz->last_ts > 0) {
        GST_DEBUG ("No animations found, nothing else to push");
        return GST_FLOW_UNEXPECTED;
      }
    } else if (egueb_smil_feature_animation_duration_get(thiz->animation, &clock)) {
      if (thiz->last_stop < clock) {
        gst_base_src_new_seamless_segment (src, 0, clock, thiz->last_ts); 
      } else if (thiz->last_stop > clock) {
        GST_DEBUG ("EOS");
        return GST_FLOW_UNEXPECTED;
      }
      thiz->last_stop = clock;
    }
  }

  /* check if we need to send the EOS */
  if (thiz->last_ts >= thiz->last_stop) {
        GST_DEBUG ("EOS reached, current: %" GST_TIME_FORMAT " stop: %" GST_TIME_FORMAT,
            GST_TIME_ARGS (thiz->last_ts), GST_TIME_ARGS (thiz->last_stop));
        return GST_FLOW_UNEXPECTED;
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

  buffer_size = gst_egueb_src_get_size (thiz);

  /* We need to check downstream if the caps have changed so we can
   * allocate an optimus size of surface
   */
  ret = gst_pad_alloc_buffer_and_set_caps (GST_BASE_SRC_PAD (src), src->offset,
      buffer_size, GST_PAD_CAPS (GST_BASE_SRC_PAD (src)), &outbuf);
  if (ret == GST_FLOW_OK) {
    new_buffer_size = GST_BUFFER_SIZE (outbuf);
    buffer_size = gst_egueb_src_get_size (thiz);
    if (new_buffer_size != buffer_size) {
      GST_ERROR_OBJECT (thiz, "different size %d %d", new_buffer_size, buffer_size);
      gst_buffer_unref (outbuf);
      outbuf = NULL;
    }
  } else {
    outbuf = NULL;
  }

  gst_egueb_src_draw (thiz);

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

  gst_egueb_src_convert (thiz, &outbuf);
  GST_DEBUG_OBJECT (thiz, "Sending buffer with ts: %" GST_TIME_FORMAT, GST_TIME_ARGS (thiz->last_ts));

  /* set the duration for the next buffer, this must be done after the tick in case
   * the QoS changed the fps on the svg
   */
  thiz->duration = gst_util_uint64_scale (GST_SECOND, 1, thiz->fps);
  /* set the timestamp and duration baesed on the last timestamp set */
  GST_BUFFER_DURATION (outbuf) = thiz->duration;
  GST_BUFFER_TIMESTAMP (outbuf) = thiz->last_ts;
  thiz->last_ts += GST_BUFFER_DURATION (outbuf);

  *buf = outbuf;

  return GST_FLOW_OK;
}

static void
gst_egueb_src_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstEguebSrc * thiz = GST_EGUEB_SRC (object);

  switch (prop_id) {
    case PROP_XML:
      gst_value_set_buffer (value, thiz->xml);
      break;
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
gst_egueb_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstEguebSrc * thiz = GST_EGUEB_SRC (object);

  switch (prop_id) {
    case PROP_XML:
      thiz->xml = gst_buffer_ref (gst_value_get_buffer (value));
      break;
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
      const gchar *location;

      if (thiz->location) {
        g_free (thiz->location);
        thiz->location = NULL;
      }
      location = g_value_get_string (value);
      if (location) {
        thiz->location = g_strdup (location);
      }
      break;
    }
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstStateChangeReturn
gst_egueb_src_change_state (GstElement * element, GstStateChange transition)
{
  GstEguebSrc *thiz = GST_EGUEB_SRC (element);
  GstStateChangeReturn ret;

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      if (!gst_egueb_src_setup (thiz)) {
        ret = GST_STATE_CHANGE_FAILURE;
        goto beach;
      }
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
gst_egueb_src_dispose (GObject * object)
{
  GstEguebSrc *thiz = GST_EGUEB_SRC (object);

  GST_DEBUG_OBJECT (thiz, "Disposing");
  gst_egueb_src_cleanup (thiz);

  enesim_renderer_unref(thiz->background);

  if (thiz->doc_lock)
    g_mutex_free (thiz->doc_lock);
  GST_CALL_PARENT (G_OBJECT_CLASS, dispose, (object));

  egueb_smil_shutdown ();
  egueb_dom_shutdown ();
}

static void
gst_egueb_src_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);
  GstBaseSrcClass *base_class = GST_BASE_SRC_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_egueb_src_src_factory));
  gst_element_class_set_details (element_class, &gst_egueb_src_details);

  /* set virtual pointers */
  base_class->create = gst_egueb_src_create;
  base_class->set_caps = gst_egueb_src_set_caps;
  base_class->get_caps = gst_egueb_src_get_caps;
  base_class->fixate = gst_egueb_src_fixate;
  base_class->event = gst_egueb_src_event;
  base_class->query = gst_egueb_src_query;
  base_class->is_seekable = gst_egueb_src_is_seekable;
  base_class->prepare_seek_segment = gst_egueb_src_prepare_seek_segment;
  base_class->do_seek = gst_egueb_src_do_seek;
}

static void
gst_egueb_src_init (GstEguebSrc * thiz,
    GstEguebSrcClass * g_class)
{
  egueb_dom_init ();
  egueb_smil_init ();
  /* make it work in time */
  gst_base_src_set_format (GST_BASE_SRC (thiz), GST_FORMAT_TIME);
  thiz->doc_lock = g_mutex_new ();
  /* initial seek segment position */
  thiz->seek = GST_CLOCK_TIME_NONE;
  thiz->last_ts = 0;
  thiz->last_stop = -1;
  /* set default properties */
  thiz->container_w = 256;
  thiz->container_h = 256;
  thiz->background = enesim_renderer_background_new();
  enesim_renderer_background_color_set (thiz->background, 0x00000000);
}

static void
gst_egueb_src_class_init (GstEguebSrcClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);

  gobject_class->dispose = GST_DEBUG_FUNCPTR (gst_egueb_src_dispose);
  gobject_class->set_property =
      GST_DEBUG_FUNCPTR (gst_egueb_src_set_property);
  gobject_class->get_property =
      GST_DEBUG_FUNCPTR (gst_egueb_src_get_property);

  parent_class = g_type_class_peek_parent (klass);

  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_egueb_src_change_state);

  /* Properties */
  g_object_class_install_property (gobject_class, PROP_XML,
      gst_param_spec_mini_object ("xml", "XML",
          "XML Buffer",
          GST_TYPE_BUFFER, G_PARAM_READWRITE));
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
}
