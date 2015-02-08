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

#ifndef GST_EGUEB_VIDEO_BIN_H
#define GST_EGUEB_VIDEO_BIN_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>

#include <Egueb_Dom.h>

G_BEGIN_DECLS

#define GST_TYPE_EGUEB_VIDEO_BIN            (gst_egueb_video_bin_get_type())
#define GST_EGUEB_VIDEO_BIN(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj),\
                                         GST_TYPE_EGUEB_VIDEO_BIN, GstEguebVideoBin))
#define GST_EGUEB_VIDEO_BIN_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass),\
                                         GST_TYPE_EGUEB_VIDEO_BIN, GstEguebVideoBinClass))
#define GST_EGUEB_VIDEO_BIN_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),\
                                         GST_TYPE_EGUEB_VIDEO_BIN, GstEguebVideoBinClass))
#define GST_IS_EGUEB_VIDEO_BIN(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj),\
                                         GST_TYPE_EGUEB_VIDEO_BIN))
#define GST_IS_EGUEB_VIDEO_BIN_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass),\
                                         GST_TYPE_EGUEB_VIDEO_BIN))
typedef struct _GstEguebVideoBin
{
  GstBin parent;
  Enesim_Renderer *image;
  GstElement *uridecodebin;
  GstElement *convert;
  GstElement *appsink;
} GstEguebVideoBin;

typedef struct _GstEguebVideoBinClass
{
  GstBinClass parent;
} GstEguebVideoBinClass;


GType gst_egueb_video_bin_get_type (void);

G_END_DECLS

#endif
