#include <config.h>
#include <gst/gst.h>
#include <stdio.h>
#include <stdarg.h>

#include "gstgmedec.h"
#include <gme/gme_custom_dprintf.h>


GST_DEBUG_CATEGORY_STATIC(gmedec_debug);
#define GST_CAT_DEFAULT gmedec_debug


enum
{
	PROP_0,
	PROP_ECHO,
	PROP_STEREO_SEPARATION,
	PROP_ENABLE_EFFECTS,
	PROP_ENABLE_SURROUND
};


#define DEFAULT_ECHO               0.2
#define DEFAULT_STEREO_SEPARATION  0.2
#define DEFAULT_ENABLE_EFFECTS     FALSE
#define DEFAULT_ENABLE_SURROUND    TRUE



static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE(
	"sink",
	GST_PAD_SINK,
	GST_PAD_ALWAYS,
	GST_STATIC_CAPS(
		"audio/x-ay ; "
		"audio/x-gbs ; "
		"audio/x-gym ; "
		"audio/x-hes ; "
		"audio/x-kss ; "
		"audio/x-nsf ; "
		"audio/x-nsfe ; "
		"audio/x-sap ; "
		"audio/x-sgc ; "
		"audio/x-spc ; "
		"audio/x-vgm ; "
	)
);

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE(
	"src",
	GST_PAD_SRC,
	GST_PAD_ALWAYS,
	GST_STATIC_CAPS(
		"audio/x-raw, "
		"format = (string) " GST_AUDIO_NE(S16) ", "
		"layout = (string) interleaved, "
		"rate = (int) [ 1, 48000 ], "
		"channels = (int) 2 "
	)
);



G_DEFINE_TYPE(GstGmeDec, gst_gme_dec, GST_TYPE_NONSTREAM_AUDIO_DECODER)



static void gst_gme_dec_finalize(GObject *object);

static void gst_gme_dec_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec);
static void gst_gme_dec_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);

static gboolean gst_gme_dec_seek(GstNonstreamAudioDecoder *dec, GstClockTime *new_position);
static GstClockTime gst_gme_dec_tell(GstNonstreamAudioDecoder *dec);

static GstTagList* gst_gme_dec_tags_from_track_info(GstGmeDec *gme_dec, guint track_nr);
static GstClockTime gst_gme_dec_duration_from_track_info(GstGmeDec *gme_dec, guint track_nr);

static gboolean gst_gme_dec_load_from_buffer(GstNonstreamAudioDecoder *dec, GstBuffer *source_data, guint initial_subsong, GstClockTime *initial_position, GstNonstreamAudioOutputMode *initial_output_mode, gint *initial_num_loops);

static void gst_gme_dec_update_effects(GstGmeDec *gme_dec);

static gboolean gst_gme_dec_set_current_subsong(GstNonstreamAudioDecoder *dec, guint subsong, GstClockTime *initial_position);
static guint gst_gme_dec_get_current_subsong(GstNonstreamAudioDecoder *dec);

static guint gst_gme_dec_get_num_subsongs(GstNonstreamAudioDecoder *dec);
static GstClockTime gst_gme_dec_get_subsong_duration(GstNonstreamAudioDecoder *dec, guint subsong);
static GstTagList* gst_gme_dec_get_subsong_tags(GstNonstreamAudioDecoder *dec, guint subsong);

static gboolean gst_gme_dec_set_num_loops(GstNonstreamAudioDecoder *dec, gint num_loops);
static gint gst_gme_dec_get_num_loops(GstNonstreamAudioDecoder *dec);

static guint gst_gme_dec_get_supported_output_modes(GstNonstreamAudioDecoder *dec);
static gboolean gst_gme_dec_decode(GstNonstreamAudioDecoder *dec, GstBuffer **buffer, guint *num_samples);

#ifdef CUSTOM_DPRINTF_FUNCTION
static void gst_gme_dec_custom_dprintf(const char * fmt, va_list vl);
#endif



