#include <stdlib.h>
#include <string.h>
#include <config.h>

#include <gst/gst.h>

#include "gstdumbdec.h"

#include "dumb-kode54-git/dumb/include/internal/it.h"


GST_DEBUG_CATEGORY_STATIC(dumbdec_debug);
#define GST_CAT_DEFAULT dumbdec_debug


enum
{
	PROP_0,
	PROP_RESAMPLING_QUALITY,
	PROP_RAMP_STYLE
};



/* ramp styles (taken from foo_dumb mod.cpp) */
#define DUMB_RAMP_STYLE_NONE 0
#define DUMB_RAMP_STYLE_LOGARITHMIC 1
#define DUMB_RAMP_STYLE_LINEAR 2
#define DUMB_RAMP_STYLE_XM_LIN_ELSE_NONE 3
#define DUMB_RAMP_STYLE_XM_LIN_ELSE_LOG 4

/* property defaults */
#define DEFAULT_RESAMPLING_QUALITY DUMB_RQ_CUBIC
#define DEFAULT_RAMP_STYLE DUMB_RAMP_STYLE_NONE

/* caps negotiation defaults */
#define DEFAULT_SAMPLE_RATE 48000
#define DEFAULT_NUM_CHANNELS 2

#define RENDER_BIT_DEPTH 16
#define AUDIO_FORMAT GST_AUDIO_FORMAT_S16



static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE(
	"sink",
	GST_PAD_SINK,
	GST_PAD_ALWAYS,
	GST_STATIC_CAPS(
		"audio/x-mod, "
		"type = (string) { mod, s3m, stm, xm, it, ptm, psm, mtm, 669, dsm, asylum-amf, dsmi-amf, okt }"
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
		"channels = (int) [ 1, 2 ] "
	)
);



G_DEFINE_TYPE(GstDumbDec, gst_dumb_dec, GST_TYPE_NONSTREAM_AUDIO_DECODER)



static GType gst_dumb_dec_resampling_quality_get_type(void);
#define GST_TYPE_DUMB_DEC_RESAMPLING_QUALITY (gst_dumb_dec_resampling_quality_get_type())

static GType gst_dumb_dec_ramp_style_get_type(void);
#define GST_TYPE_DUMB_DEC_RAMP_STYLE (gst_dumb_dec_ramp_style_get_type())

static void gst_dumb_dec_finalize(GObject *object);

static void gst_dumb_dec_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec);
static void gst_dumb_dec_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);

static gboolean gst_dumb_dec_seek(GstNonstreamAudioDecoder *dec, GstClockTime new_position);
static GstClockTime gst_dumb_dec_tell(GstNonstreamAudioDecoder *dec);

static gboolean gst_dumb_dec_load(GstNonstreamAudioDecoder *dec, GstBuffer *source_data, guint initial_subsong, GstClockTime *initial_position, GstNonstreamAudioOutputMode *initial_output_mode);

static gboolean gst_dumb_dec_set_current_subsong(GstNonstreamAudioDecoder *dec, guint subsong, GstClockTime *initial_position);
static guint gst_dumb_dec_get_current_subsong(GstNonstreamAudioDecoder *dec);
static guint gst_dumb_dec_get_num_subsongs(GstNonstreamAudioDecoder *dec);

static gboolean gst_dumb_dec_set_num_loops(GstNonstreamAudioDecoder *dec, gint num_loops);
static gint gst_dumb_dec_get_num_loops(GstNonstreamAudioDecoder *dec);

static guint gst_dumb_dec_get_supported_output_modes(GstNonstreamAudioDecoder *dec);
static gboolean gst_dumb_dec_set_output_mode(GstNonstreamAudioDecoder *dec, GstNonstreamAudioOutputMode mode, GstClockTime *current_position);

static gboolean gst_dumb_dec_decode(GstNonstreamAudioDecoder *dec, GstBuffer **buffer, guint *num_samples);

static gboolean gst_dumb_dec_init_sigrenderer_at_pos(GstDumbDec *dumb_dec, long seek_pos);
static gboolean gst_dumb_dec_init_sigrenderer_at_order(GstDumbDec *dumb_dec, int order);
static void gst_dumb_dec_init_sigrenderer_common(GstDumbDec *dumb_dec);

static void gst_dumb_scan_for_subsongs(GstDumbDec *dumb_dec);



