/* Gst Egueb - Gstreamer based plugins and libs for Egueb
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

#include <Egueb_Dom.h>
#include <gst/gst.h>
/*============================================================================*
 *                                  Local                                     *
 *============================================================================*/
#define GST_EGUEB_LOG_COLOR_DEFAULT EINA_COLOR_ORANGE
/* Whenever a file needs to generate a log, it must declare this first */

#ifdef ERR
# undef ERR
#endif
#define ERR(...) EINA_LOG_DOM_ERR(_egueb_svg_video_provider_gstreamer_log, __VA_ARGS__)

#ifdef WARN
# undef WARN
#endif
#define WARN(...) EINA_LOG_DOM_WARN(_egueb_svg_video_provider_gstreamer_log, __VA_ARGS__)

#ifdef INFO
# undef INFO
#endif
#define INFO(...) EINA_LOG_DOM_INFO(_egueb_svg_video_provider_gstreamer_log, __VA_ARGS__)

#ifdef DBG
# undef DBG
#endif
#define DBG(...) EINA_LOG_DOM_DBG(_egueb_svg_video_provider_gstreamer_log, __VA_ARGS__)

#if 0
/* FIXME we still need to register the log domain */
static int _egueb_svg_video_provider_gstreamer_log = -1;
#endif


typedef struct _Gst_Egueb_Video_Provider
{
	Egueb_Dom_Video_Provider *vp;
	GstElement *playbin2;
	Enesim_Renderer *image;
} Gst_Egueb_Video_Provider;

static void _gst_egueb_video_provider_buffer_free(void *data, void *user_data)
{
	GstBuffer *buffer = user_data;
	gst_buffer_unref(buffer);
}

static void _gst_egueb_video_provider_fakesink_handoff_cb(GstElement *object,
		GstBuffer *buf, GstPad *pad, gpointer data)
{
	Gst_Egueb_Video_Provider *thiz = data;
	GstCaps *caps;
	const GstStructure *s;
	gint width, height;
	Enesim_Surface *surface;

	caps = gst_buffer_get_caps(buf);
	s = gst_caps_get_structure(caps, 0);
	/* get the width and height */
	gst_structure_get_int(s, "width", &width);
	gst_structure_get_int(s, "height", &height);
	gst_caps_unref(caps);

	/* lock the renderer */
	enesim_renderer_lock(thiz->image);
	/* set the new surface */
	surface = enesim_surface_new_data_from(ENESIM_FORMAT_ARGB8888,
			width, height, EINA_FALSE, GST_BUFFER_DATA(buf),
			GST_ROUND_UP_4(width * 4), _gst_egueb_video_provider_buffer_free, gst_buffer_ref(buf));
	enesim_renderer_image_source_surface_set(thiz->image, surface);
	/* unlock the renderer */
	enesim_renderer_unlock(thiz->image);
}

/* We will use this to notify egueb about some changes (buffering, state, error, whatever) */
static gboolean _gst_egueb_video_provider_bus_watch(GstBus *bus, GstMessage *msg, gpointer data)
{
	Gst_Egueb_Video_Provider *thiz = data;

	if (msg->src != (gpointer) thiz->playbin2)
		return TRUE;

	switch (GST_MESSAGE_TYPE (msg)) {
		case GST_MESSAGE_EOS:
		break;

		case GST_MESSAGE_STATE_CHANGED:
		break;

		default:
		break;
	}
	return TRUE;
}

/*----------------------------------------------------------------------------*
 *                       The Video provider interface                         *
 *----------------------------------------------------------------------------*/
