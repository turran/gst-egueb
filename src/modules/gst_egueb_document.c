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

#include <gst/gst.h>
#include <Egueb_Dom.h>

#include "gst_egueb_document.h"

GST_DEBUG_CATEGORY_EXTERN (gst_egueb_document_debug);
#define GST_CAT_DEFAULT gst_egueb_document_debug

/* we will read in 4k blocks */
#define BUFFER_SIZE 4096
/*============================================================================*
 *                                  Local                                     *
 *============================================================================*/
struct _Gst_Egueb_Document
{
	Egueb_Dom_Node *doc;
	Egueb_Dom_Node *topmost;
	Egueb_Dom_Feature *io;
};

typedef struct _Gst_Egueb_Document_Pipeline
{
	GstElement *pipeline;
	gboolean done;
	/* data needed for the data event */
	Eina_Binbuf *binbuf;
	/* data needed for the image event */
	Enesim_Stream *s;
	void *stream_mmap;
	/* the enesim stream mmap */
	Enesim_Surface *surface;
	gboolean buffer_pushed;
} Gst_Egueb_Document_Pipeline;

static gboolean _gst_egueb_document_pipeline_process(Gst_Egueb_Document_Pipeline *p)
{
	GstBus *bus;

	bus = gst_pipeline_get_bus(GST_PIPELINE(p->pipeline));
	while (!p->done)
	{
		GstMessage *msg;
		msg = gst_bus_timed_pop (bus, GST_CLOCK_TIME_NONE);
		switch (GST_MESSAGE_TYPE(msg))
		{
			case GST_MESSAGE_ERROR:{
			GError *err = NULL;
			gchar *dbg_info = NULL;
			gst_message_parse_error(msg, &err, &dbg_info);
			GST_ERROR("Error received on the pipeline '%s', %s'", err->message, dbg_info ? dbg_info : "none");
			p->done = TRUE;
			}
			break;

			case GST_MESSAGE_EOS:
			GST_INFO("Pipeline reached EOS");
			p->done = TRUE;
			break;

			default:
			break;
		}
		gst_message_unref(msg);
	}
	gst_object_unref(bus);
}

static void
_gst_egueb_document_data_fakesink_handoff_cb (GstElement * object,
		 GstBuffer * buf, GstPad * pad, gpointer data)
{
	Gst_Egueb_Document_Pipeline *p = data;

	eina_binbuf_append_length(p->binbuf, GST_BUFFER_DATA(buf),
			GST_BUFFER_SIZE(buf)); 
}

static void _gst_egueb_document_data_uridecodebin_pad_added_cb (
		GstElement *src, GstPad *pad, gpointer data)
{
	Gst_Egueb_Document_Pipeline *p = data;
	GstElement *sink;
	GstPad *sinkpad;

	/* connect a fakesink */
	sink = gst_element_factory_make("fakesink", NULL);
	g_object_set(sink, "signal-handoffs", TRUE, NULL);
	/* get the handoff buffer and put it on an enesim stream */
	g_signal_connect (G_OBJECT (sink), "handoff",
			G_CALLBACK (_gst_egueb_document_data_fakesink_handoff_cb),
			p);

	gst_bin_add(GST_BIN(p->pipeline), gst_object_ref(sink));
	sinkpad = gst_element_get_static_pad(sink, "sink");
	gst_pad_link(pad, sinkpad);
	gst_element_sync_state_with_parent(sink);
	gst_object_unref(sinkpad);
	gst_object_unref(sink);
}

static void _gst_egueb_document_buffer_free(void *data, void *user_data)
{
	GstBuffer *buffer = user_data;
	gst_buffer_unref(buffer);
}

static void
_gst_egueb_document_image_fakesink_handoff_cb (GstElement * object,
		 GstBuffer * buf, GstPad * pad, gpointer data)
{
	Gst_Egueb_Document_Pipeline *p = data;
	GstCaps *caps;
	const GstStructure *s;
	Enesim_Buffer *ebuf;
	Enesim_Buffer_Sw_Data edata;
	Enesim_Renderer *r;
	gint width, height;

	caps = gst_buffer_get_caps(buf);
	s = gst_caps_get_structure(caps, 0);
	/* get the width and height */
	gst_structure_get_int(s, "width", &width);
	gst_structure_get_int(s, "height", &height);
	gst_caps_unref(caps);

	/* create the surface based on the buffer data */
	/* the data is not premultiplied, we need to do it */
	/* TODO we could add the xrgb caps so for jpegs there's no need to premultiply */
	edata.argb8888.plane0 = (uint32_t *)GST_BUFFER_DATA(buf);
	edata.argb8888.plane0_stride = GST_ROUND_UP_4(width * 4);

	ebuf = enesim_buffer_new_data_from(ENESIM_BUFFER_FORMAT_ARGB8888,
			width, height, EINA_FALSE, &edata, _gst_egueb_document_buffer_free,
			gst_buffer_ref(buf));
	p->surface = enesim_surface_new(ENESIM_FORMAT_ARGB8888, width, height);

	r = enesim_renderer_importer_new();
	enesim_renderer_importer_buffer_set(r, ebuf);
	enesim_renderer_draw(r, p->surface, ENESIM_ROP_FILL, NULL, 0, 0, NULL);
	enesim_renderer_unref(r);
}