void gst_dumb_dec_class_init(GstDumbDecClass *klass)
{
	GObjectClass *object_class;
	GstElementClass *element_class;
	GstNonstreamAudioDecoderClass *dec_class;

	GST_DEBUG_CATEGORY_INIT(dumbdec_debug, "dumbdec", 0, "DUMB module player");

	object_class = G_OBJECT_CLASS(klass);
	element_class = GST_ELEMENT_CLASS(klass);
	dec_class = GST_NONSTREAM_AUDIO_DECODER_CLASS(klass);

	gst_element_class_add_pad_template(element_class, gst_static_pad_template_get(&sink_template));
	gst_element_class_add_pad_template(element_class, gst_static_pad_template_get(&src_template));

	object_class->finalize = GST_DEBUG_FUNCPTR(gst_dumb_dec_finalize);
	object_class->set_property = GST_DEBUG_FUNCPTR(gst_dumb_dec_set_property);
	object_class->get_property = GST_DEBUG_FUNCPTR(gst_dumb_dec_get_property);

	dec_class->seek = GST_DEBUG_FUNCPTR(gst_dumb_dec_seek);
	dec_class->tell = GST_DEBUG_FUNCPTR(gst_dumb_dec_tell);
	dec_class->load = GST_DEBUG_FUNCPTR(gst_dumb_dec_load);
	dec_class->set_num_loops = GST_DEBUG_FUNCPTR(gst_dumb_dec_set_num_loops);
	dec_class->get_num_loops = GST_DEBUG_FUNCPTR(gst_dumb_dec_get_num_loops);
	dec_class->get_supported_output_modes = GST_DEBUG_FUNCPTR(gst_dumb_dec_get_supported_output_modes);
	dec_class->set_output_mode = GST_DEBUG_FUNCPTR(gst_dumb_dec_set_output_mode);
	dec_class->decode = GST_DEBUG_FUNCPTR(gst_dumb_dec_decode);
	dec_class->set_current_subsong = GST_DEBUG_FUNCPTR(gst_dumb_dec_set_current_subsong);
	dec_class->get_current_subsong = GST_DEBUG_FUNCPTR(gst_dumb_dec_get_current_subsong);
	dec_class->get_num_subsongs = GST_DEBUG_FUNCPTR(gst_dumb_dec_get_num_subsongs);

	g_object_class_install_property(
		object_class,
		PROP_RESAMPLING_QUALITY,
		g_param_spec_enum(
			"resampling-quality",
			"Resampling quality",
			"Quality to use for resampling module samples during playback",
			GST_TYPE_DUMB_DEC_RESAMPLING_QUALITY,
			DEFAULT_RESAMPLING_QUALITY,
			G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS
		)
	);
	g_object_class_install_property(
		object_class,
		PROP_RAMP_STYLE,
		g_param_spec_enum(
			"ramp-style",
			"Ramp style",
			"Volume ramp style to use for volume changes inside module playback",
			GST_TYPE_DUMB_DEC_RAMP_STYLE,
			DEFAULT_RAMP_STYLE,
			G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS
		)
	);

	gst_element_class_set_static_metadata(
		element_class,
		"DUMB module player",
		"Codec/Decoder/Audio",
		"Playes module files (MOD/S3M/XM/IT/MTM/...) using the DUMB (Dynamic Universal Music Bibliotheque) library",
		"Carlos Rafael Giani <dv@pseudoterminal.org>"
	);
}


void gst_dumb_dec_init(GstDumbDec *dumb_dec)
{
	dumb_dec->cur_loop_count = 0;
	dumb_dec->num_loops = 0;
	dumb_dec->loop_end_reached = FALSE;

	dumb_dec->duh = NULL;
	dumb_dec->duh_sigrenderer = NULL;

	dumb_dec->resampling_quality = DEFAULT_RESAMPLING_QUALITY;
	dumb_dec->ramp_style = DEFAULT_RAMP_STYLE;

	dumb_dec->subsongs = NULL;
	dumb_dec->cur_subsong = 0;
	dumb_dec->cur_subsong_info = NULL;
	dumb_dec->num_subsongs = 0;
	dumb_dec->subsongs_explicit = FALSE;
	dumb_dec->cur_subsong_start_pos = 0;
}


static GType gst_dumb_dec_resampling_quality_get_type(void)
{
	static GType gst_dumb_dec_resampling_quality_type = 0;

	if (!gst_dumb_dec_resampling_quality_type)
	{
		static GEnumValue resampling_quality_values[] =
		{
			{ DUMB_RQ_ALIASING,  "Aliasing (fastest; lowest quality)", "aliasing" },
			{ DUMB_RQ_LINEAR,    "Linear interpolation",               "linear"   },
			{ DUMB_RQ_CUBIC,     "Cubic interpolation",                "cubic"    },
			{ DUMB_RQ_FIR,       "FIR filter (slowest; best quality)", "fir"      },
			{ 0, NULL, NULL },
		};

		gst_dumb_dec_resampling_quality_type = g_enum_register_static(
			"DumbDecResamplingQuality",
			resampling_quality_values
		);
	}

	return gst_dumb_dec_resampling_quality_type;
}


