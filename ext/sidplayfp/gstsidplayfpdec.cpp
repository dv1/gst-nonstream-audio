/*
 *   sidplayfp C64 SID music decoder based on the nonstream-audio GStreamer base class
 *   Copyright (C) 2016 Carlos Rafael Giani
 *
 *   This library is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU Lesser General Public
 *   License as published by the Free Software Foundation; either
 *   version 2.1 of the License, or (at your option) any later version.
 *
 *   This library is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *   Lesser General Public License for more details.
 *
 *   You should have received a copy of the GNU Lesser General Public
 *   License along with this library; if not, write to the Free Software
 *   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

/**
 * SECTION:element-sidplayfpdec
 * @see_also: #GstSidplayfpDec
 *
 * sidplayfpdec decodes SID music files.
 * It uses the <ulink url="https://sourceforge.net/p/sidplay-residfp/wiki/Home/">sidplayfp library</ulink>
 * for this purpose. It can be autoplugged and therefore works with decodebin.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch-1.0 filesrc location=media/example.sid ! sidplayfp ! audioconvert ! audioresample ! autoaudiosink
 * ]|
 * </refsect2>
 */


#ifdef HAVE_CONFIG_H
 #include <config.h>
#endif

#include <memory>
#include <string.h>
#include <gst/gst.h>

#include <sidplayfp/sidplayfp.h>
#include <sidplayfp/SidInfo.h>
#include <sidplayfp/SidTuneInfo.h>

#include "gstsidplayfpdec.h"


GST_DEBUG_CATEGORY_STATIC(sidplayfpdec_debug);
#define GST_CAT_DEFAULT sidplayfpdec_debug


/* C++11 deprecated auto_ptr in favor of unique_ptr (which was introduced in C++11) */
#if ((defined(_HAS_AUTO_PTR_ETC) && (_HAS_AUTO_PTR_ETC == 0)) || (__cplusplus >= 201103L))
#define USE_UNIQUE_PTR
#endif

#define DEFAULT_DEFAULT_C64_MODEL SidConfig::PAL
#define DEFAULT_FORCE_C64_MODEL FALSE
#define DEFAULT_DEFAULT_SID_MODEL SidConfig::MOS6581
#define DEFAULT_FORCE_SID_MODEL FALSE
#define DEFAULT_SAMPLING_METHOD SidConfig::RESAMPLE_INTERPOLATE


enum
{
	PROP_0,
	PROP_KERNAL_ROM,
	PROP_BASIC_ROM,
	PROP_CHARACTER_GEN_ROM,
	PROP_DEFAULT_C64_MODEL,
	PROP_FORCE_C64_MODEL,
	PROP_DEFAULT_SID_MODEL,
	PROP_FORCE_SID_MODEL,
	PROP_SAMPLING_METHOD,
	PROP_FALLBACK_SONG_LENGTH,
	PROP_HSVC_SONGLENGTH_DB_PATH,
	PROP_OUTPUT_BUFFER_SIZE
};


#define DEFAULT_OUTPUT_BUFFER_SIZE 1024

#define DEFAULT_SAMPLE_RATE 48000
#define DEFAULT_NUM_CHANNELS 2
#define DEFAULT_FALLBACK_SONG_LENGTH (3*60 + 30)
#define DEFAULT_HSVC_SONGLENGTH_DB_PATH NULL



static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE(
	"sink",
	GST_PAD_SINK,
	GST_PAD_ALWAYS,
	GST_STATIC_CAPS(
		"audio/x-sid"
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
		"rate = (int) [ 8000, MAX ], "
		"channels = (int) { 1, 2 } "
	)
);



G_DEFINE_TYPE(GstSidplayfpDec, gst_sidplayfp_dec, GST_TYPE_NONSTREAM_AUDIO_DECODER)



static void gst_sidplayfp_dec_finalize(GObject *object);

static void gst_sidplayfp_dec_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec);
static void gst_sidplayfp_dec_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);

static GstClockTime gst_sidplayfp_dec_tell(GstNonstreamAudioDecoder *dec);

static gboolean gst_sidplayfp_dec_load_from_buffer(GstNonstreamAudioDecoder *dec, GstBuffer *source_data, guint initial_subsong, GstNonstreamAudioSubsongMode initial_subsong_mode, GstClockTime *initial_position, GstNonstreamAudioOutputMode *initial_output_mode, gint *initial_num_loops);

static GstTagList* gst_sidplayfp_dec_get_main_tags(GstNonstreamAudioDecoder *dec);

static gboolean gst_sidplayfp_dec_set_current_subsong(GstNonstreamAudioDecoder *dec, guint subsong, GstClockTime *initial_position);
static guint gst_sidplayfp_dec_get_current_subsong(GstNonstreamAudioDecoder *dec);

static guint gst_sidplayfp_dec_get_num_subsongs(GstNonstreamAudioDecoder *dec);
static int_least32_t gst_sidplayfp_dec_get_subsong_duration_internal(GstSidplayfpDec *sidplayfp_dec, guint subsong);
static GstClockTime gst_sidplayfp_dec_get_subsong_duration(GstNonstreamAudioDecoder *dec, guint subsong);

static gboolean gst_sidplayfp_dec_set_num_loops(GstNonstreamAudioDecoder *dec, gint num_loops);
static gint gst_sidplayfp_dec_get_num_loops(GstNonstreamAudioDecoder *dec);