void gst_gme_dec_class_init(GstGmeDecClass *klass)
{
	GObjectClass *object_class;
	GstElementClass *element_class;
	GstNonstreamAudioDecoderClass *dec_class;

	GST_DEBUG_CATEGORY_INIT(gmedec_debug, "gmedec", 0, "video game music player");

	object_class = G_OBJECT_CLASS(klass);
	element_class = GST_ELEMENT_CLASS(klass);
	dec_class = GST_NONSTREAM_AUDIO_DECODER_CLASS(klass);

	gst_element_class_add_pad_template(element_class, gst_static_pad_template_get(&sink_template));
	gst_element_class_add_pad_template(element_class, gst_static_pad_template_get(&src_template));

	object_class->finalize = GST_DEBUG_FUNCPTR(gst_gme_dec_finalize);
	object_class->set_property = GST_DEBUG_FUNCPTR(gst_gme_dec_set_property);
	object_class->get_property = GST_DEBUG_FUNCPTR(gst_gme_dec_get_property);

	dec_class->seek = GST_DEBUG_FUNCPTR(gst_gme_dec_seek);
	dec_class->tell = GST_DEBUG_FUNCPTR(gst_gme_dec_tell);
	dec_class->load_from_buffer = GST_DEBUG_FUNCPTR(gst_gme_dec_load_from_buffer);
	dec_class->set_num_loops = GST_DEBUG_FUNCPTR(gst_gme_dec_set_num_loops);
	dec_class->get_num_loops = GST_DEBUG_FUNCPTR(gst_gme_dec_get_num_loops);
	dec_class->get_supported_output_modes = GST_DEBUG_FUNCPTR(gst_gme_dec_get_supported_output_modes);
	dec_class->decode = GST_DEBUG_FUNCPTR(gst_gme_dec_decode);
	dec_class->set_current_subsong = GST_DEBUG_FUNCPTR(gst_gme_dec_set_current_subsong);
	dec_class->get_current_subsong = GST_DEBUG_FUNCPTR(gst_gme_dec_get_current_subsong);
	dec_class->get_num_subsongs = GST_DEBUG_FUNCPTR(gst_gme_dec_get_num_subsongs);
	dec_class->get_subsong_duration = GST_DEBUG_FUNCPTR(gst_gme_dec_get_subsong_duration);
	dec_class->get_subsong_tags = GST_DEBUG_FUNCPTR(gst_gme_dec_get_subsong_tags);

	g_object_class_install_property(
		object_class,
		PROP_ECHO,
		g_param_spec_double(
			"echo",
			"Amount of echo",
			"Amount of echo to apply; 0.0 = none  1.0 = maximum (has no effect on GYM,SPC,VGM music)",
			0.0, 1.0,
			DEFAULT_ECHO,
			G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS
		)
	);
	g_object_class_install_property(
		object_class,
		PROP_STEREO_SEPARATION,
		g_param_spec_double(
			"stereo-separation",
			"Stereo separation",
			"Stereo separation strength; 0.0 = none (mono)  1.0 = hard left/right separation (has no effect on GYM,SPC,VGM music)",
			0.0, 1.0,
			DEFAULT_STEREO_SEPARATION,
			G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS
		)
	);
	g_object_class_install_property(
		object_class,
		PROP_ENABLE_EFFECTS,
		g_param_spec_boolean(
			"enable-effects",
			"Enable postprocessing effects",
			"Enable postprocessing effects (stereo separation, echo, surround; has no effect on GYM,SPC,VGM music)",
			DEFAULT_ENABLE_EFFECTS,
			G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS
		)
	);
	g_object_class_install_property(
		object_class,
		PROP_ENABLE_SURROUND,
		g_param_spec_boolean(
			"enable-surround",
			"Enable surround",
			"Enable a fake surround sound by phase-inverting some channels (has no effect on GYM,SPC,VGM music)",
			DEFAULT_ENABLE_SURROUND,
			G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS
		)
	);

	gst_element_class_set_static_metadata(
		element_class,
		"video game music player",
		"Codec/Decoder/Audio",
		"Plays video game music using the Game Music Emulator library",
		"Carlos Rafael Giani <dv@pseudoterminal.org>"
	);

#ifdef CUSTOM_DPRINTF_FUNCTION
	gme_custom_dprintf = gst_gme_dec_custom_dprintf;
#endif
}


void gst_gme_dec_init(GstGmeDec *gme_dec)
{
	gme_dec->emu = NULL;
	gme_dec->cur_track = 0;
	gme_dec->num_tracks = 0;

	gme_dec->echo = DEFAULT_ECHO;
	gme_dec->stereo_separation = DEFAULT_STEREO_SEPARATION;
	gme_dec->enable_effects = DEFAULT_ENABLE_EFFECTS;
	gme_dec->enable_surround = DEFAULT_ENABLE_SURROUND;
}


