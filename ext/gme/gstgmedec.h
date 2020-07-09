#ifndef GSTGMEDEC_H
#define GSTGMEDEC_H


#include <gst/gst.h>
#include <gme/gme.h>
#include "gst/audio/gstnonstreamaudiodecoder.h"


G_BEGIN_DECLS


typedef struct _GstGmeDec GstGmeDec;
typedef struct _GstGmeDecClass GstGmeDecClass;


#define GST_TYPE_GME_DEC             (gst_gme_dec_get_type())
#define GST_GME_DEC(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_GME_DEC, GstGmeDec))
#define GST_GME_DEC_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_GME_DEC, GstGmeDecClass))
#define GST_IS_GME_DEC(obj)          (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_GME_DEC))
#define GST_IS_GME_DEC_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_GME_DEC))


struct _GstGmeDec
{
	GstNonstreamAudioDecoder parent;

	gme_t *emu;
	guint num_tracks, cur_track;
	guint num_loops;

	gdouble echo, stereo_separation;
	gboolean enable_effects, enable_surround;
};


struct _GstGmeDecClass
{
	GstNonstreamAudioDecoderClass parent_class;
};


GType gst_gme_dec_get_type(void);


G_END_DECLS


#endif
