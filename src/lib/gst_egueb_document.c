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
/*----------------------------------------------------------------------------*
 *                               IO interface                                 *
 *----------------------------------------------------------------------------*/
static void _gst_egueb_document_feature_io_data_cb(Egueb_Dom_Event *ev, void *data EINA_UNUSED)
{
	/* TODO */
	/* create the pipeline */
	/* create an uridecodebin2 and put the uri */
	/* mark the caps as anything so we can get the data raw as it is stored */
	/* connect a fakesink or an appsink */
	/* get the handoff buffer and put it on an enesim stream */
}

/* create an image loading pipeline */
static void _gst_egueb_document_feature_io_image_cb(Egueb_Dom_Event *ev, void *data EINA_UNUSED)
{
	Enesim_Stream *s;

	s = egueb_dom_event_io_stream_get(ev);
	if (!s) return;

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

void gst_egueb_document_feature_io_cleanup(Gst_Egueb_Document *thiz)
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