static GType gst_dumb_dec_ramp_style_get_type(void)
{
	static GType gst_dumb_dec_ramp_style_type = 0;

	if (!gst_dumb_dec_ramp_style_type)
	{
		static GEnumValue ramp_style_values[] =
		{
			{ DUMB_RAMP_STYLE_NONE,             "No volume ramping",                                            "none"             },
			{ DUMB_RAMP_STYLE_LOGARITHMIC,      "Logarithmic volume ramping",                                   "logarithmic"      },
			{ DUMB_RAMP_STYLE_LINEAR,           "Linear volume ramping",                                        "linear"           },
			{ DUMB_RAMP_STYLE_XM_LIN_ELSE_NONE, "Linear volume ramping for XM modules, none for others",        "xm-lin-else-none" },
			{ DUMB_RAMP_STYLE_XM_LIN_ELSE_LOG,  "Linear volume ramping for XM modules, logarithmic for others", "xm-lin-else-log"  },
			{ 0, NULL, NULL },
		};

		gst_dumb_dec_ramp_style_type = g_enum_register_static(
			"DumbDecRampStyle",
			ramp_style_values
		);
	}

	return gst_dumb_dec_ramp_style_type;
}


static void gst_dumb_dec_finalize(GObject *object)
{
	GstDumbDec *dumb_dec;

	g_return_if_fail(GST_IS_DUMB_DEC(object));
	dumb_dec = GST_DUMB_DEC(object);

	if (dumb_dec->subsongs != NULL)
		g_array_free(dumb_dec->subsongs, TRUE);

	if (dumb_dec->duh_sigrenderer != NULL)
		duh_end_sigrenderer(dumb_dec->duh_sigrenderer);

	if (dumb_dec->duh != NULL)
		unload_duh(dumb_dec->duh);

	G_OBJECT_CLASS(gst_dumb_dec_parent_class)->finalize(object);
}


static void gst_dumb_dec_set_property(GObject *object, guint prop_id, const GValue *value, G_GNUC_UNUSED GParamSpec *pspec)
{
	GstNonstreamAudioDecoder *dec;
	GstDumbDec *dumb_dec;

	dec = GST_NONSTREAM_AUDIO_DECODER(object);
	dumb_dec = GST_DUMB_DEC(object);

	switch (prop_id)
	{
		case PROP_RESAMPLING_QUALITY:
		{
			DUMB_IT_SIGRENDERER *itsr;

			GST_NONSTREAM_AUDIO_DECODER_STREAM_LOCK(dec);
			dumb_dec->resampling_quality = g_value_get_enum(value);
			if (dumb_dec->duh_sigrenderer != NULL)
			{
				itsr = duh_get_it_sigrenderer(dumb_dec->duh_sigrenderer);
				dumb_it_set_resampling_quality(itsr, dumb_dec->resampling_quality);
			}
			GST_NONSTREAM_AUDIO_DECODER_STREAM_UNLOCK(dec);

			break;
		}
		case PROP_RAMP_STYLE:
		{
			DUMB_IT_SIGRENDERER *itsr;

			GST_NONSTREAM_AUDIO_DECODER_STREAM_LOCK(dec);
			dumb_dec->ramp_style = g_value_get_enum(value);
			if (dumb_dec->duh_sigrenderer != NULL)
			{
				itsr = duh_get_it_sigrenderer(dumb_dec->duh_sigrenderer);
				dumb_it_set_ramp_style(itsr, dumb_dec->ramp_style);
			}
			GST_NONSTREAM_AUDIO_DECODER_STREAM_UNLOCK(dec);

			break;
		}
		default:
			break;
	}
}


static void gst_dumb_dec_get_property(GObject *object, guint prop_id, GValue *value, G_GNUC_UNUSED GParamSpec *pspec)
{
	GstDumbDec *dumb_dec = GST_DUMB_DEC(object);

	switch (prop_id)
	{
		case PROP_RESAMPLING_QUALITY:
			g_value_set_enum(value, dumb_dec->resampling_quality);
			break;
		case PROP_RAMP_STYLE:
			g_value_set_enum(value, dumb_dec->ramp_style);
			break;
		default:
			break;
	}
}


