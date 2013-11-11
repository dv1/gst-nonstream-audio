#include <config.h>
#include <gst/gst.h>
#include <stdlib.h>

#include "gstuaderawdec.h"


GST_DEBUG_CATEGORY_STATIC(uaderawdec_debug);
#define GST_CAT_DEFAULT uaderawdec_debug


enum
{
	PROP_0,
	PROP_LOCATION,
	PROP_UADECORE_FILE,
	PROP_BASE_DIRECTORY,
	PROP_FILTER_TYPE,
	PROP_HEADPHONE_MODE,
	PROP_USE_FILTER,
	PROP_GAIN,
	PROP_USE_POSTPROCESSING,
	PROP_PANNING
};


#define DEFAULT_LOCATION NULL
#define DEFAULT_UADECORE_FILE UADE_CONFIG_UADE_CORE
#define DEFAULT_BASE_DIRECTORY UADE_CONFIG_BASE_DIR
#define DEFAULT_FILTER_TYPE GST_UADE_FILTER_TYPE_A500
#define DEFAULT_HEADPHONE_MODE GST_UADE_HEADPHONE_MODE_NONE
#define DEFAULT_USE_FILTER FALSE
#define DEFAULT_GAIN 1.0
#define DEFAULT_USE_POSTPROCESSING TRUE
#define DEFAULT_PANNING 0.0

#define NUM_SAMPLES_PER_OUTBUF 1024



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



G_DEFINE_TYPE(GstUadeRawDec, gst_uade_raw_dec, GST_TYPE_NONSTREAM_AUDIO_DECODER)



static void gst_uade_raw_dec_finalize(GObject *object);

static void gst_uade_raw_dec_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec);
static void gst_uade_raw_dec_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);

static gboolean gst_uade_raw_dec_load_from_custom(GstNonstreamAudioDecoder *dec, guint initial_subsong, GstClockTime *initial_position, GstNonstreamAudioOutputMode *initial_output_mode);

static gboolean gst_uade_raw_dec_set_current_subsong(GstNonstreamAudioDecoder *dec, guint subsong, GstClockTime *initial_position);
static guint gst_uade_raw_dec_get_current_subsong(GstNonstreamAudioDecoder *dec);

static guint gst_uade_raw_dec_get_num_subsongs(GstNonstreamAudioDecoder *dec);

static guint gst_uade_raw_dec_get_supported_output_modes(GstNonstreamAudioDecoder *dec);
static gboolean gst_uade_raw_dec_decode(GstNonstreamAudioDecoder *dec, GstBuffer **buffer, guint *num_samples);



