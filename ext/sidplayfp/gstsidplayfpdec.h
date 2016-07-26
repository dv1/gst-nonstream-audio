#ifndef GSTSIDPLAYFPDEC_H
#define GSTSIDPLAYFPDEC_H


#include <gst/gst.h>
#include "gst/audio/gstnonstreamaudiodecoder.h"
#include <string>
#include <sidplayfp/sidplayfp.h>
#include <sidplayfp/SidTune.h>
#include <sidplayfp/SidConfig.h>
#include <sidplayfp/SidDatabase.h>
#include <sidplayfp/builders/residfp.h>


G_BEGIN_DECLS


typedef struct _GstSidplayfpDec GstSidplayfpDec;
typedef struct _GstSidplayfpDecClass GstSidplayfpDecClass;


#define GST_TYPE_SIDPLAYFP_DEC             (gst_sidplayfp_dec_get_type())
#define GST_SIDPLAYFP_DEC(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_SIDPLAYFP_DEC, GstSidplayfpDec))
#define GST_SIDPLAYFP_DEC_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_SIDPLAYFP_DEC, GstSidplayfpDecClass))
#define GST_IS_SIDPLAYFP_DEC(obj)          (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_SIDPLAYFP_DEC))
#define GST_IS_SIDPLAYFP_DEC_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_SIDPLAYFP_DEC))


typedef enum
{
	GST_SIDPLAYFP_DEC_KERNAL_ROM = 0,
	GST_SIDPLAYFP_DEC_BASIC_ROM = 1,
	GST_SIDPLAYFP_DEC_CHARACTER_GEN_ROM = 2
}
GstSidplayfpDecRomIndex;


struct _GstSidplayfpDec
{
	GstNonstreamAudioDecoder parent;

	sidplayfp engine;
	ReSIDfpBuilder builder;
	SidTune *tune;
	char md5[SidTune::MD5_LENGTH + 1];

	GstBuffer *rom_images[3];

	SidConfig::c64_model_t default_c64_model;
	gboolean force_c64_model;
	SidConfig::sid_model_t default_sid_model;
	gboolean force_sid_model;
	SidConfig::sampling_method_t sampling_method;

	guint fallback_song_length;
	gchar *hsvc_songlength_db_path;
	SidDatabase database;
	int_least32_t *subsong_lengths;

	unsigned int current_subsong;

	gint sample_rate, num_channels;

	gint num_loops;

	guint output_buffer_size;

	GstTagList *main_tags;
};


struct _GstSidplayfpDecClass
{
	GstNonstreamAudioDecoderClass parent_class;
};


GType gst_sidplayfp_dec_get_type(void);


G_END_DECLS


#endif