static void * _gst_egueb_video_provider_descriptor_create(void)
{
	Gst_Egueb_Video_Provider *thiz;
	GstElement *fakesink, *capsfilter, *sink;
	GstBus *bus;
	GstPad *pad, *ghost_pad;
	GstCaps *caps;

	thiz = calloc(1, sizeof(Gst_Egueb_Video_Provider));

	sink = gst_bin_new(NULL);

	/* force a rgb32 bpp */
	capsfilter = gst_element_factory_make("capsfilter", NULL);
	caps = gst_caps_new_simple ("video/x-raw-rgb",
			"depth", G_TYPE_INT, 24, "bpp", G_TYPE_INT, 32,
			"endianness", G_TYPE_INT, G_BIG_ENDIAN,
			"red_mask", G_TYPE_INT, 0x0000ff00,
			"green_mask", G_TYPE_INT, 0x00ff0000,
			"blue_mask", G_TYPE_INT, 0xff000000,
			NULL);
	g_object_set(capsfilter, "caps", caps, NULL);

	/* define a new sink based on fakesink to catch up every buffer */
	fakesink = gst_element_factory_make("fakesink", NULL);
	g_object_set(fakesink, "sync", TRUE, "signal-handoffs", TRUE, NULL);
	g_signal_connect(G_OBJECT(fakesink), "handoff",
			G_CALLBACK(_gst_egueb_video_provider_fakesink_handoff_cb),
			thiz); 

	gst_bin_add_many(GST_BIN(sink), capsfilter, fakesink, NULL);
	gst_element_link(capsfilter, fakesink);
	/* Create ghost pad and link it to the capsfilter */
	pad = gst_element_get_static_pad (capsfilter, "sink");
	ghost_pad = gst_ghost_pad_new ("sink", pad);
	gst_pad_set_active (ghost_pad, TRUE);
	gst_element_add_pad (GST_ELEMENT(sink), ghost_pad);
	gst_object_unref (pad);

	thiz->playbin2 = gst_element_factory_make("playbin2", NULL);
	/* we add a message handler */
	bus = gst_pipeline_get_bus (GST_PIPELINE (thiz->playbin2));
	gst_bus_add_watch (bus, _gst_egueb_video_provider_bus_watch, thiz);
	gst_object_unref (bus);

	/* finally set the sink */
	g_object_set(thiz->playbin2, "video-sink", sink, NULL);

	return thiz;
}

static void _gst_egueb_video_provider_descriptor_destroy(void *data)
{
	Gst_Egueb_Video_Provider *thiz = data;

	gst_element_set_state(thiz->playbin2, GST_STATE_NULL);
	gst_object_unref(thiz->playbin2);

	if (thiz->image)
	{
		enesim_renderer_unref(thiz->image);
		thiz->image = NULL;
	}
	free(thiz);
}

static void _gst_egueb_video_provider_descriptor_open(void *data, Egueb_Dom_String *uri)
{
	Gst_Egueb_Video_Provider *thiz = data;

	/* the uri that comes from the api must be absolute */
	gst_element_set_state(thiz->playbin2, GST_STATE_READY);
	g_object_set(thiz->playbin2, "uri", egueb_dom_string_string_get(uri), NULL);
}

static void _gst_egueb_video_provider_descriptor_close(void *data)
{
	Gst_Egueb_Video_Provider *thiz = data;
	gst_element_set_state(thiz->playbin2, GST_STATE_READY);
}

static void _gst_egueb_video_provider_descriptor_play(void *data)
{
	Gst_Egueb_Video_Provider *thiz = data;
	gst_element_set_state(thiz->playbin2, GST_STATE_PLAYING);
}

static void _gst_egueb_video_provider_descriptor_pause(void *data)
{
	Gst_Egueb_Video_Provider *thiz = data;
	gst_element_set_state(thiz->playbin2, GST_STATE_PAUSED);
}

static Egueb_Dom_Video_Provider_Descriptor _gst_egueb_video_provider = {
	/* .create 	= */ _gst_egueb_video_provider_descriptor_create,
	/* .destroy 	= */ _gst_egueb_video_provider_descriptor_destroy,
	/* .open 	= */ _gst_egueb_video_provider_descriptor_open,
	/* .close 	= */ _gst_egueb_video_provider_descriptor_close,
	/* .play 	= */ _gst_egueb_video_provider_descriptor_play,
	/* .pause 	= */ _gst_egueb_video_provider_descriptor_pause
};
/*============================================================================*
 *                                 Global                                     *
 *============================================================================*/
/*============================================================================*
 *                                   API                                      *
 *============================================================================*/
EAPI Egueb_Dom_Video_Provider * gst_egueb_video_provider_new(
		const Egueb_Dom_Video_Provider_Notifier *notifier,
		Enesim_Renderer *image, Egueb_Dom_Node *n)
{
	Gst_Egueb_Video_Provider *thiz;
	Egueb_Dom_Video_Provider *ret;

	ret = egueb_dom_video_provider_new(&_gst_egueb_video_provider,
			notifier, enesim_renderer_ref(image), n);
	if (!ret)
	{
		enesim_renderer_unref(image);
		return NULL;
	}

	thiz = egueb_dom_video_provider_data_get(ret);
	thiz->image = image;
	thiz->vp = ret;

	return ret;
}