void gst_uade_raw_dec_class_init(GstUadeRawDecClass *klass)
{
	GObjectClass *object_class;
	GstElementClass *element_class;
	GstNonstreamAudioDecoderClass *dec_class;

	GST_DEBUG_CATEGORY_INIT(uaderawdec_debug, "uaderawdec", 0, "video game music player");

	object_class = G_OBJECT_CLASS(klass);
	element_class = GST_ELEMENT_CLASS(klass);
	dec_class = GST_NONSTREAM_AUDIO_DECODER_CLASS(klass);

	gst_element_class_add_pad_template(element_class, gst_static_pad_template_get(&src_template));

	object_class->finalize = GST_DEBUG_FUNCPTR(gst_uade_raw_dec_finalize);
	object_class->set_property = GST_DEBUG_FUNCPTR(gst_uade_raw_dec_set_property);
	object_class->get_property = GST_DEBUG_FUNCPTR(gst_uade_raw_dec_get_property);

	dec_class->loads_from_sinkpad = FALSE;

	dec_class->load_from_custom = GST_DEBUG_FUNCPTR(gst_uade_raw_dec_load_from_custom);

	dec_class->get_supported_output_modes = GST_DEBUG_FUNCPTR(gst_uade_raw_dec_get_supported_output_modes);
	dec_class->decode = GST_DEBUG_FUNCPTR(gst_uade_raw_dec_decode);

	dec_class->set_current_subsong = GST_DEBUG_FUNCPTR(gst_uade_raw_dec_set_current_subsong);
	dec_class->get_current_subsong = GST_DEBUG_FUNCPTR(gst_uade_raw_dec_get_current_subsong);
	dec_class->get_num_subsongs = GST_DEBUG_FUNCPTR(gst_uade_raw_dec_get_num_subsongs);

	/* TODO: inspect:
	 * - timeouts
	 * - pal/ntsc
	 * - looping control
	 * - typefind-like utility function
	 * - resampler [default,sinc,none]
	 * - check fixed caps & see if downstream caps' sample rate can be adapted
	 */

	g_object_class_install_property(
		object_class,
		PROP_LOCATION,
		g_param_spec_string(
			"location",
			"File Location",
			"Location of the music file to play",
			DEFAULT_LOCATION,
			G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS
		)
	);
	g_object_class_install_property(
		object_class,
		PROP_UADECORE_FILE,
		g_param_spec_string(
			"uadecore-file",
			"uadecore file location",
			"Location of the uadecore file",
			DEFAULT_LOCATION,
			G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS
		)
	);
	g_object_class_install_property(
		object_class,
		PROP_BASE_DIRECTORY,
		g_param_spec_string(
			"base-directory",
			"Base directory",
			"Directory containing eagleplayer.conf , the score file, and the players subdirectory",
			DEFAULT_LOCATION,
			G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS
		)
	);
	g_object_class_install_property(
		object_class,
		PROP_FILTER_TYPE,
		g_param_spec_enum(
			"filter-type",
			"Playback filter type",
			"Lowpass filter for the audio playback",
			GST_TYPE_UADE_FILTER_TYPE,
			DEFAULT_FILTER_TYPE,
			G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS
		)
	);
	g_object_class_install_property(
		object_class,
		PROP_HEADPHONE_MODE,
		g_param_spec_enum(
			"headphone-mode",
			"Headphone mode",
			"Headphone output mode to use",
			GST_TYPE_UADE_HEADPHONE_MODE,
			DEFAULT_HEADPHONE_MODE,
			G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS
		)
	);
	g_object_class_install_property(
		object_class,
		PROP_USE_FILTER,
		g_param_spec_boolean(
			"use-filter",
			"Use the lowpass filter",
			"Whether or not to use the configured lowpass filter",
			DEFAULT_USE_FILTER,
			G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS
		)
	);
	g_object_class_install_property(
		object_class,
		PROP_GAIN,
		g_param_spec_double(
			"gain",
			"Gain",
			"Gain to apply on the output; 0.0 = silence  1.0 = 100% (no change)",
			0.0, 128.0,
			DEFAULT_GAIN,
			G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS
		)
	);
	g_object_class_install_property(
		object_class,
		PROP_USE_POSTPROCESSING,
		g_param_spec_boolean(
			"use-postprocessing",
			"Use postprocessing",
			"Whether or not to use postprocessing effects (if set to FALSE, this disables: headphone mode, panning, gain)",
			DEFAULT_USE_POSTPROCESSING,
			G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS
		)
	);
	g_object_class_install_property(
		object_class,
		PROP_PANNING,
		g_param_spec_double(
			"panning",
			"Panning amount",
			"Amount of panning to apply; 0.0 = full stereo  1.0 = mono  2.0 = inverse stereo",
			0.0, 2.0,
			DEFAULT_PANNING,
			G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS
		)
	);

	gst_element_class_set_static_metadata(
		element_class,
		"UADE Amiga music player",
		"Codec/Decoder/Source/Audio",
		"Plays Commodore Amiga game and demo music",
		"Carlos Rafael Giani <dv@pseudoterminal.org>"
	);
}