static guint gst_sidplayfp_dec_get_supported_output_modes(GstNonstreamAudioDecoder *dec);
static gboolean gst_sidplayfp_dec_decode(GstNonstreamAudioDecoder *dec, GstBuffer **buffer, guint *num_samples);

static const gchar * gst_sidplayfp_dec_get_rom_name(GstSidplayfpDecRomIndex index);
static unsigned int gst_sidplayfp_dec_to_sid_subsong_nr(SidTune *tune, guint subsong);

static GType gst_sidplayfp_dec_c64_model_get_type(void);
static GType gst_sidplayfp_dec_sid_model_get_type(void);
static GType gst_sidplayfp_dec_sampling_method_get_type(void);


static inline const gchar * yesno_str(gboolean b)
{
	return b ? "yes" : "no";
}

// TODO: reset playback position when switching subsong during playback
// TODO: is seeking somehow possible?



void gst_sidplayfp_dec_class_init(GstSidplayfpDecClass *klass)
{
	GObjectClass *object_class;
	GstElementClass *element_class;
	GstNonstreamAudioDecoderClass *dec_class;

	GST_DEBUG_CATEGORY_INIT(sidplayfpdec_debug, "sidplayfpdec", 0, "libsidplayfp-based SID music decoder");

	object_class = G_OBJECT_CLASS(klass);
	element_class = GST_ELEMENT_CLASS(klass);
	dec_class = GST_NONSTREAM_AUDIO_DECODER_CLASS(klass);

	gst_element_class_add_pad_template(element_class, gst_static_pad_template_get(&sink_template));
	gst_element_class_add_pad_template(element_class, gst_static_pad_template_get(&src_template));

	object_class->finalize = GST_DEBUG_FUNCPTR(gst_sidplayfp_dec_finalize);
	object_class->set_property = GST_DEBUG_FUNCPTR(gst_sidplayfp_dec_set_property);
	object_class->get_property = GST_DEBUG_FUNCPTR(gst_sidplayfp_dec_get_property);

	dec_class->tell                       = GST_DEBUG_FUNCPTR(gst_sidplayfp_dec_tell);
	dec_class->load_from_buffer           = GST_DEBUG_FUNCPTR(gst_sidplayfp_dec_load_from_buffer);
	dec_class->get_main_tags              = GST_DEBUG_FUNCPTR(gst_sidplayfp_dec_get_main_tags);
	dec_class->set_current_subsong        = GST_DEBUG_FUNCPTR(gst_sidplayfp_dec_set_current_subsong);
	dec_class->get_current_subsong        = GST_DEBUG_FUNCPTR(gst_sidplayfp_dec_get_current_subsong);
	dec_class->set_num_loops              = GST_DEBUG_FUNCPTR(gst_sidplayfp_dec_set_num_loops);
	dec_class->get_num_loops              = GST_DEBUG_FUNCPTR(gst_sidplayfp_dec_get_num_loops);
	dec_class->get_num_subsongs           = GST_DEBUG_FUNCPTR(gst_sidplayfp_dec_get_num_subsongs);
	dec_class->get_subsong_duration       = GST_DEBUG_FUNCPTR(gst_sidplayfp_dec_get_subsong_duration);
	dec_class->get_supported_output_modes = GST_DEBUG_FUNCPTR(gst_sidplayfp_dec_get_supported_output_modes);
	dec_class->decode                     = GST_DEBUG_FUNCPTR(gst_sidplayfp_dec_decode);

	gst_element_class_set_static_metadata(
		element_class,
		"libsidplayfp-based SID music decoder",
		"Codec/Decoder/Audio",
		"Decodes C64 SID music files using libsidplayfp",
		"Carlos Rafael Giani <dv@pseudoterminal.org>"
	);

	g_object_class_install_property(
		object_class,
		PROP_KERNAL_ROM,
		g_param_spec_boxed(
			"kernal-rom",
			"Kernal ROM",
			"Kernal ROM image, needed for some tunes",
			GST_TYPE_BUFFER,
			GParamFlags(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)
		)
	);
	g_object_class_install_property(
		object_class,
		PROP_BASIC_ROM,
		g_param_spec_boxed(
			"basic-rom",
			"Basic ROM",
			"Basic ROM image, needed for tunes with C64BASIC executable portions",
			GST_TYPE_BUFFER,
			GParamFlags(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)
		)
	);
	g_object_class_install_property(
		object_class,
		PROP_CHARACTER_GEN_ROM,
		g_param_spec_boxed(
			"character-gen-rom",
			"Character generator ROM",
			"Character generator ROM image",
			GST_TYPE_BUFFER,
			GParamFlags(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)
		)
	);
	g_object_class_install_property(
		object_class,
		PROP_DEFAULT_C64_MODEL,
		g_param_spec_enum(
			"default-c64-model",
			"Default C64 model",
			"Default C64 model to use when it is not defined by the song (or if force-c64-model is enabled)",
			gst_sidplayfp_dec_c64_model_get_type(),
			DEFAULT_DEFAULT_C64_MODEL,
			GParamFlags(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)
		)
	);
	g_object_class_install_property(
		object_class,
		PROP_FORCE_C64_MODEL,
		g_param_spec_boolean(
			"force-c64-model",
			"Force C64 model",
			"Force the use of the default C64 model, overriding the song's definition",
			DEFAULT_FORCE_C64_MODEL,
			GParamFlags(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)
		)
	);
	g_object_class_install_property(
		object_class,
		PROP_DEFAULT_SID_MODEL,
		g_param_spec_enum(
			"default-sid-model",
			"Default SID model",
			"Default SID model to use when it is not defined by the song (or if force-sid-model is enabled)",
			gst_sidplayfp_dec_sid_model_get_type(),
			DEFAULT_DEFAULT_SID_MODEL,
			GParamFlags(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)
		)
	);
	g_object_class_install_property(
		object_class,
		PROP_FORCE_SID_MODEL,
		g_param_spec_boolean(
			"force-sid-model",
			"Force SID model",
			"Force the use of the default SID model, overriding the song's definition",
			DEFAULT_FORCE_SID_MODEL,
			GParamFlags(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)
		)
	);
	g_object_class_install_property(
		object_class,
		PROP_SAMPLING_METHOD,
		g_param_spec_enum(
			"sampling-method",
			"Sampling method",
			"Sampling method",
			gst_sidplayfp_dec_sampling_method_get_type(),
			DEFAULT_SAMPLING_METHOD,
			GParamFlags(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)
		)
	);
	g_object_class_install_property(
		object_class,
		PROP_FALLBACK_SONG_LENGTH,
		g_param_spec_uint(
			"fallback-song-length",
			"Fallback song length",
			"Songlength to use if HSVD song length database is unavailable or does not contain song length, in seconds",
			1, G_MAXUINT,
			DEFAULT_FALLBACK_SONG_LENGTH,
			GParamFlags(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)
		)
	);
	g_object_class_install_property(
		object_class,
		PROP_HSVC_SONGLENGTH_DB_PATH,
		g_param_spec_string(
			"hsvc-songlength-db-path",
			"HSVC song length database path",
			"Full path to HSVD song length database (incl. filename); if NULL, no song length database is used",
			DEFAULT_HSVC_SONGLENGTH_DB_PATH,
			GParamFlags(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)
		)
	);
	g_object_class_install_property(
		object_class,
		PROP_OUTPUT_BUFFER_SIZE,
		g_param_spec_uint(
			"output-buffer-size",
			"Output buffer size",
			"Size of each output buffer, in samples (actual size can be smaller than this during flush or EOS)",
			1, G_MAXUINT / (2 * 2), /* 2*2 => stereo output with S16 samples; this ensures that no overflow can happen */
			DEFAULT_OUTPUT_BUFFER_SIZE,
			GParamFlags(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)
		)
	);
}


