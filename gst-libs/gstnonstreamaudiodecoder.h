#ifndef GSTNONSTREAMAUDIODEC_H
#define GSTNONSTREAMAUDIODEC_H

#include <gst/gst.h>
#include <gst/audio/audio.h>


G_BEGIN_DECLS


typedef struct _GstNonstreamAudioDecoder GstNonstreamAudioDecoder;
typedef struct _GstNonstreamAudioDecoderClass GstNonstreamAudioDecoderClass;


#define GST_TYPE_NONSTREAM_AUDIO_DECODER             (gst_nonstream_audio_decoder_get_type())
#define GST_NONSTREAM_AUDIO_DECODER(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_NONSTREAM_AUDIO_DECODER, GstNonstreamAudioDecoder))
#define GST_NONSTREAM_AUDIO_DECODER_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_NONSTREAM_AUDIO_DECODER, GstNonstreamAudioDecoderClass))
#define GST_NONSTREAM_AUDIO_DECODER_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS((obj), GST_TYPE_NONSTREAM_AUDIO_DECODER, GstNonstreamAudioDecoderClass))
#define GST_IS_NONSTREAM_AUDIO_DECODER(obj)          (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_NONSTREAM_AUDIO_DECODER))
#define GST_IS_NONSTREAM_AUDIO_DECODER_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_NONSTREAM_AUDIO_DECODER))

#define GST_NONSTREAM_AUDIO_DECODER_SINK_NAME    "sink"
#define GST_NONSTREAM_AUDIO_DECODER_SRC_NAME     "src"

#define GST_NONSTREAM_AUDIO_DECODER_SINK_PAD(obj)        (((GstNonstreamAudioDecoder *) (obj))->sinkpad)
#define GST_NONSTREAM_AUDIO_DECODER_SRC_PAD(obj)         (((GstNonstreamAudioDecoder *) (obj))->srcpad)

#define GST_NONSTREAM_AUDIO_DECODER_STREAM_LOCK(dec)   g_rec_mutex_lock(&(GST_NONSTREAM_AUDIO_DECODER(dec)->stream_lock))
#define GST_NONSTREAM_AUDIO_DECODER_STREAM_UNLOCK(dec) g_rec_mutex_unlock(&(GST_NONSTREAM_AUDIO_DECODER(dec)->stream_lock))


struct _GstNonstreamAudioDecoder
{
	GstElement element;
	GstPad *sinkpad, *srcpad;

	GstClockTime duration;
	guint64 offset;
	GstSegment cur_segment;

	guint num_subsongs;

	gboolean loaded;

	GstAudioInfo audio_info;
	gboolean output_format_changed;

	GstAllocator *allocator;
	GstAllocationParams allocation_params;

	GRecMutex stream_lock;
};


struct _GstNonstreamAudioDecoderClass
{
	GstElementClass element_class;

	/*< public >*/
	/* virtual methods for subclasses */

	gboolean (*seek)(GstNonstreamAudioDecoder *dec, GstClockTime new_position);
	GstClockTime (*tell)(GstNonstreamAudioDecoder *dec);

	gboolean (*load)(GstNonstreamAudioDecoder *dec, GstBuffer *source_data);

	guint (*get_current_subsong)(GstNonstreamAudioDecoder *dec);
	gboolean (*set_current_subsong)(GstNonstreamAudioDecoder *dec, guint subsong);

	gboolean (*decode)(GstNonstreamAudioDecoder *dec, GstBuffer **buffer, guint *num_samples);

	gboolean (*negotiate)(GstNonstreamAudioDecoder *dec);

	gboolean (*decide_allocation)(GstNonstreamAudioDecoder *dec, GstQuery *query);
	gboolean (*propose_allocation)(GstNonstreamAudioDecoder *dec, GstQuery * query);
};


GType gst_nonstream_audio_decoder_get_type(void);

void gst_nonstream_audio_decoder_set_duration(GstNonstreamAudioDecoder *dec, GstClockTime duration);

void gst_nonstream_audio_decoder_init_subsong_properties(GstNonstreamAudioDecoderClass *klass);
void gst_nonstream_audio_decoder_set_subsong_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec);
void gst_nonstream_audio_decoder_get_subsong_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);
void gst_nonstream_audio_decoder_set_num_subsongs(GstNonstreamAudioDecoder *dec, guint num_subsongs);

gboolean gst_nonstream_audio_decoder_set_output_audioinfo(GstNonstreamAudioDecoder *dec, GstAudioInfo const *info);
gboolean gst_nonstream_audio_decoder_negotiate(GstNonstreamAudioDecoder *dec);
void gst_nonstream_audio_decoder_get_downstream_format(GstNonstreamAudioDecoder *dec, gint *sample_rate, gint *num_channels);

GstBuffer * gst_nonstream_audio_decoder_allocate_output_buffer(GstNonstreamAudioDecoder *dec, gsize size);


G_END_DECLS


#endif