void gst_uade_raw_dec_init(GstUadeRawDec *uade_raw_dec)
{
	uade_raw_dec->state = NULL;
	uade_raw_dec->info  = NULL;

	uade_raw_dec->location           = NULL;
	uade_raw_dec->uadecore_file      = g_strdup(DEFAULT_UADECORE_FILE);
	uade_raw_dec->base_directory     = g_strdup(DEFAULT_BASE_DIRECTORY);
	uade_raw_dec->filter_type        = DEFAULT_FILTER_TYPE;
	uade_raw_dec->headphone_mode     = DEFAULT_HEADPHONE_MODE;
	uade_raw_dec->use_filter         = DEFAULT_USE_FILTER;
	uade_raw_dec->gain               = DEFAULT_GAIN;
	uade_raw_dec->use_postprocessing = DEFAULT_USE_POSTPROCESSING;
	uade_raw_dec->panning            = DEFAULT_PANNING;

	uade_raw_dec->playback_started = FALSE;
	uade_raw_dec->current_subsong  = 0;
}


static void gst_uade_raw_dec_finalize(GObject *object)
{
	GstUadeRawDec *uade_raw_dec;

	g_return_if_fail(GST_IS_UADE_RAW_DEC(object));
	uade_raw_dec = GST_UADE_RAW_DEC(object);

	if (uade_raw_dec->playback_started)
		uade_stop(uade_raw_dec->state);
	uade_cleanup_state(uade_raw_dec->state);

	g_free(uade_raw_dec->location);
	g_free(uade_raw_dec->uadecore_file);
	g_free(uade_raw_dec->base_directory);

	G_OBJECT_CLASS(gst_uade_raw_dec_parent_class)->finalize(object);
}


static void gst_uade_raw_dec_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	GstUadeRawDec *uade_raw_dec = GST_UADE_RAW_DEC(object);

#define CHECK_IF_ALREADY_INITIALIZED() \
	do { \
		if (uade_raw_dec->state != NULL) \
		{ \
			GST_ERROR_OBJECT(uade_raw_dec, "changes to the %s property after playback already started are not supported", g_param_spec_get_name(pspec)); \
			return; \
		} \
	} while (0)

#define UPDATE_EXISTING_NUM_CONFIG(OPTION, VALUE) \
	do { \
		if (uade_raw_dec->state != NULL) \
		{ \
			gchar *tmpstr = g_strdup_printf("%f", (VALUE)); \
			struct uade_config *cfg = uade_get_effective_config(uade_raw_dec->state); \
			uade_config_set_option(cfg, (OPTION), tmpstr); \
			g_free(tmpstr); \
		} \
	} while (0)

	switch (prop_id)
	{
		case PROP_LOCATION:
			if (uade_raw_dec->location != NULL)
			{
				GST_ERROR_OBJECT(uade_raw_dec, "a music file is already opened; reopening is not supported");
				return;
			}
			uade_raw_dec->location = g_strdup(g_value_get_string(value));
			break;
		case PROP_UADECORE_FILE:
			CHECK_IF_ALREADY_INITIALIZED();
			g_free(uade_raw_dec->uadecore_file);
			uade_raw_dec->uadecore_file = g_strdup(g_value_get_string(value));
			break;
		case PROP_BASE_DIRECTORY:
			CHECK_IF_ALREADY_INITIALIZED();
			g_free(uade_raw_dec->base_directory);
			uade_raw_dec->base_directory = g_strdup(g_value_get_string(value));
			break;
		case PROP_FILTER_TYPE:
			CHECK_IF_ALREADY_INITIALIZED();
			uade_raw_dec->filter_type = g_value_get_enum(value);
			break;
		case PROP_HEADPHONE_MODE:
			CHECK_IF_ALREADY_INITIALIZED();
			uade_raw_dec->headphone_mode = g_value_get_enum(value);
			break;
		case PROP_USE_FILTER:
			uade_raw_dec->use_filter = g_value_get_boolean(value);
			if (uade_raw_dec->state != NULL)
				uade_set_filter_state(uade_raw_dec->state, uade_raw_dec->use_filter);
			break;
		case PROP_GAIN:
			uade_raw_dec->gain = g_value_get_double(value);
			UPDATE_EXISTING_NUM_CONFIG(UC_GAIN, uade_raw_dec->panning);
			break;
		case PROP_USE_POSTPROCESSING:
			uade_raw_dec->use_postprocessing = g_value_get_boolean(value);
			if (uade_raw_dec->state != NULL)
				uade_effect_enable(uade_raw_dec->state, UADE_EFFECT_ALLOW);
			break;
		case PROP_PANNING:
			uade_raw_dec->panning = g_value_get_double(value);
			UPDATE_EXISTING_NUM_CONFIG(UC_PANNING_VALUE, uade_raw_dec->panning);
			break;
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
			break;
	}