void gst_sidplayfp_dec_init(GstSidplayfpDec *sidplayfp_dec)
{
	new (&(sidplayfp_dec->engine)) sidplayfp();
	new (&(sidplayfp_dec->builder)) ReSIDfpBuilder("gstsidplayfp-builder");
	sidplayfp_dec->tune = NULL;

	memset(sidplayfp_dec->rom_images, 0, sizeof(sidplayfp_dec->rom_images));

	sidplayfp_dec->default_c64_model = DEFAULT_DEFAULT_C64_MODEL;
	sidplayfp_dec->force_c64_model = DEFAULT_FORCE_C64_MODEL;
	sidplayfp_dec->default_sid_model = DEFAULT_DEFAULT_SID_MODEL;
	sidplayfp_dec->force_sid_model = DEFAULT_FORCE_SID_MODEL;
	sidplayfp_dec->sampling_method = DEFAULT_SAMPLING_METHOD;

	sidplayfp_dec->fallback_song_length = DEFAULT_FALLBACK_SONG_LENGTH;
	sidplayfp_dec->hsvc_songlength_db_path = g_strdup(DEFAULT_HSVC_SONGLENGTH_DB_PATH);
	// TODO: set path to $PREFIX/share/sidplayfp/Songlengths.txt
	new (&(sidplayfp_dec->database)) SidDatabase();
	sidplayfp_dec->subsong_lengths = NULL;

	sidplayfp_dec->sample_rate = DEFAULT_SAMPLE_RATE;
	sidplayfp_dec->num_channels = DEFAULT_NUM_CHANNELS;

	sidplayfp_dec->output_buffer_size = DEFAULT_OUTPUT_BUFFER_SIZE;

	sidplayfp_dec->main_tags = NULL;
}


static void gst_sidplayfp_dec_finalize(GObject *object)
{
	int i;
	GstSidplayfpDec *sidplayfp_dec = GST_SIDPLAYFP_DEC(object);

	for (i = 0; i < 3; ++i)
	{
		if (sidplayfp_dec->rom_images[i] != NULL)
			gst_buffer_unref(sidplayfp_dec->rom_images[i]);
	}

	if (sidplayfp_dec->main_tags != NULL)
		gst_tag_list_unref(sidplayfp_dec->main_tags);

	sidplayfp_dec->database.~SidDatabase();
	g_free(sidplayfp_dec->subsong_lengths);

	if (sidplayfp_dec->tune != NULL)
		delete sidplayfp_dec->tune;

	sidplayfp_dec->builder.~ReSIDfpBuilder();
	sidplayfp_dec->engine.~sidplayfp();

	g_free(sidplayfp_dec->hsvc_songlength_db_path);

	G_OBJECT_CLASS(gst_sidplayfp_dec_parent_class)->finalize(object);
}


