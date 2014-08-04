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

#ifndef _GST_EGUEB_H_
#define _GST_EGUEB_H_

#ifdef EAPI
# undef EAPI
#endif

#ifdef _WIN32
# ifdef GST_EGUEB_BUILD
#  ifdef DLL_EXPORT
#   define EAPI __declspec(dllexport)
#  else
#   define EAPI
#  endif
# else
#  define EAPI __declspec(dllimport)
# endif
#else
# ifdef __GNUC__
#  if __GNUC__ >= 4
#   define EAPI __attribute__ ((visibility("default")))
#  else
#   define EAPI
#  endif
# else
#  define EAPI
# endif
#endif

#include <Egueb_Dom.h>

EAPI void gst_egueb_init(void);
EAPI void gst_egueb_shutdown(void);

EAPI Egueb_Dom_Video_Provider * gst_egueb_video_provider_new(
		const Egueb_Dom_Video_Provider_Notifier *notifier,
		Enesim_Renderer *image, Egueb_Dom_Node *n);

typedef struct _Gst_Egueb_Document Gst_Egueb_Document;
EAPI Gst_Egueb_Document * gst_egueb_document_new(Egueb_Dom_Node *doc);
EAPI void gst_egueb_document_free(Gst_Egueb_Document *thiz);
EAPI void gst_egueb_document_feature_io_setup(Gst_Egueb_Document *thiz);

#endif