static void gst_gme_dec_finalize(GObject *object)
{
	GstGmeDec *gme_dec;

	g_return_if_fail(GST_IS_GME_DEC(object));
	gme_dec = GST_GME_DEC(object);

	if (gme_dec->emu != NULL)
		gme_delete(gme_dec->emu);
}


static void gst_gme_dec_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	GstNonstreamAudioDecoder *dec;
	GstGmeDec *gme_dec;

	dec = GST_NONSTREAM_AUDIO_DECODER(object);
	gme_dec = GST_GME_DEC(dec);

	switch (prop_id)
	{
		case PROP_ECHO:
		case PROP_STEREO_SEPARATION:
		case PROP_ENABLE_EFFECTS:
		case PROP_ENABLE_SURROUND:
		{
			GST_NONSTREAM_AUDIO_DECODER_LOCK_MUTEX(dec);

			switch (prop_id)
			{
				case PROP_ECHO:
					gme_dec->echo = g_value_get_double(value);
					break;
				case PROP_STEREO_SEPARATION:
					gme_dec->stereo_separation = g_value_get_double(value);
					break;
				case PROP_ENABLE_EFFECTS:
					gme_dec->enable_effects = g_value_get_boolean(value);
					break;
				case PROP_ENABLE_SURROUND:
					gme_dec->enable_surround = g_value_get_boolean(value);
					break;
				default:
					break;
			}

			gst_gme_dec_update_effects(gme_dec);

			GST_NONSTREAM_AUDIO_DECODER_UNLOCK_MUTEX(dec);

			break;
		}
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
			break;
	}
}


static void gst_gme_dec_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	GstGmeDec *gme_dec = GST_GME_DEC(object);

	switch (prop_id)
	{
		case PROP_ECHO:
		case PROP_STEREO_SEPARATION:
		case PROP_ENABLE_EFFECTS:
		case PROP_ENABLE_SURROUND:
		{
			GST_NONSTREAM_AUDIO_DECODER_LOCK_MUTEX(object);

			switch (prop_id)
			{
				case PROP_ECHO:
					g_value_set_double(value, gme_dec->echo);
					break;
				case PROP_STEREO_SEPARATION:
					g_value_set_double(value, gme_dec->stereo_separation);
					break;
				case PROP_ENABLE_EFFECTS:
					g_value_set_boolean(value, gme_dec->enable_effects);
					break;
				case PROP_ENABLE_SURROUND:
					g_value_set_boolean(value, gme_dec->enable_surround);
					break;
				default:
					break;
			}

			GST_NONSTREAM_AUDIO_DECODER_UNLOCK_MUTEX(object);

			break;
		}
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
			break;
	}
}


static gboolean gst_gme_dec_seek(GstNonstreamAudioDecoder *dec, GstClockTime *new_position)
{
	gme_err_t err;
	GstGmeDec *gme_dec = GST_GME_DEC(dec);
	g_return_val_if_fail(gme_dec->emu != NULL, FALSE);

	err = gme_seek(gme_dec->emu, *new_position / GST_MSECOND);
	if (G_UNLIKELY(err != NULL))
	{
		GST_ERROR_OBJECT(dec, "error while seeking: %s", err);
		return FALSE;
	}
	else
	{
		*new_position = gst_gme_dec_tell(dec);
		GST_DEBUG_OBJECT(dec, "position after seeking: %" GST_TIME_FORMAT, GST_TIME_ARGS(*new_position));
		return TRUE;
	}
}


static GstClockTime gst_gme_dec_tell(GstNonstreamAudioDecoder *dec)
{
	GstGmeDec *gme_dec = GST_GME_DEC(dec);
	g_return_val_if_fail(gme_dec->emu != NULL, GST_CLOCK_TIME_NONE);

	return (GstClockTime)(gme_tell(gme_dec->emu)) * GST_MSECOND;
}


