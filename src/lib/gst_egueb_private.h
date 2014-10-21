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

#ifndef _GST_EGUEB_PRIVATE_H_
#define _GST_EGUEB_PRIVATE_H_

#include <Egueb_Dom.h>
#include <gst/gst.h>

Gst_Egueb_Document * gst_egueb_document_new(Egueb_Dom_Node *doc);
void gst_egueb_document_free(Gst_Egueb_Document *thiz);
void gst_egueb_document_feature_io_setup(Gst_Egueb_Document *thiz);

#endif
