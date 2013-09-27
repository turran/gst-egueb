plugindir = $(libdir)/gstreamer-@GST_MAJORMINOR@
plugin_LTLIBRARIES = src/libgstegueb.la

src_libgstegueb_la_SOURCES = \
src/gst_egueb_xml_sink.c \
src/gst_egueb_svg_src.c \
src/gst_egueb_svg.c \
src/gst_egueb.c

src_libgstegueb_la_CFLAGS = $(GSTREAMER_EGUEB_CFLAGS)
src_libgstegueb_la_LIBADD = $(GSTREAMER_EGUEB_LIBS)
src_libgstegueb_la_LDFLAGS = -no-undefined -module -avoid-version