static void _gst_egueb_document_image_decodebin2_pad_added_cb (
		GstElement *src, GstPad *pad, gpointer data)
{
	Gst_Egueb_Document_Pipeline *p = data;
	GstPadLinkReturn linked;
	GstElement *ffmpegcolorspace;
	GstElement *capsfilter;
	GstElement *sink;
	GstPad *sinkpad;
	GstPad *srcpad;
	GstCaps *caps;

	/* connect a ffmpegcolorspace to output on the enesim format */
	ffmpegcolorspace = gst_element_factory_make("ffmpegcolorspace", NULL);

	/* only allow rgb final data */
	capsfilter = gst_element_factory_make("capsfilter", NULL);
	caps = gst_caps_new_simple("video/x-raw-rgb", 
			"bpp", G_TYPE_INT, 32,
			"depth", G_TYPE_INT, 32,
			"endianness", G_TYPE_INT, G_BIG_ENDIAN,
			"alpha_mask", G_TYPE_INT, 0x000000ff,
			"red_mask", G_TYPE_INT, 0x0000ff00,
			"green_mask", G_TYPE_INT, 0x00ff0000,
			"blue_mask", G_TYPE_INT, 0xff000000,
			NULL);
	g_object_set(G_OBJECT(capsfilter), "caps", caps, NULL);

	/* connect a fakesink */
	sink = gst_element_factory_make("fakesink", NULL);
	g_object_set(sink, "signal-handoffs", TRUE, NULL);
	/* get the handoff buffer and put it on an enesim stream */
	g_signal_connect(G_OBJECT (sink), "handoff",
			G_CALLBACK (_gst_egueb_document_image_fakesink_handoff_cb),
			p);

	/* start linking the elements */
	gst_bin_add_many(GST_BIN(p->pipeline),
			gst_object_ref(ffmpegcolorspace),
			gst_object_ref(capsfilter),
			gst_object_ref(sink), NULL);

	sinkpad = gst_element_get_static_pad(ffmpegcolorspace, "sink");
	linked = gst_pad_link(pad, sinkpad);
	gst_object_unref(sinkpad);

	if (linked != GST_PAD_LINK_OK) {
		gst_bin_remove(GST_BIN(p->pipeline), ffmpegcolorspace);
		gst_bin_remove(GST_BIN(p->pipeline), capsfilter);
		gst_bin_remove(GST_BIN(p->pipeline), sink);

		gst_element_set_state(ffmpegcolorspace, GST_STATE_NULL);
		gst_element_set_state(capsfilter, GST_STATE_NULL);
		gst_element_set_state(sink, GST_STATE_NULL);
		p->done = TRUE;
		goto done;
	}

	srcpad = gst_element_get_static_pad(ffmpegcolorspace, "src");
	sinkpad = gst_element_get_static_pad(capsfilter, "sink");
	gst_pad_link(srcpad, sinkpad);
	gst_object_unref(sinkpad);
	gst_object_unref(srcpad);

	srcpad = gst_element_get_static_pad(capsfilter, "src");
	sinkpad = gst_element_get_static_pad(sink, "sink");
	gst_pad_link(srcpad, sinkpad);
	gst_object_unref(sinkpad);
	gst_object_unref(srcpad);

	gst_element_sync_state_with_parent(ffmpegcolorspace);
	gst_element_sync_state_with_parent(capsfilter);
	gst_element_sync_state_with_parent(sink);
done:
	gst_object_unref(ffmpegcolorspace);
	gst_object_unref(capsfilter);
	gst_object_unref(sink);
}