static gboolean gst_dumb_dec_seek(GstNonstreamAudioDecoder *dec, GstClockTime new_position)
{
	GstClockTime pos;
	GstDumbDec *dumb_dec = GST_DUMB_DEC(dec);

	if (dumb_dec->duh == NULL)
	{
		GST_WARNING_OBJECT(dec, "ignoring seek request - module is not loaded");
		return FALSE;
	}

	pos = gst_util_uint64_scale_int(new_position, 65536, GST_SECOND) + dumb_dec->cur_subsong_start_pos;

	if (!gst_dumb_dec_init_sigrenderer_at_pos(GST_DUMB_DEC(dec), pos))
	{
		GST_ELEMENT_ERROR(dec, STREAM, DECODE, (NULL), ("cannot reinitialize DUMB decoding"));
		return FALSE;
	}

	return TRUE;
}


static GstClockTime gst_dumb_dec_tell(GstNonstreamAudioDecoder *dec)
{
	GstClockTime pos;
	GstDumbDec *dumb_dec = GST_DUMB_DEC(dec);

	if (dumb_dec->duh_sigrenderer == NULL)
		return 0;

	pos = duh_sigrenderer_get_position(dumb_dec->duh_sigrenderer) - dumb_dec->cur_subsong_start_pos;
	if (!dumb_dec->do_actual_looping)
		pos += dumb_dec->cur_subsong_info->length * dumb_dec->cur_loop_count;
	pos = gst_util_uint64_scale_int(pos, GST_SECOND, 65536);

	return pos;
}


