#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include "gst_egueb_xml_sink.h"
#include "gst_egueb_src.h"
#include "gst_egueb_demux.h"
#include "gst_egueb_type.h"

GST_DEBUG_CATEGORY (gst_egueb_xml_sink_debug);
GST_DEBUG_CATEGORY (gst_egueb_src_debug);
GST_DEBUG_CATEGORY (gst_egueb_demux_debug);

static gboolean
plugin_init (GstPlugin * plugin)
{
  /* first register the debug categories */
  GST_DEBUG_CATEGORY_INIT (gst_egueb_xml_sink_debug, "eguebxmlsink", 0, "Egueb XML sink");
  GST_DEBUG_CATEGORY_INIT (gst_egueb_src_debug, "eguebsrc", 0, "Egueb SVG source");
  GST_DEBUG_CATEGORY_INIT (gst_egueb_demux_debug, "eguebdemux", 0, "Egueb SVG demuxer");

  /* now register the elements */
  if (!gst_element_register (plugin, "eguebxmlsink",
          GST_RANK_MARGINAL, GST_TYPE_EGUEB_XML_SINK))
    return FALSE;
  if (!gst_element_register (plugin, "eguebsrc",
          GST_RANK_MARGINAL, GST_TYPE_EGUEB_SRC))
    return FALSE;
  if (!gst_element_register (plugin, "eguebdemux",
          GST_RANK_PRIMARY + 1, GST_TYPE_EGUEB_DEMUX))
    return FALSE;

  return TRUE;
}

/* this is the structure that gstreamer looks for to register plugins
 */
GST_PLUGIN_DEFINE (GST_VERSION_MAJOR, GST_VERSION_MINOR,
    "egueb", "Egueb GStreamer Plugin",
    plugin_init, VERSION, GST_LICENSE_UNKNOWN, PACKAGE_NAME,
    "http://www.turran.org");

