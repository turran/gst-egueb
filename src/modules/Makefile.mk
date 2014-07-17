plugindir = $(libdir)/gstreamer-@GST_MAJORMINOR@
plugin_LTLIBRARIES = src/modules/libgstegueb.la

src_modules_libgstegueb_la_SOURCES = \
src/modules/gst_egueb_xml_sink.c \
src/modules/gst_egueb_src.c \
src/modules/gst_egueb_demux.c \
src/modules/gst_egueb.c

src_modules_libgstegueb_la_CFLAGS = $(GSTREAMER_EGUEB_CFLAGS)
src_modules_libgstegueb_la_LIBADD = $(GSTREAMER_EGUEB_LIBS)
src_modules_libgstegueb_la_LDFLAGS = -no-undefined -module -avoid-version