static gboolean gst_dumb_dec_load(GstNonstreamAudioDecoder *dec, GstBuffer *source_data, guint initial_subsong, GstClockTime *initial_position, GstNonstreamAudioOutputMode *initial_output_mode)
{
	gboolean ret;
	GstDumbDec *dumb_dec = GST_DUMB_DEC(dec);

	dumb_dec->sample_rate = DEFAULT_SAMPLE_RATE;
	dumb_dec->num_channels = DEFAULT_NUM_CHANNELS;
	gst_nonstream_audio_decoder_get_downstream_info(dec, NULL, &(dumb_dec->sample_rate), &(dumb_dec->num_channels));

	{
		GstMapInfo map;
		DUMBFILE *dumbfile;

		gst_buffer_map(source_data, &map, GST_MAP_READ);

		{
			int subsong_idx, num_psm_subsongs;
			DUH *psm_duh;

			dumb_dec->subsongs = NULL;

			dumbfile = dumbfile_open_memory((char const *)(map.data), map.size);
			num_psm_subsongs = dumb_get_psm_subsong_count(dumbfile);
			dumbfile_close(dumbfile);

			if (num_psm_subsongs > 0)
			{
				GST_INFO_OBJECT(dec, "song data contains information about %d subsongs - reading", num_psm_subsongs);
				gst_dumb_dec_subsong_info *subsong_info;
				dumb_dec->subsongs = g_array_new(FALSE, FALSE, sizeof(gst_dumb_dec_subsong_info));
				g_array_set_size(dumb_dec->subsongs, num_psm_subsongs);
				subsong_info = (gst_dumb_dec_subsong_info *)(dumb_dec->subsongs->data);

				for (subsong_idx = 0; subsong_idx < num_psm_subsongs; ++subsong_idx)
				{
					dumbfile = dumbfile_open_memory((char const *)(map.data), map.size);
					psm_duh = dumb_read_any(dumbfile, 0/*restrict_*/, subsong_idx);
					if (psm_duh != NULL)
					{
						long len = dumb_it_build_checkpoints(duh_get_it_sigdata(psm_duh), 0);
						GST_DEBUG_OBJECT(dumb_dec, "subsong %d: length %d", subsong_idx, len);
						unload_duh(psm_duh);
						subsong_info[subsong_idx].start_order = 0;
						subsong_info[subsong_idx].length = len;
					}
					dumbfile_close(dumbfile);
				}

				dumb_dec->subsongs_explicit = TRUE;
			}
		}

		dumbfile = dumbfile_open_memory((char const *)(map.data), map.size);

		dumb_dec->duh = dumb_read_any(dumbfile, 0/*restrict_*/, dumb_dec->subsongs_explicit ? initial_subsong : (guint)0);

		dumbfile_close(dumbfile);
		gst_buffer_unmap(source_data, &map);

		if (dumb_dec->duh == NULL)
		{
			GST_ELEMENT_ERROR(dumb_dec, STREAM, DECODE, (NULL), ("DUMB failed to read module data"));
			return FALSE;
		}
	}

	*initial_position = 0;

	if (*initial_output_mode == GST_NONSTREM_AUDIO_OUTPUT_MODE_UNDEFINED)
		*initial_output_mode = GST_NONSTREM_AUDIO_OUTPUT_MODE_LOOPING;

	dumb_dec->do_actual_looping = ((*initial_output_mode) == GST_NONSTREM_AUDIO_OUTPUT_MODE_LOOPING);

	/* In case there is no dedicated subsong information inside the song data, scan the song for these
	   many modules contain isolated subsets that act as subsongs */
	if (dumb_dec->subsongs == NULL)
	{
		GST_INFO_OBJECT(dumb_dec, "song data does not contain subsong information - searching for subsongs by scanning");
		gst_dumb_scan_for_subsongs(dumb_dec);
		if (dumb_dec->subsongs == NULL)
			dumb_dec->subsongs = g_array_new(FALSE, FALSE, sizeof(gst_dumb_dec_subsong_info));
		GST_INFO_OBJECT(dumb_dec, "found %u subsongs by scanning", dumb_dec->subsongs->len);
	}

	if (dumb_dec->subsongs->len < 1)
	{
		gst_dumb_dec_subsong_info info;
	
		info.start_order = 0;
		info.length = duh_get_length(dumb_dec->duh);

		g_array_append_val(dumb_dec->subsongs, info);

		GST_INFO_OBJECT(dumb_dec, "no subsongs found - adding entire song as one subsong, start order 0, length %d", info.length);
	}

	dumb_dec->num_subsongs = dumb_dec->subsongs->len;

	if (initial_subsong >= dumb_dec->num_subsongs)
	{
		GST_WARNING_OBJECT(dumb_dec, "initial subsong %u out of bounds (there are %u subsongs) - setting it to 0", initial_subsong, dumb_dec->num_subsongs);
		initial_subsong = 0;
	}

	dumb_dec->cur_subsong = initial_subsong;
	dumb_dec->cur_subsong_info = &g_array_index(dumb_dec->subsongs, gst_dumb_dec_subsong_info, initial_subsong);
	dumb_dec->cur_subsong_start_pos = 0;

	if (dumb_dec->cur_subsong_info->start_order == 0)
	{
		ret = gst_dumb_dec_init_sigrenderer_at_pos(dumb_dec, 0);
		dumb_dec->cur_subsong_start_pos = 0;
	}
	else
	{
		ret = gst_dumb_dec_init_sigrenderer_at_order(dumb_dec, dumb_dec->cur_subsong_info->start_order);
		if (ret)
			dumb_dec->cur_subsong_start_pos = duh_sigrenderer_get_position(dumb_dec->duh_sigrenderer);
		else
			dumb_dec->cur_subsong_start_pos = 0;
	}

	if (!ret)
	{
		GST_ELEMENT_ERROR(dumb_dec, STREAM, DECODE, (NULL), ("cannot initialize DUMB decoding"));
		return FALSE;
	}

	/* Set output format */
	{
		GstAudioInfo audio_info;

		gst_audio_info_init(&audio_info);

		gst_audio_info_set_format(
			&audio_info,
			GST_AUDIO_FORMAT_S16,
			dumb_dec->sample_rate,
			dumb_dec->num_channels,
			NULL
		);

		if (!gst_nonstream_audio_decoder_set_output_audioinfo(dec, &audio_info))
			return FALSE;
	}

	gst_nonstream_audio_decoder_set_duration(dec, gst_util_uint64_scale_int(dumb_dec->cur_subsong_info->length, GST_SECOND, 65536));

	{
		char const *title, *message;
		GstTagList *tags;

		tags = gst_tag_list_new_empty();

		title = duh_get_tag(dumb_dec->duh, "TITLE");
		if (title != NULL)
			gst_tag_list_add(tags, GST_TAG_MERGE_APPEND, GST_TAG_TITLE, title, NULL);

		message = (char const*)(dumb_it_sd_get_song_message(duh_get_it_sigdata(dumb_dec->duh)));
		if (message != NULL)
			gst_tag_list_add(tags, GST_TAG_MERGE_APPEND, GST_TAG_COMMENT, message, NULL);

		gst_pad_push_event(GST_NONSTREAM_AUDIO_DECODER_SRC_PAD(dumb_dec), gst_event_new_tag(tags));
	}


	return TRUE;
}


static gboolean gst_dumb_dec_set_current_subsong(GstNonstreamAudioDecoder *dec, guint subsong, GstClockTime *initial_position)
{
	gst_dumb_dec_subsong_info *subsong_info;
	GstDumbDec *dumb_dec = GST_DUMB_DEC(dec);

	if (dumb_dec->duh == NULL)
	{
		GST_WARNING_OBJECT(dumb_dec, "could not set current subsong to %u - module not loaded", subsong);
		return FALSE;
	}

	subsong_info = &g_array_index(dumb_dec->subsongs, gst_dumb_dec_subsong_info, subsong);

	if (gst_dumb_dec_init_sigrenderer_at_order(dumb_dec, subsong_info->start_order))
	{
		long subsong_start_pos = dumb_dec->subsongs_explicit ? (long)0 : duh_sigrenderer_get_position(dumb_dec->duh_sigrenderer);

		*initial_position = 0;
		dumb_dec->cur_subsong = subsong;
		dumb_dec->cur_subsong_info = subsong_info;
		dumb_dec->cur_subsong_start_pos = subsong_start_pos;
		gst_nonstream_audio_decoder_set_duration(dec, gst_util_uint64_scale_int(subsong_info->length, GST_SECOND, 65536));
		return TRUE;
	}
	else
		return FALSE;
}


