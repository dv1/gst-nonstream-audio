#ifndef GSTDUMBDEC_H
#define GSTDUMBDEC_H

#include <gst/gst.h>
#include <dumb.h>
#include "gstnonstreamaudiodecoder.h"


G_BEGIN_DECLS


typedef struct _GstDumbDec GstDumbDec;
typedef struct _GstDumbDecClass GstDumbDecClass;


#define GST_TYPE_DUMB_DEC             (gst_dumb_dec_get_type())
#define GST_DUMB_DEC(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_DUMB_DEC, GstDumbDec))
#define GST_DUMB_DEC_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_DUMB_DEC, GstDumbDecClass))
#define GST_IS_DUMB_DEC(obj)          (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_DUMB_DEC))
#define GST_IS_DUMB_DEC_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_DUMB_DEC))


typedef struct
{
	long start_order, length;
}
gst_dumb_dec_subsong_info;


struct _GstDumbDec
{
	GstNonstreamAudioDecoder parent;

	gint sample_rate, num_channels;

	gint cur_loop_count, num_loops;
	gboolean loop_end_reached;
	gboolean do_actual_looping;

	gint resampling_quality, ramp_style;

	DUH *duh;
	DUH_SIGRENDERER *duh_sigrenderer;

	GArray *subsongs;
	guint cur_subsong, num_subsongs;
	gst_dumb_dec_subsong_info *cur_subsong_info;
	gboolean subsongs_explicit;
	long cur_subsong_start_pos;

};


struct _GstDumbDecClass
{
	GstNonstreamAudioDecoderClass parent_class;
};


GType gst_dumb_dec_get_type(void);


G_END_DECLS


#endif