#undef CHECK_IF_ALREADY_INITIALIZED
#undef UPDATE_EXISTING_NUM_CONFIG
}


static void gst_uade_raw_dec_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	GstUadeRawDec *uade_raw_dec = GST_UADE_RAW_DEC(object);

	switch (prop_id)
	{
		case PROP_LOCATION:
			g_value_set_string(value, uade_raw_dec->location);
			break;
		case PROP_UADECORE_FILE:
			g_value_set_string(value, uade_raw_dec->uadecore_file);
			break;
		case PROP_BASE_DIRECTORY:
			g_value_set_string(value, uade_raw_dec->base_directory);
			break;
		case PROP_FILTER_TYPE:
			g_value_set_enum(value, uade_raw_dec->filter_type);
			break;
		case PROP_HEADPHONE_MODE:
			g_value_set_enum(value, uade_raw_dec->headphone_mode);
			break;
		case PROP_USE_FILTER:
			g_value_set_boolean(value, uade_raw_dec->use_filter);
			break;
		case PROP_GAIN:
			g_value_set_double(value, uade_raw_dec->gain);
			break;
		case PROP_USE_POSTPROCESSING:
			g_value_set_boolean(value, uade_raw_dec->use_postprocessing);
			break;
		case PROP_PANNING:
			g_value_set_double(value, uade_raw_dec->panning);
			break;
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
			break;
	}
}