static guint gst_dumb_dec_get_current_subsong(GstNonstreamAudioDecoder *dec)
{
	GstDumbDec *dumb_dec = GST_DUMB_DEC(dec);
	return dumb_dec->cur_subsong;
}


static guint gst_dumb_dec_get_num_subsongs(GstNonstreamAudioDecoder *dec)
{
	GstDumbDec *dumb_dec = GST_DUMB_DEC(dec);
	return dumb_dec->num_subsongs;
}


static gboolean gst_dumb_dec_set_num_loops(GstNonstreamAudioDecoder *dec, gint num_loops)
{
	GstDumbDec *dumb_dec;

	dumb_dec = GST_DUMB_DEC(dec);

	if ((num_loops < 1) || (dumb_dec->cur_loop_count >= num_loops))
		dumb_dec->cur_loop_count = 0;
	dumb_dec->num_loops = num_loops;

	return TRUE;
}


static gint gst_dumb_dec_get_num_loops(GstNonstreamAudioDecoder *dec)
{
	GstDumbDec *dumb_dec;

	dumb_dec = GST_DUMB_DEC(dec);

	return dumb_dec->num_loops;
}


static guint gst_dumb_dec_get_supported_output_modes(G_GNUC_UNUSED GstNonstreamAudioDecoder *dec)
{
	return (1u << GST_NONSTREM_AUDIO_OUTPUT_MODE_LOOPING) | (1u << GST_NONSTREM_AUDIO_OUTPUT_MODE_STEADY);
}


static gboolean gst_dumb_dec_set_output_mode(GstNonstreamAudioDecoder *dec, GstNonstreamAudioOutputMode mode, GstClockTime *current_position)
{
	GstDumbDec *dumb_dec = GST_DUMB_DEC(dec);
	dumb_dec->do_actual_looping = (mode == GST_NONSTREM_AUDIO_OUTPUT_MODE_LOOPING);
	*current_position = gst_dumb_dec_tell(dec);
	return TRUE;
}


static gboolean gst_dumb_dec_decode(GstNonstreamAudioDecoder *dec, GstBuffer **buffer, guint *num_samples)
{
	GstDumbDec *dumb_dec;
	GstBuffer *outbuf;
	GstMapInfo map;
	gint num_samples_per_outbuf, num_bytes_per_outbuf, actual_num_samples_read;

	dumb_dec = GST_DUMB_DEC(dec);

	if (dumb_dec->loop_end_reached)
	{
		dumb_dec->loop_end_reached = FALSE;
		if (dumb_dec->do_actual_looping)
			gst_nonstream_audio_decoder_handle_loop(dec, gst_dumb_dec_tell(dec));
	}

	num_samples_per_outbuf = 1024;
	num_bytes_per_outbuf = num_samples_per_outbuf * dumb_dec->num_channels * RENDER_BIT_DEPTH / 8;

	outbuf = gst_nonstream_audio_decoder_allocate_output_buffer(dec, num_bytes_per_outbuf);
	if (outbuf == NULL)
		return FALSE;

	gst_buffer_map(outbuf, &map, GST_MAP_WRITE);
	actual_num_samples_read = duh_render(dumb_dec->duh_sigrenderer, RENDER_BIT_DEPTH, 0, 1.0f, 65536.0f / dumb_dec->sample_rate, num_samples_per_outbuf, map.data);
	gst_buffer_unmap(outbuf, &map);

	if (actual_num_samples_read == 0)
	{
		gst_buffer_unref(outbuf);
		GST_INFO_OBJECT(dumb_dec, "DUMB reached end of module");
		return FALSE;
	}
	else
	{
		if (actual_num_samples_read != num_samples_per_outbuf)
			gst_buffer_set_size(outbuf, actual_num_samples_read * dumb_dec->num_channels * RENDER_BIT_DEPTH / 8);

		*buffer = outbuf;
		*num_samples = actual_num_samples_read;

		return TRUE;
	}
}


