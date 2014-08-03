lib_LTLIBRARIES = src/lib/libgstegueb.la

installed_headersdir = $(pkgincludedir)-$(VMAJ)
dist_installed_headers_DATA = \
src/lib/Gst_Egueb.h

src_lib_libgstegueb_la_SOURCES = \
src/lib/gst_egueb_video_provider.c \
src/lib/gst_egueb_main.c

src_lib_libgstegueb_la_CPPFLAGS = $(GST_EGUEB_LIB_CFLAGS)
src_lib_libgstegueb_la_LIBADD = $(GST_EGUEB_LIB_LIBS)
