bin_PROGRAMS += src/bin/gst-egueb-record

src_bin_gst_egueb_record_CPPFLAGS = @GST_EGUEB_BIN_CFLAGS@
src_bin_gst_egueb_record_SOURCES =  src/bin/gst-egueb-record.c
src_bin_gst_egueb_record_LDADD = @GST_EGUEB_BIN_LIBS@