static int gst_dumb_dec_loop_callback(void *ptr)
{
	gboolean continue_loop;
	GstDumbDec *dumb_dec;
	GstNonstreamAudioDecoder *dec;

	dumb_dec = (GstDumbDec*)(ptr);
	dec = GST_NONSTREAM_AUDIO_DECODER(dumb_dec);

	GST_DEBUG_OBJECT(dumb_dec, "DUMB reached loop callback");

	if (dumb_dec->num_loops < 0)
		continue_loop = TRUE;
	else if (dumb_dec->num_loops == 0)
		continue_loop = FALSE;
	else
	{
		if (dumb_dec->cur_loop_count >= dumb_dec->num_loops)
			continue_loop = FALSE;
		else
		{
			++dumb_dec->cur_loop_count;
			continue_loop = TRUE;
		}
	}

	if (continue_loop)
	{
		dumb_dec->loop_end_reached = TRUE;
	}

	GST_DEBUG_OBJECT(dec, "position reported by DUMB: %u loopcount: %u", duh_sigrenderer_get_position(dumb_dec->duh_sigrenderer), dumb_dec->cur_loop_count);

	return continue_loop ? 0 : 1;
}


static gboolean gst_dumb_dec_init_sigrenderer_at_pos(GstDumbDec *dumb_dec, long seek_pos)
{
	DUH_SIGRENDERER *new_sr;

	g_return_val_if_fail(dumb_dec->duh != NULL, FALSE);

	new_sr = duh_start_sigrenderer(
		dumb_dec->duh,
		0,
		dumb_dec->num_channels,
		seek_pos
	);
	if (new_sr == NULL)
		return FALSE;

	if (dumb_dec->duh_sigrenderer != NULL)
		duh_end_sigrenderer(dumb_dec->duh_sigrenderer);

	dumb_dec->duh_sigrenderer = new_sr;

	gst_dumb_dec_init_sigrenderer_common(dumb_dec);

	return TRUE;
}


static gboolean gst_dumb_dec_init_sigrenderer_at_order(GstDumbDec *dumb_dec, int order)
{
	DUH_SIGRENDERER *new_sr;

	g_return_val_if_fail(dumb_dec->duh != NULL, FALSE);

	new_sr = dumb_it_start_at_order(
		dumb_dec->duh,
		dumb_dec->num_channels,
		order
	);
	if (new_sr == NULL)
		return FALSE;

	if (dumb_dec->duh_sigrenderer != NULL)
		duh_end_sigrenderer(dumb_dec->duh_sigrenderer);

	dumb_dec->duh_sigrenderer = new_sr;

	gst_dumb_dec_init_sigrenderer_common(dumb_dec);

	return TRUE;
}


static void gst_dumb_dec_init_sigrenderer_common(GstDumbDec *dumb_dec)
{
	dumb_dec->cur_loop_count = 0;
	dumb_dec->loop_end_reached = FALSE;

	{
		DUMB_IT_SIGRENDERER *itsr = duh_get_it_sigrenderer(dumb_dec->duh_sigrenderer);

		dumb_it_set_resampling_quality(itsr, dumb_dec->resampling_quality);
		dumb_it_set_ramp_style(itsr, dumb_dec->ramp_style);

		dumb_it_set_loop_callback(itsr, &gst_dumb_dec_loop_callback, dumb_dec);
		dumb_it_set_xm_speed_zero_callback(itsr, &gst_dumb_dec_loop_callback, dumb_dec);
		dumb_it_set_global_volume_zero_callback(itsr, &gst_dumb_dec_loop_callback, dumb_dec);
	}
}


static gboolean dumb_it_test_for_speed_and_tempo( DUMB_IT_SIGDATA * itsd )
{
	unsigned char pattern_tested[ 256 ];
	memset( pattern_tested, 0, sizeof( pattern_tested ) );
	for ( unsigned i = 0, j = itsd->n_orders; i < j; i++ )
	{
		long pattern_number = itsd->order[ i ];
		if ( (pattern_number < itsd->n_patterns) && !pattern_tested[ pattern_number ] )
		{
			pattern_tested[ pattern_number ] = 1;
			IT_PATTERN * pat = &itsd->pattern[ pattern_number ];
			gboolean speed_found = FALSE, tempo_found = FALSE;
			for ( unsigned k = 0, l = pat->n_entries; k < l; k++ )
			{
				IT_ENTRY * entry = &pat->entry[ k ];
				if ( IT_IS_END_ROW( entry ) )
				{
					speed_found = FALSE;
					tempo_found = FALSE;
				}
				else if ( entry->mask & IT_ENTRY_EFFECT &&
					( entry->effect == IT_SET_SPEED || entry->effect == IT_SET_SONG_TEMPO ) )
				{
					if ( entry->effect == IT_SET_SPEED ) speed_found = TRUE;
					else tempo_found = TRUE;
					if ( speed_found && tempo_found ) return TRUE;
				}
			}
		}
	}
	return FALSE;
}


