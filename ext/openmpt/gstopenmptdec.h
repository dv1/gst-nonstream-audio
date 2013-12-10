#ifndef GSTOPENMPTDEC_H
#define GSTOPENMPTDEC_H


#include <gst/gst.h>
#include "gstnonstreamaudiodecoder.h"
#include <libopenmpt/libopenmpt.h>


G_BEGIN_DECLS


typedef struct _GstOpenMptDec GstOpenMptDec;
typedef struct _GstOpenMptDecClass GstOpenMptDecClass;


#define GST_TYPE_OPENMPT_DEC             (gst_openmpt_dec_get_type())
#define GST_OPENMPT_DEC(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_OPENMPT_DEC, GstOpenMptDec))
#define GST_OPENMPT_DEC_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_OPENMPT_DEC, GstOpenMptDecClass))
#define GST_IS_OPENMPT_DEC(obj)          (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_OPENMPT_DEC))
#define GST_IS_OPENMPT_DEC_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_OPENMPT_DEC))


struct _GstOpenMptDec
{
	GstNonstreamAudioDecoder parent;
	openmpt_module *mod;

	guint cur_subsong, num_subsongs;
	double *subsong_durations;

	gint num_loops;

	gint master_gain, stereo_separation, filter_length, volume_ramping;

	GstAudioFormat sample_format;
	gint sample_rate, num_channels;
};


struct _GstOpenMptDecClass
{
	GstNonstreamAudioDecoderClass parent_class;
};


GType gst_openmpt_dec_get_type(void);


G_END_DECLS


#endif