static void set_rom_image_gstproperty(GstSidplayfpDec *sidplayfp_dec, const GValue *value, GstSidplayfpDecRomIndex index)
{
	GstBuffer *new_rom_image = (GstBuffer *)g_value_get_boxed(value);

	GST_NONSTREAM_AUDIO_DECODER_LOCK_MUTEX(sidplayfp_dec);
	gst_buffer_replace(&(sidplayfp_dec->rom_images[index]), new_rom_image);
	GST_NONSTREAM_AUDIO_DECODER_UNLOCK_MUTEX(sidplayfp_dec);
}


static void get_rom_image_gstproperty(GstSidplayfpDec *sidplayfp_dec, GValue *value, GstSidplayfpDecRomIndex index)
{
	GST_NONSTREAM_AUDIO_DECODER_LOCK_MUTEX(sidplayfp_dec);

	if (sidplayfp_dec->rom_images[index] != NULL)
		gst_buffer_ref(sidplayfp_dec->rom_images[index]);
	g_value_set_boxed(value, sidplayfp_dec->rom_images[index]);

	GST_NONSTREAM_AUDIO_DECODER_UNLOCK_MUTEX(sidplayfp_dec);
}


static void gst_sidplayfp_dec_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	GstNonstreamAudioDecoder *dec;
	GstSidplayfpDec *sidplayfp_dec;

	dec = GST_NONSTREAM_AUDIO_DECODER(object);
	sidplayfp_dec = GST_SIDPLAYFP_DEC(object);

	switch (prop_id)
	{
		case PROP_KERNAL_ROM:
			set_rom_image_gstproperty(sidplayfp_dec, value, GST_SIDPLAYFP_DEC_KERNAL_ROM);
			break;

		case PROP_BASIC_ROM:
			set_rom_image_gstproperty(sidplayfp_dec, value, GST_SIDPLAYFP_DEC_BASIC_ROM);
			break;

		case PROP_CHARACTER_GEN_ROM:
			set_rom_image_gstproperty(sidplayfp_dec, value, GST_SIDPLAYFP_DEC_CHARACTER_GEN_ROM);
			break;

		case PROP_DEFAULT_C64_MODEL:
			GST_NONSTREAM_AUDIO_DECODER_LOCK_MUTEX(dec);
			sidplayfp_dec->default_c64_model = SidConfig::c64_model_t(g_value_get_enum(value));
			GST_NONSTREAM_AUDIO_DECODER_UNLOCK_MUTEX(dec);
			break;

		case PROP_FORCE_C64_MODEL:
			GST_NONSTREAM_AUDIO_DECODER_LOCK_MUTEX(dec);
			sidplayfp_dec->force_c64_model = g_value_get_boolean(value);
			GST_NONSTREAM_AUDIO_DECODER_UNLOCK_MUTEX(dec);
			break;

		case PROP_DEFAULT_SID_MODEL:
			GST_NONSTREAM_AUDIO_DECODER_LOCK_MUTEX(dec);
			sidplayfp_dec->default_sid_model = SidConfig::sid_model_t(g_value_get_enum(value));
			GST_NONSTREAM_AUDIO_DECODER_UNLOCK_MUTEX(dec);
			break;

		case PROP_FORCE_SID_MODEL:
			GST_NONSTREAM_AUDIO_DECODER_LOCK_MUTEX(dec);
			sidplayfp_dec->force_sid_model = g_value_get_boolean(value);
			GST_NONSTREAM_AUDIO_DECODER_UNLOCK_MUTEX(dec);
			break;

		case PROP_SAMPLING_METHOD:
			GST_NONSTREAM_AUDIO_DECODER_LOCK_MUTEX(dec);
			sidplayfp_dec->sampling_method = SidConfig::sampling_method_t(g_value_get_enum(value));
			GST_NONSTREAM_AUDIO_DECODER_UNLOCK_MUTEX(dec);
			break;

		case PROP_OUTPUT_BUFFER_SIZE:
			GST_NONSTREAM_AUDIO_DECODER_LOCK_MUTEX(dec);
			sidplayfp_dec->output_buffer_size = g_value_get_uint(value);
			GST_NONSTREAM_AUDIO_DECODER_UNLOCK_MUTEX(dec);
			break;

		case PROP_FALLBACK_SONG_LENGTH:
			GST_NONSTREAM_AUDIO_DECODER_LOCK_MUTEX(dec);
			sidplayfp_dec->fallback_song_length = g_value_get_uint(value);
			GST_NONSTREAM_AUDIO_DECODER_UNLOCK_MUTEX(dec);
			break;

		case PROP_HSVC_SONGLENGTH_DB_PATH:
			GST_NONSTREAM_AUDIO_DECODER_LOCK_MUTEX(dec);
			g_free(sidplayfp_dec->hsvc_songlength_db_path);
			sidplayfp_dec->hsvc_songlength_db_path = g_value_dup_string(value);
			GST_NONSTREAM_AUDIO_DECODER_UNLOCK_MUTEX(dec);
			break;

		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
			break;
	}
}