static void dumb_it_convert_tempos( DUMB_IT_SIGDATA * itsd, gboolean vsync )
{
	for ( unsigned i = 0, j = itsd->n_patterns; i < j; i++ )
	{
		IT_PATTERN * pat = &itsd->pattern[ i ];
		for ( unsigned k = 0, l = pat->n_entries; k < l; k++ )
		{
			IT_ENTRY * entry = &pat->entry[ k ];
			if ( entry->mask & IT_ENTRY_EFFECT )
			{
				if ( vsync && entry->effect == IT_SET_SONG_TEMPO ) entry->effect = IT_SET_SPEED;
				else if ( !vsync && entry->effect == IT_SET_SPEED && entry->effectvalue > 0x20 ) entry->effect = IT_SET_SONG_TEMPO;
			}
		}
	}
}


typedef struct
{
	GstDumbDec *dumb_dec;
	GArray *subsongs;
} gst_dumb_subsong_scan_context;


static int gst_dumb_scan_callback(void *context, int order, long length)
{
	gst_dumb_subsong_scan_context *ctx;
	gst_dumb_dec_subsong_info info;
	
	ctx = (gst_dumb_subsong_scan_context *)context;
	GST_DEBUG_OBJECT(ctx->dumb_dec, "found subsong in scan callback: order %d length %d", order, length);

	info.start_order = order;
	info.length = length;

	g_array_append_val(ctx->subsongs, info);

	return 0;
}


static void gst_dumb_scan_for_subsongs(GstDumbDec *dumb_dec)
{
	char const *format;
	int start_order;
	int is_mod;
	gst_dumb_subsong_scan_context ctx;
	GArray *subsongs;

	subsongs = g_array_new(FALSE, FALSE, sizeof(gst_dumb_dec_subsong_info));

	ctx.dumb_dec = dumb_dec;
	ctx.subsongs = subsongs;

	format = duh_get_tag(dumb_dec->duh, "FORMAT");
	start_order = dumb_it_scan_for_playable_orders(duh_get_it_sigdata(dumb_dec->duh), gst_dumb_scan_callback, &ctx);

	is_mod = (strcmp(format, "MOD") == 0);

	if (!start_order && is_mod)
	{
		DUMB_IT_SIGDATA *itsd = duh_get_it_sigdata(dumb_dec->duh);

		GST_DEBUG_OBJECT(dumb_dec, "song format is MOD -> need to repeat scan because of tempo conversion");

		if (!dumb_it_test_for_speed_and_tempo(itsd))
		{
			GArray *mod_subsongs;
			mod_subsongs = g_array_new(FALSE, FALSE, sizeof(gst_dumb_dec_subsong_info));
			ctx.subsongs = mod_subsongs;

			dumb_it_convert_tempos(itsd, TRUE);
			start_order = dumb_it_scan_for_playable_orders(duh_get_it_sigdata(dumb_dec->duh), gst_dumb_scan_callback, &ctx);
			if (!start_order)
			{
				guint i;
				long total_length_original;
				long total_length_vblank;
				gst_dumb_dec_subsong_info *subsong_info;
				gst_dumb_dec_subsong_info *mod_subsong_info;

				total_length_original = 0;
				total_length_vblank = 0;
				subsong_info = (gst_dumb_dec_subsong_info *)(subsongs->data);
				mod_subsong_info = (gst_dumb_dec_subsong_info *)(mod_subsongs->data);

				/* Safe to assume that both have the same song count as
				   speed/tempo don't affect song flow control */
				for (i = 0; i < subsongs->len; ++i)
				{
					total_length_original += subsong_info[i].length;
					total_length_vblank += mod_subsong_info[i].length;
				}

				if (
					(total_length_original != 0) ||
					(
						(total_length_vblank != 0) && (total_length_vblank < total_length_original)
					)
				)
				{
					for (i = 0; i < subsongs->len; ++i)
						subsong_info[i].length = mod_subsong_info[i].length;
				}
			}

			g_array_free(mod_subsongs, TRUE);
		}
	}

	dumb_dec->subsongs = subsongs;
}





static gboolean plugin_init(GstPlugin *plugin)
{
	if (!gst_element_register(plugin, "dumbdec", GST_RANK_PRIMARY + 1, gst_dumb_dec_get_type())) return FALSE;
	return TRUE;
}

GST_PLUGIN_DEFINE(
	GST_VERSION_MAJOR,
	GST_VERSION_MINOR,
	dumbdec,
	"DUMB (Dynamic Universal Music Bibliotheque) module player",
	plugin_init,
	"1.0",
	"LGPL",
	"package",
	"http://no-url-yet"
)

