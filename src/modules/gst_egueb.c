#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include "gst_egueb_demux.h"
#include "gst_egueb_type.h"

#if HAVE_GST_1
#define PLUGIN_NAME egueb
#else
#define PLUGIN_NAME "egueb"
#endif

GST_DEBUG_CATEGORY (gst_egueb_demux_debug);
GST_DEBUG_CATEGORY (gst_egueb_document_debug);

static gboolean
plugin_init (GstPlugin * plugin)
{
  /* first register the debug categories */
  GST_DEBUG_CATEGORY_INIT (gst_egueb_demux_debug, "eguebdemux", 0, "Egueb SVG demuxer");
  GST_DEBUG_CATEGORY_INIT (gst_egueb_document_debug, "eguebdoc", 0, "Egueb document");

  /* now register the elements */
  if (!gst_element_register (plugin, "eguebdemux",
          GST_RANK_PRIMARY + 1, GST_TYPE_EGUEB_DEMUX))
    return FALSE;

  return TRUE;
}

/* this is the structure that gstreamer looks for to register plugins
 */
GST_PLUGIN_DEFINE (GST_VERSION_MAJOR, GST_VERSION_MINOR,
    PLUGIN_NAME, "Egueb GStreamer Plugin",
    plugin_init, VERSION, GST_LICENSE_UNKNOWN, PACKAGE_NAME,
    "http://www.turran.org");