static void gst_sidplayfp_dec_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	GstSidplayfpDec *sidplayfp_dec = GST_SIDPLAYFP_DEC(object);

	switch (prop_id)
	{
		case PROP_KERNAL_ROM:
			get_rom_image_gstproperty(sidplayfp_dec, value, GST_SIDPLAYFP_DEC_KERNAL_ROM);
			break;

		case PROP_BASIC_ROM:
			get_rom_image_gstproperty(sidplayfp_dec, value, GST_SIDPLAYFP_DEC_BASIC_ROM);
			break;

		case PROP_CHARACTER_GEN_ROM:
			get_rom_image_gstproperty(sidplayfp_dec, value, GST_SIDPLAYFP_DEC_CHARACTER_GEN_ROM);
			break;

		case PROP_DEFAULT_C64_MODEL:
			GST_NONSTREAM_AUDIO_DECODER_LOCK_MUTEX(object);
			g_value_set_enum(value, sidplayfp_dec->default_c64_model);
			GST_NONSTREAM_AUDIO_DECODER_UNLOCK_MUTEX(object);
			break;

		case PROP_FORCE_C64_MODEL:
			GST_NONSTREAM_AUDIO_DECODER_LOCK_MUTEX(object);
			g_value_set_boolean(value, sidplayfp_dec->force_c64_model);
			GST_NONSTREAM_AUDIO_DECODER_UNLOCK_MUTEX(object);
			break;

		case PROP_DEFAULT_SID_MODEL:
			GST_NONSTREAM_AUDIO_DECODER_LOCK_MUTEX(object);
			g_value_set_enum(value, sidplayfp_dec->default_sid_model);
			GST_NONSTREAM_AUDIO_DECODER_UNLOCK_MUTEX(object);
			break;

		case PROP_FORCE_SID_MODEL:
			GST_NONSTREAM_AUDIO_DECODER_LOCK_MUTEX(object);
			g_value_set_boolean(value, sidplayfp_dec->force_sid_model);
			GST_NONSTREAM_AUDIO_DECODER_UNLOCK_MUTEX(object);
			break;

		case PROP_SAMPLING_METHOD:
			GST_NONSTREAM_AUDIO_DECODER_LOCK_MUTEX(object);
			g_value_set_enum(value, sidplayfp_dec->sampling_method);
			GST_NONSTREAM_AUDIO_DECODER_UNLOCK_MUTEX(object);
			break;

		case PROP_OUTPUT_BUFFER_SIZE:
			GST_NONSTREAM_AUDIO_DECODER_LOCK_MUTEX(object);
			g_value_set_uint(value, sidplayfp_dec->output_buffer_size);
			GST_NONSTREAM_AUDIO_DECODER_UNLOCK_MUTEX(object);
			break;

		case PROP_FALLBACK_SONG_LENGTH:
			GST_NONSTREAM_AUDIO_DECODER_LOCK_MUTEX(object);
			g_value_set_uint(value, sidplayfp_dec->fallback_song_length);
			GST_NONSTREAM_AUDIO_DECODER_UNLOCK_MUTEX(object);
			break;

		case PROP_HSVC_SONGLENGTH_DB_PATH:
			GST_NONSTREAM_AUDIO_DECODER_LOCK_MUTEX(object);
			g_value_set_string(value, sidplayfp_dec->hsvc_songlength_db_path);
			GST_NONSTREAM_AUDIO_DECODER_UNLOCK_MUTEX(object);
			break;

		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
			break;
	}
}


static GstClockTime gst_sidplayfp_dec_tell(GstNonstreamAudioDecoder *dec)
{
	GstSidplayfpDec *sidplayfp_dec = GST_SIDPLAYFP_DEC(dec);
	return sidplayfp_dec->engine.time() * GST_SECOND;
}