static gboolean gst_uade_raw_dec_load_from_custom(GstNonstreamAudioDecoder *dec, guint initial_subsong, GstClockTime *initial_position, GstNonstreamAudioOutputMode *initial_output_mode)
{
	GstUadeRawDec *uade_raw_dec;
	int ret;
	GstTagList *tags;
	struct uade_config *config;
	gchar *tmpstr;

	uade_raw_dec = GST_UADE_RAW_DEC(dec);

	g_assert(uade_raw_dec->state == NULL);

	if (uade_raw_dec->location == NULL)
	{
		GST_ERROR_OBJECT(uade_raw_dec, "no location set -> nothing to play");
		return FALSE;
	}

	GST_TRACE_OBJECT(uade_raw_dec, "attempting to load music file \"%s\"", uade_raw_dec->location);


	config = uade_new_config();

	uade_config_set_option(config, UC_ONE_SUBSONG, NULL);
	uade_config_set_option(config, UC_NO_EP_END, NULL);

	uade_config_set_option(config, UC_UADECORE_FILE, uade_raw_dec->uadecore_file);
	uade_config_set_option(config, UC_BASE_DIR, uade_raw_dec->base_directory);

	switch (uade_raw_dec->filter_type)
	{
		case GST_UADE_FILTER_TYPE_A500:  uade_config_set_option(config, UC_FILTER_TYPE, "a500"); break;
		case GST_UADE_FILTER_TYPE_A1200: uade_config_set_option(config, UC_FILTER_TYPE, "a1200"); break;
		default: break;
	}
	switch (uade_raw_dec->headphone_mode)
	{
		case GST_UADE_HEADPHONE_MODE_NONE: uade_config_set_option(config, UC_NO_HEADPHONES, NULL); break;
		case GST_UADE_HEADPHONE_MODE_1:    uade_config_set_option(config, UC_HEADPHONES, NULL); break;
		case GST_UADE_HEADPHONE_MODE_2:    uade_config_set_option(config, UC_HEADPHONES2, NULL); break;
		default: break;
	}

	if (!(uade_raw_dec->use_filter))
		uade_config_set_option(config, UC_NO_FILTER, NULL); /* this must be called AFTER the filter type is set */

	tmpstr = g_strdup_printf("%f", uade_raw_dec->gain);
	uade_config_set_option(config, UC_GAIN, tmpstr);
	g_free(tmpstr);

	if (!(uade_raw_dec->use_postprocessing))
		uade_config_set_option(config, UC_NO_POSTPROCESSING, NULL);

	tmpstr = g_strdup_printf("%f", uade_raw_dec->panning);
	uade_config_set_option(config, UC_PANNING_VALUE, tmpstr);
	g_free(tmpstr);

	uade_raw_dec->state = uade_new_state(config);

	free(config);


	/* Set output format */
	gst_nonstream_audio_decoder_set_output_audioinfo_simple(
		GST_NONSTREAM_AUDIO_DECODER(uade_raw_dec),
		uade_get_sampling_rate(uade_raw_dec->state),
		GST_AUDIO_FORMAT_S16,
		2
	);

	ret = uade_play(uade_raw_dec->location, -1, uade_raw_dec->state);
	if (!ret)
	{
		GST_ERROR_OBJECT(uade_raw_dec, "uade_play failed");
		return FALSE;
	}

	GST_TRACE_OBJECT(uade_raw_dec, "loading successful, retrieving song information");

	uade_raw_dec->playback_started = TRUE;

	uade_raw_dec->info = uade_get_song_info(uade_raw_dec->state);
	if (uade_raw_dec->info == NULL)
	{
		GST_ERROR_OBJECT(uade_raw_dec, "uade_get_song_info failed");
		return FALSE;
	}

	GST_INFO_OBJECT(uade_raw_dec, "min subsong: %d  max subsong: %d", uade_raw_dec->info->subsongs.min, uade_raw_dec->info->subsongs.max);

	uade_raw_dec->current_subsong = CLAMP(((int)initial_subsong) + uade_raw_dec->info->subsongs.min, uade_raw_dec->info->subsongs.min, uade_raw_dec->info->subsongs.max);
	if (uade_seek(UADE_SEEK_SUBSONG_RELATIVE, 0, uade_raw_dec->current_subsong, uade_raw_dec->state) != 0)
	{
		GST_ERROR_OBJECT(uade_raw_dec, "seeking to initial subsong failed");
		return FALSE;
	}

	*initial_position = 0;
	*initial_output_mode = GST_NONSTREM_AUDIO_OUTPUT_MODE_STEADY;

	tags = gst_tag_list_new_empty();
	if (uade_raw_dec->info->modulename[0] != 0)
		gst_tag_list_add(tags, GST_TAG_MERGE_APPEND, GST_TAG_TITLE, uade_raw_dec->info->modulename, NULL);
	if (uade_raw_dec->info->formatname[0] != 0)
		gst_tag_list_add(tags, GST_TAG_MERGE_APPEND, GST_TAG_CONTAINER_FORMAT, uade_raw_dec->info->formatname, NULL);
	if (uade_raw_dec->info->playername[0] != 0)
		gst_tag_list_add(tags, GST_TAG_MERGE_APPEND, GST_TAG_APPLICATION_NAME, uade_raw_dec->info->playername, NULL);

	gst_pad_push_event(GST_NONSTREAM_AUDIO_DECODER_SRC_PAD(uade_raw_dec), gst_event_new_tag(tags));

	return TRUE;
}