static void _gst_egueb_document_image_appsrc_need_data_cb (
		GstElement *src, guint size, gpointer data)
{
	Gst_Egueb_Document_Pipeline *p = data;
	GstFlowReturn ret;
	GstBuffer *buf;
	size_t stream_length;

	if (p->buffer_pushed)
	{
		GST_DEBUG("Image data already pushed, pushing the EOS");
		if (p->stream_mmap)
			enesim_stream_munmap(p->s, p->stream_mmap);
		g_signal_emit_by_name (src, "end-of-stream", &ret);
		return;
	}

	GST_DEBUG("Pushing the image to decode");
	p->stream_mmap = enesim_stream_mmap(p->s, &stream_length);
	if (p->stream_mmap)
	{
		/* push the stream */
		buf = gst_buffer_new();
		GST_BUFFER_DATA(buf) = (gchar *)p->stream_mmap;
		GST_BUFFER_SIZE(buf) = stream_length;
		p->buffer_pushed = TRUE;
	}
	else
	{
		ssize_t written;

		buf = gst_buffer_new_and_alloc(BUFFER_SIZE);
		written = enesim_stream_read(p->s, GST_BUFFER_DATA(buf), BUFFER_SIZE);
		if (written <= 0)
		{
			gst_buffer_unref(buf);
			p->buffer_pushed = TRUE;
			g_signal_emit_by_name (src, "end-of-stream", &ret);
			return;
		}
		GST_BUFFER_SIZE(buf) = written;
	}

	/* push the buffer and the end of stream at once */
	g_signal_emit_by_name (src, "push-buffer", buf, &ret);
}
/*----------------------------------------------------------------------------*
 *                               IO interface                                 *
 *----------------------------------------------------------------------------*/
static void _gst_egueb_document_feature_io_data_cb(Egueb_Dom_Event *ev, void *data)
{
	Gst_Egueb_Document *thiz = data;
	Gst_Egueb_Document_Pipeline pipe;
	GstElement *pipeline;
	GstElement *uridecodebin;
	Egueb_Dom_Uri uri;
	Egueb_Dom_String *final_uri = NULL;
	char *binbuf_data;
	size_t binbuf_len;

	egueb_dom_event_io_uri_get(ev, &uri);
	if (uri.type == EGUEB_DOM_URI_TYPE_RELATIVE)
	{
		Egueb_Dom_String *location;
		Egueb_Dom_Uri final;
		Eina_Bool resolved;

		location = egueb_dom_document_uri_get(thiz->doc);
		resolved = egueb_dom_uri_resolve(&uri, location, &final);
		egueb_dom_string_unref(location);
		if (!resolved)
		{
			GST_WARNING("Impossible to resolve the uri");
			egueb_dom_uri_cleanup(&uri);
			return;
		}
		final_uri = egueb_dom_string_ref(final.location);
		egueb_dom_uri_cleanup(&final);
	}
	else
	{
		final_uri = egueb_dom_string_ref(uri.location);
	}
	egueb_dom_uri_cleanup(&uri);

	/* create the pipeline */
	pipeline = gst_pipeline_new(NULL);

	/* setup our own pipe */
	pipe.pipeline = pipeline;
	pipe.binbuf = eina_binbuf_new();
	pipe.done = FALSE;

	/* create an uridecodebin and put the uri */
	/* mark the caps as anything so we can get the data raw as it is stored */
	uridecodebin = gst_element_factory_make("uridecodebin", NULL);
	g_object_set (uridecodebin,
			"uri", egueb_dom_string_string_get(final_uri),
			"caps", gst_caps_new_any(), NULL);
	g_signal_connect (G_OBJECT (uridecodebin), "pad-added",
			G_CALLBACK (_gst_egueb_document_data_uridecodebin_pad_added_cb),
			&pipe);
	egueb_dom_string_unref(final_uri);
	gst_bin_add(GST_BIN(pipeline), uridecodebin);

	/* launch it */
	gst_element_set_state(pipeline, GST_STATE_PLAYING);
	_gst_egueb_document_pipeline_process(&pipe);
	gst_element_set_state(pipeline, GST_STATE_NULL);
	gst_object_unref(pipeline);

	/* finish */
	binbuf_len = eina_binbuf_length_get(pipe.binbuf);
	binbuf_data = eina_binbuf_string_steal(pipe.binbuf);
	if (binbuf_data)
	{
		Enesim_Stream *s;
		s = enesim_stream_buffer_new(binbuf_data, binbuf_len);
		egueb_dom_event_io_data_finish(ev, s);
	}
	eina_binbuf_free(pipe.binbuf);
}