static gboolean gst_sidplayfp_dec_load_from_buffer(GstNonstreamAudioDecoder *dec, GstBuffer *source_data, guint initial_subsong, G_GNUC_UNUSED GstNonstreamAudioSubsongMode initial_subsong_mode, GstClockTime *initial_position, GstNonstreamAudioOutputMode *initial_output_mode, gint *initial_num_loops)
{
	GstSidplayfpDec *sidplayfp_dec = GST_SIDPLAYFP_DEC(dec);
	guint max_num_sids;
	GstMapInfo buffer_map;
	unsigned int sid_subsong_nr;


	/* Determine the sample rate and channel count to use */
	sidplayfp_dec->sample_rate = DEFAULT_SAMPLE_RATE;
	sidplayfp_dec->num_channels = DEFAULT_NUM_CHANNELS;
	gst_nonstream_audio_decoder_get_downstream_info(dec, NULL, &(sidplayfp_dec->sample_rate), &(sidplayfp_dec->num_channels));


	/* Set output format */
	if (!gst_nonstream_audio_decoder_set_output_format_simple(
		dec,
		sidplayfp_dec->sample_rate,
		GST_AUDIO_FORMAT_S16,
		sidplayfp_dec->num_channels
	))
		return FALSE;


	/* Load the SID song length database if a path is set */
	if (sidplayfp_dec->hsvc_songlength_db_path != NULL)
	{
		GST_DEBUG_OBJECT(sidplayfp_dec, "Attempting to read HSVC songlength database from \"%s\"", sidplayfp_dec->hsvc_songlength_db_path);

		if (!(sidplayfp_dec->database.open(sidplayfp_dec->hsvc_songlength_db_path)))
		{
			GST_NONSTREAM_AUDIO_DECODER_UNLOCK_MUTEX(dec);
			GST_ELEMENT_ERROR(sidplayfp_dec, RESOURCE, OPEN_READ, ("Could not open HSVC song length database"), ("error message: %s", sidplayfp_dec->database.error()));
			GST_NONSTREAM_AUDIO_DECODER_LOCK_MUTEX(dec);

			return FALSE;
		}
	}


	/* Set ROMs */
	{
		GstMapInfo rom_maps[3];
		int i;

		memset(rom_maps, 0, sizeof(rom_maps));

		for (i = 0; i < 3; ++i)
		{
			gchar const *rom_name = gst_sidplayfp_dec_get_rom_name(GstSidplayfpDecRomIndex(i));

			if (sidplayfp_dec->rom_images[i] == NULL)
				continue;

			if (!gst_buffer_map(sidplayfp_dec->rom_images[i], &(rom_maps[i]), GST_MAP_READ))
			{
				int j;

				GST_ERROR_OBJECT(sidplayfp_dec, "Could not map %s ROM", rom_name);

				for (j = 0; j < i; ++j)
				{
					if (sidplayfp_dec->rom_images[j] != NULL)
						gst_buffer_unmap(sidplayfp_dec->rom_images[j], &(rom_maps[j]));
				}

				return FALSE;
			}

			GST_DEBUG_OBJECT(sidplayfp_dec, "Using %s ROM with %" G_GSIZE_FORMAT " bytes", rom_name, rom_maps[i].size);
		}

		GST_DEBUG_OBJECT(
			sidplayfp_dec,
			"ROMs in use:  KERNAL: %s  BASIC: %s  character generator: %s",
			yesno_str(rom_maps[GST_SIDPLAYFP_DEC_KERNAL_ROM].data != NULL),
			yesno_str(rom_maps[GST_SIDPLAYFP_DEC_BASIC_ROM].data != NULL),
			yesno_str(rom_maps[GST_SIDPLAYFP_DEC_CHARACTER_GEN_ROM].data != NULL)
		);

		sidplayfp_dec->engine.setRoms(
			rom_maps[GST_SIDPLAYFP_DEC_KERNAL_ROM].data,
			rom_maps[GST_SIDPLAYFP_DEC_BASIC_ROM].data,
			rom_maps[GST_SIDPLAYFP_DEC_CHARACTER_GEN_ROM].data
		);

		for (i = 0; i < 3; ++i)
		{
			if (sidplayfp_dec->rom_images[i] != NULL)
				gst_buffer_unmap(sidplayfp_dec->rom_images[i], &(rom_maps[i]));
		}
	}


	/* Create SIDs */
	max_num_sids = sidplayfp_dec->engine.info().maxsids();
	GST_DEBUG_OBJECT(sidplayfp_dec, "Max number of SIDs: %u", max_num_sids);
	sidplayfp_dec->builder.create(max_num_sids);
	if (!(sidplayfp_dec->builder.getStatus()))
	{
		GST_ERROR_OBJECT(sidplayfp_dec, "Could not create SIDs: %s", sidplayfp_dec->builder.error());
		return FALSE;
	}


	/* Configure engine */
	SidConfig cfg;
	cfg.defaultC64Model = sidplayfp_dec->default_c64_model;
	cfg.forceC64Model = sidplayfp_dec->force_c64_model;
	cfg.defaultSidModel = sidplayfp_dec->default_sid_model;
	cfg.forceSidModel = sidplayfp_dec->force_sid_model;
	cfg.playback = (sidplayfp_dec->num_channels == 1) ? SidConfig::MONO : SidConfig::STEREO;
	cfg.frequency = sidplayfp_dec->sample_rate;
	cfg.sidEmulation = &(sidplayfp_dec->builder);
	cfg.samplingMethod = sidplayfp_dec->sampling_method;
	cfg.fastSampling = false;

	if (!(sidplayfp_dec->engine.config(cfg)))
	{
		GST_ERROR_OBJECT(sidplayfp_dec, "Could not configure engine: %s", sidplayfp_dec->engine.error());
		return FALSE;
	}


	/* Load the SID file */
	gst_buffer_map(source_data, &buffer_map, GST_MAP_READ);
#ifdef USE_UNIQUE_PTR
	std::unique_ptr < SidTune > tune(new SidTune(buffer_map.data, buffer_map.size));
#else
	std::auto_ptr < SidTune > tune(new SidTune(buffer_map.data, buffer_map.size));
#endif
	gst_buffer_unmap(source_data, &buffer_map);

	sid_subsong_nr = gst_sidplayfp_dec_to_sid_subsong_nr(tune.get(), initial_subsong);
	sidplayfp_dec->current_subsong = initial_subsong;
	tune->selectSong(sid_subsong_nr);

	if (!(sidplayfp_dec->engine.load(tune.get())))
	{
		GST_ERROR_OBJECT(sidplayfp_dec, "Could not load SID tune: %s", sidplayfp_dec->engine.error());
		return FALSE;
	}


	/* Load HSVC database if available and retrieve subsong lengths from it */
	if (sidplayfp_dec->hsvc_songlength_db_path != NULL)
	{
		guint i;
		guint num_subsongs = tune->getInfo()->songs();

		if (num_subsongs > 0)
		{
			/* Create MD5 for retrieving subsong lengths from the database */
			tune->createMD5(sidplayfp_dec->md5);

			sidplayfp_dec->subsong_lengths = (int_least32_t *)g_malloc(sizeof(int_least32_t) * num_subsongs);

			for (i = 0; i < num_subsongs; ++i)
			{
				unsigned int sid_subsong_nr = gst_sidplayfp_dec_to_sid_subsong_nr(tune.get(), i);
				sidplayfp_dec->subsong_lengths[i] = sidplayfp_dec->database.length(sidplayfp_dec->md5, sid_subsong_nr);
				if (sidplayfp_dec->subsong_lengths[i] < 0)
					GST_ERROR_OBJECT(sidplayfp_dec, "Could not retrieve length from DB for subsong %u (%d): %s", i, gint(sid_subsong_nr), sidplayfp_dec->database.error());
				else
					GST_DEBUG_OBJECT(sidplayfp_dec, "Subsong %u (%u) / %u length: %d seconds", i, sid_subsong_nr, num_subsongs, gint(sidplayfp_dec->subsong_lengths[i]));
			}
		}
	}


	/* Get metadata and produce a tag list */
	{
		unsigned int num_info_strings = tune->getInfo()->numberOfInfoStrings();
		GST_DEBUG_OBJECT(sidplayfp_dec, "Number of info strings: %u", num_info_strings);

		if (num_info_strings >= 1)
		{
			GstTagList *tags = gst_tag_list_new_empty();
			gst_tag_list_add(tags, GST_TAG_MERGE_REPLACE, GST_TAG_TITLE, tune->getInfo()->infoString(0), NULL);

			if (num_info_strings >= 2)
				gst_tag_list_add(tags, GST_TAG_MERGE_REPLACE, GST_TAG_ARTIST, tune->getInfo()->infoString(1), NULL);

			// TODO: string #2 (release date)

			GST_DEBUG_OBJECT(sidplayfp_dec, "Produced tag list: %" GST_PTR_FORMAT, gpointer(tags));

			sidplayfp_dec->main_tags = tags;
		}
	}


	/* Miscellaneous */

	/* Seeking is not possible with sidplayfp */
	if (*initial_position != 0)
		*initial_position = 0;

	/* LOOPING output mode is not supported */
	*initial_output_mode = GST_NONSTREM_AUDIO_OUTPUT_MODE_STEADY;

	sidplayfp_dec->num_loops = *initial_num_loops;


	/* Loading succeeded - release the tune pointer and store it */

	sidplayfp_dec->tune = tune.release();

	return TRUE;
}