static GstTagList* gst_gme_dec_tags_from_track_info(GstGmeDec *gme_dec, guint track_nr)
{
	GstTagList *tags;
	gme_err_t err;
	gme_info_t *track_info;

	g_return_val_if_fail(gme_dec->emu != NULL, FALSE);

	tags = NULL;

	err = gme_track_info(gme_dec->emu, &track_info, track_nr);
	if (G_UNLIKELY(err != NULL))
	{
		gme_free_info(track_info);
		GST_ERROR_OBJECT(gme_dec, "error while trying to get track information: %s", err);
		return NULL;
	}

	tags = gst_tag_list_new_empty();

#define GME_ADD_TO_TAGS(INFO_FIELD, TAG_TYPE) \
	if (track_info->INFO_FIELD && *(track_info->INFO_FIELD)) \
		gst_tag_list_add(tags, GST_TAG_MERGE_REPLACE, (TAG_TYPE), track_info->INFO_FIELD, NULL);

	GME_ADD_TO_TAGS(system, GST_TAG_ENCODER);
	GME_ADD_TO_TAGS(game, GST_TAG_ALBUM);
	GME_ADD_TO_TAGS(song, GST_TAG_TITLE);
	GME_ADD_TO_TAGS(author, GST_TAG_ARTIST);
	GME_ADD_TO_TAGS(copyright, GST_TAG_COPYRIGHT);
	GME_ADD_TO_TAGS(comment, GST_TAG_COMMENT);
	GME_ADD_TO_TAGS(dumper, GST_TAG_CONTACT);

#undef GME_ADD_TO_TAGS

	gme_free_info(track_info);

	return tags;
}


static GstClockTime gst_gme_dec_duration_from_track_info(GstGmeDec *gme_dec, guint track_nr)
{
	gme_err_t err;
	gme_info_t *track_info;
	GstClockTime duration;

	err = gme_track_info(gme_dec->emu, &track_info, track_nr);
	if (G_UNLIKELY(err != NULL))
	{
		GST_ERROR_OBJECT(gme_dec, "error while trying to get track information: %s", err);
		duration = GST_CLOCK_TIME_NONE;
	}
	else
	{
		GST_DEBUG_OBJECT(
			gme_dec,
			"track info length stats:  length: %d  intro length: %d  loop length: %d  play length: %d",
			track_info->length,
			track_info->intro_length,
			track_info->loop_length,
			track_info->play_length
		);
		duration = (GstClockTime)(track_info->play_length) * GST_MSECOND;
	}

	gme_free_info(track_info);

	return duration;
}


static gboolean gst_gme_dec_load_from_buffer(GstNonstreamAudioDecoder *dec, GstBuffer *source_data, guint initial_subsong, GstClockTime *initial_position, GstNonstreamAudioOutputMode *initial_output_mode, gint *initial_num_loops)
{
	GstMapInfo map;
	gme_err_t err;
	GstGmeDec *gme_dec;
	gint sample_rate;
	
	gme_dec = GST_GME_DEC(dec);

	sample_rate = 48000;
	gst_nonstream_audio_decoder_get_downstream_info(dec, NULL, &sample_rate, NULL);

	/* Set output format */
	if (!gst_nonstream_audio_decoder_set_output_format_simple(
		dec,
		sample_rate,
		GST_AUDIO_FORMAT_S16,
		2
	))
		return FALSE;

	gst_buffer_map(source_data, &map, GST_MAP_READ);
	err = gme_open_data(map.data, map.size, &(gme_dec->emu), sample_rate);
	gst_buffer_unmap(source_data, &map);

	if (G_UNLIKELY(err != NULL))
	{
		GST_ERROR_OBJECT(dec, "error while loading: %s", err);
		return FALSE;
	}

	gme_dec->num_tracks = gme_track_count(gme_dec->emu);
	if (G_UNLIKELY(initial_subsong >= gme_dec->num_tracks))
	{
		GST_WARNING_OBJECT(gme_dec, "initial subsong %u out of bounds (there are %u subsongs) - setting it to 0", initial_subsong, gme_dec->num_tracks);
		initial_subsong = 0;
	}

	GST_INFO_OBJECT(gme_dec, "%d track(s) (= subsong(s)) available", gme_dec->num_tracks);

	err = gme_start_track(gme_dec->emu, initial_subsong);
	if (G_UNLIKELY(err != NULL))
	{
		GST_ERROR_OBJECT(dec, "error while starting track: %s", err);
		return FALSE;
	}

	*initial_position = 0;
	*initial_output_mode = GST_NONSTREM_AUDIO_OUTPUT_MODE_STEADY;

	gst_gme_dec_update_effects(gme_dec);

	return TRUE;
}


static void gst_gme_dec_update_effects(GstGmeDec *gme_dec)
{
	gme_effects_t effects;

	if (gme_dec->emu == NULL)
		return;

	gme_effects(gme_dec->emu, &effects);
	effects.echo = gme_dec->echo;
	effects.stereo = gme_dec->stereo_separation;
	effects.enabled = gme_dec->enable_effects;
	effects.surround = gme_dec->enable_surround;
	gme_set_effects(gme_dec->emu, &effects);
}