/* create an image loading pipeline */
static void _gst_egueb_document_feature_io_image_cb(Egueb_Dom_Event *ev, void *data)
{
	Gst_Egueb_Document *thiz = data;
	Gst_Egueb_Document_Pipeline pipe;
	GstElement *pipeline;
	GstElement *appsrc;
	GstElement *decodebin2;
	GstPad *srcpad;
	GstPad *sinkpad;
	Enesim_Stream *s;

	s = egueb_dom_event_io_stream_get(ev);
	if (!s) return;

	/* create the pipeline */
	pipeline = gst_pipeline_new(NULL);

	/* setup our own pipe */
	pipe.pipeline = pipeline;
	pipe.surface = NULL;
	pipe.s = s;
	pipe.buffer_pushed = FALSE;
	pipe.done = FALSE;

	/* create the appsrc element to push the buffers to */
	appsrc = gst_element_factory_make("appsrc", NULL);
	g_signal_connect (appsrc, "need-data",
			G_CALLBACK (_gst_egueb_document_image_appsrc_need_data_cb),
			&pipe);
	/* setup the decoder */
	decodebin2 = gst_element_factory_make("decodebin2", NULL);
	g_signal_connect(G_OBJECT (decodebin2), "pad-added",
			G_CALLBACK (_gst_egueb_document_image_decodebin2_pad_added_cb),
			&pipe);

	/* TODO */
	/* check the decodebin2 signals that the element is for decoding */
	/* check the decodebin2 caps of the pad added */
	/* those must start with an image/foo */

	/* link the appsrc and the decodebin2 */
	gst_bin_add_many(GST_BIN(pipeline), gst_object_ref(appsrc), gst_object_ref(decodebin2), NULL);
	srcpad = gst_element_get_static_pad(appsrc, "src");
	sinkpad = gst_element_get_static_pad(decodebin2, "sink");
	gst_pad_link(srcpad, sinkpad);
	gst_object_unref(srcpad);
	gst_object_unref(sinkpad);
	gst_object_unref(appsrc);
	gst_object_unref(decodebin2);

	/* launch it */
	gst_element_set_state(pipeline, GST_STATE_PLAYING);
	/* push the buffers from the stream to the branch, i.e appsrc */
	_gst_egueb_document_pipeline_process(&pipe);
	gst_element_set_state(pipeline, GST_STATE_NULL);
	gst_object_unref(pipeline);

	/* finish */
	egueb_dom_event_io_image_finish(ev, pipe.surface);
	enesim_stream_unref(s);
}

static void _gst_egueb_document_feature_io_cleanup(Gst_Egueb_Document *thiz)
{
	egueb_dom_node_event_listener_remove(thiz->topmost,
			EGUEB_DOM_EVENT_IO_DATA,
			_gst_egueb_document_feature_io_data_cb, EINA_TRUE, thiz);
	egueb_dom_node_event_listener_remove(thiz->topmost,
			EGUEB_DOM_EVENT_IO_IMAGE,
			_gst_egueb_document_feature_io_image_cb, EINA_TRUE, thiz);
}
/*============================================================================*
 *                                 Global                                     *
 *============================================================================*/
Gst_Egueb_Document * gst_egueb_document_new(Egueb_Dom_Node *doc)
{
	Gst_Egueb_Document *thiz;

	if (!doc) return NULL;

	thiz = calloc(1, sizeof(Gst_Egueb_Document));
	thiz->doc = doc;
	thiz->topmost = egueb_dom_document_document_element_get(doc);
	return thiz;
}

void gst_egueb_document_free(Gst_Egueb_Document *thiz)
{
	if (!thiz) return;

	if (thiz->io)
	{
		_gst_egueb_document_feature_io_cleanup(thiz);
		egueb_dom_feature_unref(thiz->io);
		thiz->io = NULL;
	}

	if (thiz->topmost)
	{
		egueb_dom_node_unref(thiz->topmost);
		thiz->topmost = NULL;
	}

	if (thiz->doc)
	{
		egueb_dom_node_unref(thiz->doc);
		thiz->doc = NULL;
	}

	free(thiz);
}

void gst_egueb_document_feature_io_setup(Gst_Egueb_Document *thiz)
{
	Egueb_Dom_Feature *feature;

	if (thiz->io) return;

	feature = egueb_dom_node_feature_get(thiz->topmost, EGUEB_DOM_FEATURE_IO_NAME, NULL);
	if (!feature) return;

	egueb_dom_node_event_listener_add(thiz->topmost, EGUEB_DOM_EVENT_IO_DATA,
			_gst_egueb_document_feature_io_data_cb,
			EINA_TRUE, thiz);
	egueb_dom_node_event_listener_add(thiz->topmost, EGUEB_DOM_EVENT_IO_IMAGE,
			_gst_egueb_document_feature_io_image_cb,
			EINA_TRUE, thiz);
	thiz->io = feature;
}
/*============================================================================*
 *                                   API                                      *
 *============================================================================*/
