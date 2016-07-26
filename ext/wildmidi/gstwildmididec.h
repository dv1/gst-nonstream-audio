#ifndef GSTWILDMIDIDEC_H
#define GSTWILDMIDIDEC_H


#include <gst/gst.h>
#include "gst/audio/gstnonstreamaudiodecoder.h"
#include <wildmidi_lib.h>


G_BEGIN_DECLS


typedef struct _GstWildmidiDec GstWildmidiDec;
typedef struct _GstWildmidiDecClass GstWildmidiDecClass;


#define GST_TYPE_WILDMIDI_DEC             (gst_wildmidi_dec_get_type())
#define GST_WILDMIDI_DEC(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_WILDMIDI_DEC, GstWildmidiDec))
#define GST_WILDMIDI_DEC_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_WILDMIDI_DEC, GstWildmidiDecClass))
#define GST_IS_WILDMIDI_DEC(obj)          (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_WILDMIDI_DEC))
#define GST_IS_WILDMIDI_DEC_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_WILDMIDI_DEC))


struct _GstWildmidiDec
{
	GstNonstreamAudioDecoder parent;

	midi *song;

	gboolean log_volume_scale;
	gboolean enhanced_resampling;
	gboolean reverb;
	guint output_buffer_size;
};


struct _GstWildmidiDecClass
{
	GstNonstreamAudioDecoderClass parent_class;
};


GType gst_wildmidi_dec_get_type(void);


G_END_DECLS


#endif
