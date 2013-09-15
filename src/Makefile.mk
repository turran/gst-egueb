plugindir = $(libdir)/gstreamer-@GST_MAJORMINOR@
plugin_LTLIBRARIES = libgstegueb.la

libgstegueb_la_SOURCES = \
src/gst_egueb_xml_sink.c \
src/gst_egueb_svg_src.c \
src/gst_egueb_svg.c \
src/gst_egueb.c

libgstegueb_la_CFLAGS = $(GSTREAMER_EGUEB_CFLAGS)
libgstegueb_la_LIBADD = $(GSTREAMER_EGUEB_LIBS)
libgstegueb_la_LDFLAGS = -no-undefined -module -avoid-version