static gboolean gst_uade_raw_dec_set_current_subsong(GstNonstreamAudioDecoder *dec, guint subsong, GstClockTime *initial_position)
{
	GstUadeRawDec *uade_raw_dec = GST_UADE_RAW_DEC(dec);

	g_return_val_if_fail(uade_raw_dec->state != NULL, FALSE);

	uade_raw_dec->current_subsong = CLAMP(((int)subsong) + uade_raw_dec->info->subsongs.min, uade_raw_dec->info->subsongs.min, uade_raw_dec->info->subsongs.max);
	*initial_position = 0;
	return (uade_seek(UADE_SEEK_SUBSONG_RELATIVE, 0, uade_raw_dec->current_subsong, uade_raw_dec->state) == 0);
}


static guint gst_uade_raw_dec_get_current_subsong(GstNonstreamAudioDecoder *dec)
{
	GstUadeRawDec *uade_raw_dec = GST_UADE_RAW_DEC(dec);
	return (uade_raw_dec->info == NULL) ? 0 : (uade_raw_dec->current_subsong - uade_raw_dec->info->subsongs.min);
}


static guint gst_uade_raw_dec_get_num_subsongs(GstNonstreamAudioDecoder *dec)
{
	GstUadeRawDec *uade_raw_dec = GST_UADE_RAW_DEC(dec);
	return (uade_raw_dec->info == NULL) ? 0 : (uade_raw_dec->info->subsongs.max - uade_raw_dec->info->subsongs.min + 1);
}


static guint gst_uade_raw_dec_get_supported_output_modes(G_GNUC_UNUSED GstNonstreamAudioDecoder *dec)
{
	return 1u << GST_NONSTREM_AUDIO_OUTPUT_MODE_STEADY;
}


static gboolean gst_uade_raw_dec_decode(GstNonstreamAudioDecoder *dec, GstBuffer **buffer, guint *num_samples)
{
	GstBuffer *outbuf;
	GstMapInfo map;
	GstUadeRawDec *uade_raw_dec;
	long num_samples_per_outbuf, num_bytes_per_outbuf, actual_num_samples_read, actual_num_bytes_read;

	uade_raw_dec = GST_UADE_RAW_DEC(dec);

	num_samples_per_outbuf = 1024;
	num_bytes_per_outbuf = num_samples_per_outbuf * (2 * 16 / 8);

	outbuf = gst_nonstream_audio_decoder_allocate_output_buffer(dec, num_bytes_per_outbuf);
	if (G_UNLIKELY(outbuf == NULL))
		return FALSE;

	gst_buffer_map(outbuf, &map, GST_MAP_WRITE);
	actual_num_bytes_read = uade_read(map.data, map.size, uade_raw_dec->state);
	gst_buffer_unmap(outbuf, &map);

	actual_num_samples_read = actual_num_bytes_read / (2 * 16 / 8);

	GST_TRACE_OBJECT(dec, "read %ld byte", actual_num_bytes_read);

	if (actual_num_samples_read > 0)
	{
		if (actual_num_bytes_read != num_bytes_per_outbuf)
			gst_buffer_set_size(outbuf, actual_num_bytes_read);

		*buffer = outbuf;
		*num_samples = actual_num_samples_read;
	}
	else
	{
		if (actual_num_bytes_read == 0)
			GST_INFO_OBJECT(uade_raw_dec, "UADE reached end of song");
		else if (actual_num_bytes_read < 0)
			GST_ERROR_OBJECT(uade_raw_dec, "UADE reported error during playback - shutting down");
		else
			GST_WARNING_OBJECT(uade_raw_dec, "only %ld byte decoded", actual_num_bytes_read);

		gst_buffer_unref(outbuf);
		*buffer = NULL;
		*num_samples = 0;
	}

	return ((*buffer) != NULL);
}

