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

#include "gst_egueb_private.h"
#include "Gst_Egueb.h"
/*============================================================================*
 *                                  Local                                     *
 *============================================================================*/
struct _Gst_Egueb_Document
{
	Egueb_Dom_Node *doc;
	Egueb_Dom_Feature *io;
};

typedef struct _Gst_Egueb_Document_Pipeline
{
	GstElement *pipeline;
	Eina_Binbuf *binbuf;
	Enesim_Surface *surface;
} Gst_Egueb_Document_Pipeline;

static gboolean _gst_egueb_document_pipeline_process(GstElement *p)
{
	GstBus *bus;
	gboolean done = FALSE;

	bus = gst_pipeline_get_bus(GST_PIPELINE(p));
	do
	{
		GstMessage *msg;
		msg = gst_bus_timed_pop (bus, GST_CLOCK_TIME_NONE);
		switch (GST_MESSAGE_TYPE(msg))
		{
			case GST_MESSAGE_ERROR:
			ERR("Error received on the pipeline");
			done = TRUE;
			break;

			case GST_MESSAGE_EOS:
			INFO("Data completed");
			done = TRUE;
			break;

			default:
			break;
		}
		gst_message_unref(msg);
	} while (!done);
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
	sinkpad = gst_element_get_static_pad(sink, "sink");
	gst_bin_add(GST_BIN(p->pipeline), gst_object_ref(sink));
	gst_pad_link(pad, sinkpad);
	gst_element_sync_state_with_parent(sink);
	gst_object_unref(sinkpad);
	gst_object_unref(sink);
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
	GstPad *srcpad;
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
			WARN("Impossible to resolve the uri");
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
	pipe.surface = NULL;
	pipe.binbuf = eina_binbuf_new();

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
	_gst_egueb_document_pipeline_process(pipeline);
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
	Enesim_Stream *s;

	s = egueb_dom_event_io_stream_get(ev);
	if (!s) return;

	ERR("Image not implemented yet");
	/* TODO */
	/* create the pipeline */
	
	/* push the buffers from the stream to the branch, i.e appsrc */
	/* check the decodebin2 signals that the element is for decoding */
	/* check the decodebin2 caps of the pad added */
	/* those must start with an image/foo */
	/* connect a fakesink or an appsink */
	/* get the handoff buffer and put it on an enesim surface */
	enesim_stream_unref(s);
}

static void _gst_egueb_document_feature_io_cleanup(Gst_Egueb_Document *thiz)
{
	egueb_dom_node_event_listener_remove(thiz->doc,
			EGUEB_DOM_EVENT_IO_DATA,
			_gst_egueb_document_feature_io_data_cb, EINA_TRUE, thiz);
	egueb_dom_node_event_listener_remove(thiz->doc,
			EGUEB_DOM_EVENT_IO_IMAGE,
			_gst_egueb_document_feature_io_image_cb, EINA_TRUE, thiz);
}
/*============================================================================*
 *                                 Global                                     *
 *============================================================================*/
/*============================================================================*
 *                                   API                                      *
 *============================================================================*/
EAPI Gst_Egueb_Document * gst_egueb_document_new(Egueb_Dom_Node *doc)
{
	Gst_Egueb_Document *thiz;

	if (!doc) return NULL;

	thiz = calloc(1, sizeof(Gst_Egueb_Document));
	thiz->doc = doc;
	return thiz;
}

EAPI void gst_egueb_document_free(Gst_Egueb_Document *thiz)
{
	if (!thiz) return;

	if (thiz->io)
	{
		_gst_egueb_document_feature_io_cleanup(thiz);
		egueb_dom_feature_unref(thiz->io);
		thiz->io = NULL;
	}

	if (thiz->doc)
	{
		egueb_dom_node_unref(thiz->doc);
		thiz->doc = NULL;
	}

	free(thiz);
}

EAPI void gst_egueb_document_feature_io_setup(Gst_Egueb_Document *thiz)
{
	Egueb_Dom_Feature *feature;

	if (thiz->io) return;

	feature = egueb_dom_node_feature_get(thiz->doc, EGUEB_DOM_FEATURE_IO_NAME, NULL);
	if (!feature) return;

	egueb_dom_node_event_listener_add(thiz->doc, EGUEB_DOM_EVENT_IO_DATA,
			_gst_egueb_document_feature_io_data_cb,
			EINA_TRUE, thiz);
	egueb_dom_node_event_listener_add(thiz->doc, EGUEB_DOM_EVENT_IO_IMAGE,
			_gst_egueb_document_feature_io_image_cb,
			EINA_TRUE, thiz);
	thiz->io = feature;
}