static GstTagList* gst_sidplayfp_dec_get_main_tags(GstNonstreamAudioDecoder *dec)
{
	GstSidplayfpDec *sidplayfp_dec = GST_SIDPLAYFP_DEC(dec);
	return gst_tag_list_ref(sidplayfp_dec->main_tags);
}


static gboolean gst_sidplayfp_dec_set_current_subsong(GstNonstreamAudioDecoder *dec, guint subsong, GstClockTime *)
{
	GstSidplayfpDec *sidplayfp_dec = GST_SIDPLAYFP_DEC(dec);
	unsigned int sid_subsong_nr = gst_sidplayfp_dec_to_sid_subsong_nr(sidplayfp_dec->tune, subsong);
	sidplayfp_dec->current_subsong = subsong;
	sidplayfp_dec->tune->selectSong(sid_subsong_nr);
	return TRUE;
}


static guint gst_sidplayfp_dec_get_current_subsong(GstNonstreamAudioDecoder *dec)
{
	GstSidplayfpDec *sidplayfp_dec = GST_SIDPLAYFP_DEC(dec);
	return sidplayfp_dec->current_subsong;
}


static guint gst_sidplayfp_dec_get_num_subsongs(GstNonstreamAudioDecoder *dec)
{
	GstSidplayfpDec *sidplayfp_dec = GST_SIDPLAYFP_DEC(dec);
	return sidplayfp_dec->tune->getInfo()->songs();
}


static int_least32_t gst_sidplayfp_dec_get_subsong_duration_internal(GstSidplayfpDec *sidplayfp_dec, guint subsong)
{
	if (sidplayfp_dec->subsong_lengths != NULL)
	{
		int_least32_t len = sidplayfp_dec->subsong_lengths[subsong];
		if (len == -1)
			return sidplayfp_dec->fallback_song_length;
		else
			return len;
	}
	else
	{
		return sidplayfp_dec->fallback_song_length;
	}
}


static GstClockTime gst_sidplayfp_dec_get_subsong_duration(GstNonstreamAudioDecoder *dec, guint subsong)
{
	GstSidplayfpDec *sidplayfp_dec = GST_SIDPLAYFP_DEC(dec);
	return gst_sidplayfp_dec_get_subsong_duration_internal(sidplayfp_dec, subsong) * GST_SECOND;
}


static gboolean gst_sidplayfp_dec_set_num_loops(GstNonstreamAudioDecoder *dec, gint num_loops)
{
	GstSidplayfpDec *sidplayfp_dec = GST_SIDPLAYFP_DEC(dec);
	sidplayfp_dec->num_loops = num_loops;
	return TRUE;
}


static gint gst_sidplayfp_dec_get_num_loops(GstNonstreamAudioDecoder *dec)
{
	GstSidplayfpDec *sidplayfp_dec = GST_SIDPLAYFP_DEC(dec);
	return sidplayfp_dec->num_loops;
}


