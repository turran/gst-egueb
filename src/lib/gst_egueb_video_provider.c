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


typedef struct _Gst_Egueb_Video_Provider
{
	Egueb_Dom_Video_Provider *vp;
	GstElement *playbin2;
	Enesim_Renderer *image;

	Enesim_Buffer *b;
	Enesim_Surface *s;
} Gst_Egueb_Video_Provider;

#if 0
/* FIXME we still need to register the log domain */
static int _egueb_svg_video_provider_gstreamer_log = -1;
#endif

static void _gst_egueb_video_provider_fakesink_handoff_cb(GstElement* object,
		GstBuffer *buf, GstPad *pad, gpointer data)
{
	Gst_Egueb_Video_Provider *thiz = data;

	/* lock the renderer */
	/* set the new surface */
	/* unlock the renderer */
	printf("new buffer arrived");
	enesim_renderer_lock(thiz->image);
	//enesim_renderer_image_src_set(thiz->image, thiz->s);
	enesim_renderer_unlock(thiz->image);
}

/*----------------------------------------------------------------------------*
 *                       The Video provider interface                         *
 *----------------------------------------------------------------------------*/
static void * _gst_egueb_video_provider_descriptor_create(void)
{
	Gst_Egueb_Video_Provider *thiz;
	GstElement *sink;

	thiz = calloc(1, sizeof(Gst_Egueb_Video_Provider));
	sink = gst_element_factory_make("fakesink", NULL);
	g_object_set(sink, "sync", TRUE, NULL);
	g_signal_connect(G_OBJECT(sink), "handoff",
			G_CALLBACK(_gst_egueb_video_provider_fakesink_handoff_cb),
			thiz); 

	thiz->playbin2 = gst_element_factory_make("playbin2", NULL);
	/* define a new sink based on fakesink to catch up every buffer */
	/* force a rgb32 bpp */
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
	if (thiz->s)
	{
		enesim_surface_unref(thiz->s);
		thiz->s = NULL;
	}
	if (thiz->b)
	{
		enesim_buffer_unref(thiz->b);
		thiz->b = NULL;
	}
	free(thiz);
}

static void _gst_egueb_video_provider_descriptor_open(void *data, Egueb_Dom_String *uri)
{
	Gst_Egueb_Video_Provider *thiz = data;

	printf("loading URI %s\n", egueb_dom_string_string_get(uri));
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
