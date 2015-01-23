#ifndef GST_EGUEB_DEMUX_H
#define GST_EGUEB_DEMUX_H

#include <gst/gst.h>
#include <gst/base/gstadapter.h>
#include <gst/base/gstbasesrc.h>
#include <gst/interfaces/navigation.h>

#include <Egueb_Dom.h>
#include <Egueb_Smil.h>

#include "gst_egueb_document.h"

G_BEGIN_DECLS

#define GST_TYPE_EGUEB_DEMUX            (gst_egueb_demux_get_type())
#define GST_EGUEB_DEMUX(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj),\
                                         GST_TYPE_EGUEB_DEMUX, GstEguebDemux))
#define GST_EGUEB_DEMUX_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass),\
                                         GST_TYPE_EGUEB_DEMUX, GstEguebDemuxClass))
#define GST_EGUEB_DEMUX_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),\
                                         GST_TYPE_EGUEB_DEMUX, GstEguebDemuxClass))
#define GST_IS_EGUEB_DEMUX(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj),\
                                         GST_TYPE_EGUEB_DEMUX))
#define GST_IS_EGUEB_DEMUX_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass),\
                                         GST_TYPE_EGUEB_DEMUX))
typedef struct _GstEguebDemux GstEguebDemux;
typedef struct _GstEguebDemuxClass GstEguebDemuxClass;

struct _GstEguebDemux
{
  GstElement parent;
  /* properties */
  guint container_w;
  guint container_h;
  gchar *location;

  /* private */
  GstAdapter *adapter;

  Egueb_Dom_Node *doc;
  Egueb_Dom_Node *topmost;
  Egueb_Dom_Feature *render;
  Egueb_Dom_Feature *window;
  Egueb_Dom_Feature *animation;
  Egueb_Dom_Feature *io;

  Egueb_Dom_Input *input;

  Gst_Egueb_Document *gdoc;

  GMutex *doc_lock;
  Enesim_Surface *s;
  Enesim_Renderer *background;
  Eina_List *damages;
  gboolean done;

  guint w;
  guint h;
  gint spf_n;
  gint spf_d;

  gint64 last_stop;
  guint64 seek;
  guint64 last_ts;
  gint64 duration;
  gint64 fps;
};

struct _GstEguebDemuxClass
{
  GstElementClass parent_class;
};


GType gst_egueb_demux_get_type (void);

G_END_DECLS

#endif