static guint gst_sidplayfp_dec_get_supported_output_modes(G_GNUC_UNUSED GstNonstreamAudioDecoder *)
{
	return 1u << GST_NONSTREM_AUDIO_OUTPUT_MODE_STEADY;
}


static gboolean gst_sidplayfp_dec_decode(GstNonstreamAudioDecoder *dec, GstBuffer **buffer, guint *num_samples)
{
	GstSidplayfpDec *sidplayfp_dec = GST_SIDPLAYFP_DEC(dec);
	GstMapInfo map;
	GstBuffer *outbuf;
	gsize outbuf_size;
	uint_least32_t max_num_produced_samples;
	uint_least32_t num_produced_samples;

	/* Check if playback reached its end. sidplayfp does not stop on its own;
	 * it just loops endlessly. So, manually check if the elapsed time has
	 * reached the configured limit. If so, exit.
	 *
	 * The configured limit is defined by (num_loops+1) * subsong_length .
	 * (num_loops+1), because num_loops=0 means "play and do not repeat",
	 * num_loops=1 means "play and repeat once" etc.
	 *
	 * And if num_loops < 0, then just do nothing, and let sidplayfp loop
	 * endlessly.
	 */
	if ((sidplayfp_dec->num_loops >= 0))
	{
		gint64 cur_time = sidplayfp_dec->engine.time();
		gint64 length = gst_sidplayfp_dec_get_subsong_duration_internal(sidplayfp_dec, sidplayfp_dec->current_subsong);

		if (cur_time >= ((sidplayfp_dec->num_loops + 1) * length))
			return FALSE;
	}

	max_num_produced_samples = sidplayfp_dec->output_buffer_size * sidplayfp_dec->num_channels;

	/* Allocate output buffer */
	outbuf_size = max_num_produced_samples * 2;
	outbuf = gst_nonstream_audio_decoder_allocate_output_buffer(dec, outbuf_size);
	if (G_UNLIKELY(outbuf == NULL))
		return FALSE;

	/* The actual decoding */
	gst_buffer_map(outbuf, &map, GST_MAP_WRITE);
	num_produced_samples = sidplayfp_dec->engine.play(reinterpret_cast < short* > (map.data), max_num_produced_samples);
	gst_buffer_unmap(outbuf, &map);

	*buffer = outbuf;
	*num_samples = num_produced_samples / sidplayfp_dec->num_channels;

	return TRUE;
}


static const gchar * gst_sidplayfp_dec_get_rom_name(GstSidplayfpDecRomIndex index)
{
	switch (index)
	{
		case GST_SIDPLAYFP_DEC_KERNAL_ROM: return "KERNAL";
		case GST_SIDPLAYFP_DEC_BASIC_ROM: return "BASIC"; 
		case GST_SIDPLAYFP_DEC_CHARACTER_GEN_ROM: return "character generator";
		default: g_assert_not_reached();
	}
}


static unsigned int gst_sidplayfp_dec_to_sid_subsong_nr(SidTune *tune, guint subsong)
{
	SidTuneInfo const *tune_info = tune->getInfo();

	unsigned int num_subsongs = tune_info->songs();
	if (num_subsongs == 0)
		return tune_info->startSong();

	/* sidplayfp subsongs start with index 1, and the default first
	 * subsong's index is given by startSong(). GstNonstreamAudioDecoder
	 * subsong indices however always start at 0. */
	return ((tune_info->startSong() - 1 + subsong) % num_subsongs) + 1;
}


static GType gst_sidplayfp_dec_c64_model_get_type(void)
{
	static volatile GType type = 0;

	if (g_once_init_enter(&type))
	{
		GType _type;

		static GEnumValue predef_values[] =
		{
			{ SidConfig::PAL, "PAL", "pal" },
			{ SidConfig::NTSC, "NTSC", "ntsc" },
			{ 0, NULL, NULL },
		};

		_type = g_enum_register_static(
			"SidplayfpC64Model",
			predef_values
		);

		g_once_init_leave(&type, _type);
	}

	return type;
}


static GType gst_sidplayfp_dec_sid_model_get_type(void)
{
	static volatile GType type = 0;

	if (g_once_init_enter(&type))
	{
		GType _type;

		static GEnumValue predef_values[] =
		{
			{ SidConfig::MOS6581, "Original SID 6581", "sid6581" },
			{ SidConfig::MOS8580, "Newer SID 8580", "sid8580" },
			{ 0, NULL, NULL },
		};

		_type = g_enum_register_static(
			"SidplayfpSIDModel",
			predef_values
		);

		g_once_init_leave(&type, _type);
	}

	return type;
}


static GType gst_sidplayfp_dec_sampling_method_get_type(void)
{
	static volatile GType type = 0;

	if (g_once_init_enter(&type))
	{
		GType _type;

		static GEnumValue predef_values[] =
		{
			{ SidConfig::INTERPOLATE, "Interpolate", "interpolate" },
			{ SidConfig::RESAMPLE_INTERPOLATE, "Resample and interpolate", "resample-interpolate" },
			{ 0, NULL, NULL },
		};

		_type = g_enum_register_static(
			"SidplayfpSamplingMethod",
			predef_values
		);

		g_once_init_leave(&type, _type);
	}

	return type;
}
