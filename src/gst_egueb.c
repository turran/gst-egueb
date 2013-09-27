#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include "gst_egueb_xml_sink.h"
#include "gst_egueb_svg_src.h"
#include "gst_egueb_svg.h"
#include "gst_egueb_type.h"

GST_DEBUG_CATEGORY (egueb_xml_sink_debug);
GST_DEBUG_CATEGORY (egueb_svg_src_debug);
GST_DEBUG_CATEGORY (egueb_svg_debug);

static gboolean
plugin_init (GstPlugin * plugin)
{
  /* first register the debug categories */
  GST_DEBUG_CATEGORY_INIT (egueb_xml_sink_debug, "egueb_xml_sink", 0, "Egueb XML sink");
  GST_DEBUG_CATEGORY_INIT (egueb_svg_src_debug, "egueb_svg_src", 0, "Egueb SVG source");
  GST_DEBUG_CATEGORY_INIT (egueb_svg_debug, "egueb_svg", 0, "Egueb SVG bin");

  /* now register the elements */
  if (!gst_element_register (plugin, "eguebxmlsink",
          GST_RANK_MARGINAL, GST_TYPE_EGUEB_XML_SINK))
    return FALSE;
  if (!gst_element_register (plugin, "eguebsvgsrc",
          GST_RANK_MARGINAL, GST_TYPE_EGUEB_SVG_SRC))
    return FALSE;
  if (!gst_element_register (plugin, "eguebsvg",
          GST_RANK_PRIMARY + 1, GST_TYPE_EGUEB_SVG))
    return FALSE;

  return TRUE;
}

/* this is the structure that gstreamer looks for to register plugins
 */
GST_PLUGIN_DEFINE (GST_VERSION_MAJOR, GST_VERSION_MINOR,
    "egueb", "Egueb GStreamer Plugin",
    plugin_init, VERSION, GST_LICENSE_UNKNOWN, PACKAGE_NAME,
    "http://www.turran.org");

