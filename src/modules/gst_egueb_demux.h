#ifndef GST_EGUEB_DEMUX_H
#define GST_EGUEB_DEMUX_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gst/base/gstadapter.h>
#if HAVE_GST_1
#include <gst/video/navigation.h>
#else
#include <gst/interfaces/navigation.h>
#endif

#include <Egueb_Dom.h>
#include <Egueb_Smil.h>

#include "gst_egueb_document.h"


#if HAVE_GST_1
#define GST_FLOW_WRONG_STATE GST_FLOW_FLUSHING
#define GST_FLOW_UNEXPECTED GST_FLOW_EOS
#define GST_SEGMENT_SET_POSITION(segment,value) ((segment)->position=(value))
#define GST_SEGMENT_GET_POSITION(segment)       ((segment)->position)

#else
#define gst_segment_do_seek gst_segment_set_seek
#define GST_SEGMENT_SET_POSITION(segment,value) ((segment)->last_stop=value)
#define GST_SEGMENT_GET_POSITION(segment)       ((segment)->last_stop)
#endif

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
  GMutex *doc_lock;
  GstAdapter *adapter;
  GstSegment *segment;
  gboolean send_ns;
  GstSegment *pending_segment;
  Gst_Egueb_Document *gdoc;

  /* The egueb related stuff */
  Egueb_Dom_Node *doc;
  Egueb_Dom_Node *topmost;

  /* Render feature */
  Egueb_Dom_Feature *window;
  Egueb_Dom_Feature *render;
  Enesim_Surface *s;
  Enesim_Renderer *background;
  Eina_List *damages;
  guint w;
  guint h;

  /* Animation feature */
  Egueb_Dom_Feature *animation;
  gint spf_n;
  gint spf_d;

  /* IO feature */
  Egueb_Dom_Feature *io;

  /* UI feature */
  Egueb_Dom_Input *input;

  gboolean done;

  /* TODO remove this */
  gint64 last_stop;
  /* TODO remove this */
  guint64 seek;
  guint64 next_ts;
  /* TODO rename this */
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