static gboolean gst_gme_dec_set_current_subsong(GstNonstreamAudioDecoder *dec, guint subsong, GstClockTime *initial_position)
{
	gme_err_t err;
	GstGmeDec *gme_dec = GST_GME_DEC(dec);
	g_return_val_if_fail(gme_dec->emu != NULL, FALSE);

	err = gme_start_track(gme_dec->emu, subsong);
	if (G_UNLIKELY(err != NULL))
	{
		GST_ERROR_OBJECT(dec, "error while starting track: %s", err);
		return FALSE;
	}

	gme_dec->cur_track = subsong;
	*initial_position = 0;

	return TRUE;
}


static guint gst_gme_dec_get_current_subsong(GstNonstreamAudioDecoder *dec)
{
	GstGmeDec *gme_dec = GST_GME_DEC(dec);
	return gme_dec->cur_track;
}


static guint gst_gme_dec_get_num_subsongs(GstNonstreamAudioDecoder *dec)
{
	GstGmeDec *gme_dec = GST_GME_DEC(dec);
	return gme_dec->num_tracks;
}


static GstClockTime gst_gme_dec_get_subsong_duration(GstNonstreamAudioDecoder *dec, guint subsong)
{
	return gst_gme_dec_duration_from_track_info(GST_GME_DEC(dec), subsong);
}


static GstTagList* gst_gme_dec_get_subsong_tags(GstNonstreamAudioDecoder *dec, guint subsong)
{
	return gst_gme_dec_tags_from_track_info(GST_GME_DEC(dec), subsong);
}


static guint gst_gme_dec_get_supported_output_modes(G_GNUC_UNUSED GstNonstreamAudioDecoder *dec)
{
	return 1u << GST_NONSTREM_AUDIO_OUTPUT_MODE_STEADY;
}


/* TODO: looping control */
static gboolean gst_gme_dec_set_num_loops(GstNonstreamAudioDecoder *dec, gint num_loops)
{
	return 0;
}


static gint gst_gme_dec_get_num_loops(GstNonstreamAudioDecoder *dec)
{
	return 0;
}


static gboolean gst_gme_dec_decode(GstNonstreamAudioDecoder *dec, GstBuffer **buffer, guint *num_samples)
{
	gme_err_t err;
	GstGmeDec *gme_dec;
	GstBuffer *outbuf;
	GstMapInfo map;

	gint const num_samples_per_outbuf = 1024;
	gint const num_bytes_per_outbuf = num_samples_per_outbuf * 2 * 2; // 2 bytes per sample, 2 channels

	gme_dec = GST_GME_DEC(dec);

	outbuf = gst_nonstream_audio_decoder_allocate_output_buffer(dec, num_bytes_per_outbuf);
	if (G_UNLIKELY(outbuf == NULL))
		return FALSE;

	gst_buffer_map(outbuf, &map, GST_MAP_WRITE);
	err = gme_play(gme_dec->emu, num_samples_per_outbuf * 2 /* 2 channels */ , (short *) (map.data));
	gst_buffer_unmap(outbuf, &map);

	if (G_UNLIKELY(err != NULL))
	{
		GST_ERROR_OBJECT(dec, "error while decoding: %s", err);
		gst_buffer_unref(outbuf);
		return FALSE;
	}

	*buffer = outbuf;
	*num_samples = num_samples_per_outbuf;

	return TRUE;
}


#ifdef CUSTOM_DPRINTF_FUNCTION

static void gst_gme_dec_custom_dprintf( const char * fmt, va_list vl )
{
	gst_debug_log_valist(GST_CAT_DEFAULT, GST_LEVEL_DEBUG, __FILE__, "custom_gme_dprintf", __LINE__, NULL, fmt, vl);
}

#endif



static gboolean plugin_init(GstPlugin *plugin)
{
	if (!gst_element_register(plugin, "gmedec", GST_RANK_PRIMARY + 1, gst_gme_dec_get_type())) return FALSE;
	return TRUE;
}

GST_PLUGIN_DEFINE(
	GST_VERSION_MAJOR,
	GST_VERSION_MINOR,
	gmedec,
	"video game music module player",
	plugin_init,
	"1.0",
	"LGPL",
	"package",
	"http://no-url-yet"
)

