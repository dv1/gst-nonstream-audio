#include <config.h>
#include <gst/gst.h>

#include "gstopenmptdec.h"


GST_DEBUG_CATEGORY_STATIC(openmptdec_debug);
#define GST_CAT_DEFAULT openmptdec_debug


enum
{
	PROP_0,
	PROP_MASTER_GAIN,
	PROP_STEREO_SEPARATION,
	PROP_FILTER_LENGTH,
	PROP_VOLUME_RAMPING
};


#define DEFAULT_MASTER_GAIN 0
#define DEFAULT_STEREO_SEPARATION 100
#define DEFAULT_FILTER_LENGTH 0
#define DEFAULT_VOLUME_RAMPING -1

#define DEFAULT_SAMPLE_FORMAT GST_AUDIO_FORMAT_F32
#define DEFAULT_SAMPLE_RATE 48000
#define DEFAULT_NUM_CHANNELS 2


#define NUM_SAMPLES_PER_OUTBUF 1024



#if GST_CHECK_VERSION(1, 2, 0)
#define MOD_CAPS_TYPESTR ", type = (string) { 669, asylum-amf, dsmi-amf, extreme-ams, velvet-ams, dbm, digi, dmf, dsm, far, gdm, imf, it, j2b, mdl, med, mod, mt2, mtm, okt, psm, ptm, s3m, stm, ult, xm }"
#else
#define MOD_CAPS_TYPESTR ""
#endif


static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE(
	"sink",
	GST_PAD_SINK,
	GST_PAD_ALWAYS,
	GST_STATIC_CAPS("audio/x-mod " MOD_CAPS_TYPESTR)
);

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE(
	"src",
	GST_PAD_SRC,
	GST_PAD_ALWAYS,
	GST_STATIC_CAPS(
		"audio/x-raw, "
		"format = (string) { " GST_AUDIO_NE(F32) ", " GST_AUDIO_NE(S16) " }, "
		"layout = (string) interleaved, "
		"rate = (int) [ 1, 96000 ], "
		"channels = (int) { 2, 4, 1 } "
	)
);



G_DEFINE_TYPE(GstOpenMptDec, gst_openmpt_dec, GST_TYPE_NONSTREAM_AUDIO_DECODER)



static void gst_openmpt_dec_finalize(GObject *object);

static void gst_openmpt_dec_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec);
static void gst_openmpt_dec_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);

static gboolean gst_openmpt_dec_seek(GstNonstreamAudioDecoder *dec, GstClockTime new_position);
static GstClockTime gst_openmpt_dec_tell(GstNonstreamAudioDecoder *dec);

static void gst_openmpt_dec_log_func(char const *message, void *user);
static gboolean gst_openmpt_dec_load_from_buffer(GstNonstreamAudioDecoder *dec, GstBuffer *source_data, guint initial_subsong, GstClockTime *initial_position, GstNonstreamAudioOutputMode *initial_output_mode);

static gboolean gst_openmpt_dec_set_current_subsong(GstNonstreamAudioDecoder *dec, guint subsong, GstClockTime *initial_position);
static guint gst_openmpt_dec_get_current_subsong(GstNonstreamAudioDecoder *dec);

static guint gst_openmpt_dec_get_num_subsongs(GstNonstreamAudioDecoder *dec);
static GstClockTime gst_openmpt_dec_get_subsong_duration(GstNonstreamAudioDecoder *dec, guint subsong);
static GstTagList* gst_openmpt_dec_get_subsong_tags(GstNonstreamAudioDecoder *dec, guint subsong);

static gboolean gst_openmpt_dec_set_num_loops(GstNonstreamAudioDecoder *dec, gint num_loops);
static gint gst_openmpt_dec_get_num_loops(GstNonstreamAudioDecoder *dec);

static guint gst_openmpt_dec_get_supported_output_modes(GstNonstreamAudioDecoder *dec);
static gboolean gst_openmpt_dec_decode(GstNonstreamAudioDecoder *dec, GstBuffer **buffer, guint *num_samples);



void gst_openmpt_dec_class_init(GstOpenMptDecClass *klass)
{
	GObjectClass *object_class;
	GstElementClass *element_class;
	GstNonstreamAudioDecoderClass *dec_class;

	GST_DEBUG_CATEGORY_INIT(openmptdec_debug, "openmptdec", 0, "video game music player");

	object_class = G_OBJECT_CLASS(klass);
	element_class = GST_ELEMENT_CLASS(klass);
	dec_class = GST_NONSTREAM_AUDIO_DECODER_CLASS(klass);

	gst_element_class_add_pad_template(element_class, gst_static_pad_template_get(&sink_template));
	gst_element_class_add_pad_template(element_class, gst_static_pad_template_get(&src_template));

	object_class->finalize = GST_DEBUG_FUNCPTR(gst_openmpt_dec_finalize);
	object_class->set_property = GST_DEBUG_FUNCPTR(gst_openmpt_dec_set_property);
	object_class->get_property = GST_DEBUG_FUNCPTR(gst_openmpt_dec_get_property);

	dec_class->seek = GST_DEBUG_FUNCPTR(gst_openmpt_dec_seek);
	dec_class->tell = GST_DEBUG_FUNCPTR(gst_openmpt_dec_tell);
	dec_class->load_from_buffer = GST_DEBUG_FUNCPTR(gst_openmpt_dec_load_from_buffer);
	dec_class->set_num_loops = GST_DEBUG_FUNCPTR(gst_openmpt_dec_set_num_loops);
	dec_class->get_num_loops = GST_DEBUG_FUNCPTR(gst_openmpt_dec_get_num_loops);
	dec_class->get_supported_output_modes = GST_DEBUG_FUNCPTR(gst_openmpt_dec_get_supported_output_modes);
	dec_class->decode = GST_DEBUG_FUNCPTR(gst_openmpt_dec_decode);
	dec_class->set_current_subsong = GST_DEBUG_FUNCPTR(gst_openmpt_dec_set_current_subsong);
	dec_class->get_current_subsong = GST_DEBUG_FUNCPTR(gst_openmpt_dec_get_current_subsong);
	dec_class->get_num_subsongs = GST_DEBUG_FUNCPTR(gst_openmpt_dec_get_num_subsongs);
	dec_class->get_subsong_duration = GST_DEBUG_FUNCPTR(gst_openmpt_dec_get_subsong_duration);
	dec_class->get_subsong_tags = GST_DEBUG_FUNCPTR(gst_openmpt_dec_get_subsong_tags);

	gst_element_class_set_static_metadata(
		element_class,
		"OpenMPT module player",
		"Codec/Decoder/Audio",
		"Plays module files (MOD/S3M/XM/IT/MTM/...) using OpenMPT",
		"Carlos Rafael Giani <dv@pseudoterminal.org>"
	);

	g_object_class_install_property(
		object_class,
		PROP_MASTER_GAIN,
		g_param_spec_int(
			"master-gain",
			"Master gain",
			"Gain to apply to the playback, in millibel",
			-G_MAXINT, G_MAXINT,
			DEFAULT_MASTER_GAIN,
			G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS
		)
	);
	g_object_class_install_property(
		object_class,
		PROP_STEREO_SEPARATION,
		g_param_spec_int(
			"stereo-separation",
			"Stereo separation",
			"Degree of separation for stereo channels, in percent",
			0, 400,
			DEFAULT_STEREO_SEPARATION,
			G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS
		)
	);
	g_object_class_install_property(
		object_class,
		PROP_FILTER_LENGTH,
		g_param_spec_int(
			"filter-length",
			"Filter length",
			"Length of interpolation filter to use for the samples (0 = internal default)",
			0, 8,
			DEFAULT_FILTER_LENGTH,
			G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS
		)
	);
	g_object_class_install_property(
		object_class,
		PROP_VOLUME_RAMPING,
		g_param_spec_int(
			"volume-ramping",
			"Volume ramping",
			"Volume ramping strength; higher value -> slower ramping (-1 = internal default)",
			-1, 10,
			DEFAULT_VOLUME_RAMPING,
			G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS
		)
	);
}


void gst_openmpt_dec_init(GstOpenMptDec *openmpt_dec)
{
	openmpt_dec->mod = NULL;

	openmpt_dec->cur_subsong = 0;
	openmpt_dec->num_subsongs = 0;
	openmpt_dec->subsong_durations = NULL;

	openmpt_dec->num_loops = 0;

	openmpt_dec->master_gain = DEFAULT_MASTER_GAIN;
	openmpt_dec->stereo_separation = DEFAULT_STEREO_SEPARATION;
	openmpt_dec->filter_length = DEFAULT_FILTER_LENGTH;
	openmpt_dec->volume_ramping = DEFAULT_VOLUME_RAMPING;

	openmpt_dec->sample_format = DEFAULT_SAMPLE_FORMAT;
	openmpt_dec->sample_rate = DEFAULT_SAMPLE_RATE;
	openmpt_dec->num_channels = DEFAULT_NUM_CHANNELS;
}


static void gst_openmpt_dec_finalize(GObject *object)
{
	GstOpenMptDec *openmpt_dec;

	g_return_if_fail(GST_IS_OPENMPT_DEC(object));
	openmpt_dec = GST_OPENMPT_DEC(object);

	if (openmpt_dec->mod != NULL)
		openmpt_module_destroy(openmpt_dec->mod);

	g_free(openmpt_dec->subsong_durations);

	G_OBJECT_CLASS(gst_openmpt_dec_parent_class)->finalize(object);
}


static void gst_openmpt_dec_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	GstNonstreamAudioDecoder *dec;
	GstOpenMptDec *openmpt_dec;

	dec = GST_NONSTREAM_AUDIO_DECODER(object);
	openmpt_dec = GST_OPENMPT_DEC(object);

	switch (prop_id)
	{
		case PROP_MASTER_GAIN:
		{
			GST_NONSTREAM_AUDIO_DECODER_STREAM_LOCK(dec);
			openmpt_dec->master_gain = g_value_get_int(value);
			if (openmpt_dec->mod != NULL)
				openmpt_module_set_render_param(openmpt_dec->mod, OPENMPT_MODULE_RENDER_MASTERGAIN_MILLIBEL, openmpt_dec->master_gain);
			GST_NONSTREAM_AUDIO_DECODER_STREAM_UNLOCK(dec);
			break;
		}
		case PROP_STEREO_SEPARATION:
		{
			GST_NONSTREAM_AUDIO_DECODER_STREAM_LOCK(dec);
			openmpt_dec->stereo_separation = g_value_get_int(value);
			if (openmpt_dec->mod != NULL)
				openmpt_module_set_render_param(openmpt_dec->mod, OPENMPT_MODULE_RENDER_STEREOSEPARATION_PERCENT, openmpt_dec->stereo_separation);
			GST_NONSTREAM_AUDIO_DECODER_STREAM_UNLOCK(dec);
			break;
		}
		case PROP_FILTER_LENGTH:
		{
			GST_NONSTREAM_AUDIO_DECODER_STREAM_LOCK(dec);
			openmpt_dec->filter_length = g_value_get_int(value);
			if (openmpt_dec->mod != NULL)
				openmpt_module_set_render_param(openmpt_dec->mod, OPENMPT_MODULE_RENDER_INTERPOLATIONFILTER_LENGTH, openmpt_dec->filter_length);
			GST_NONSTREAM_AUDIO_DECODER_STREAM_UNLOCK(dec);
			break;
		}
		case PROP_VOLUME_RAMPING:
		{
			GST_NONSTREAM_AUDIO_DECODER_STREAM_LOCK(dec);
			openmpt_dec->volume_ramping = g_value_get_int(value);
			if (openmpt_dec->mod != NULL)
				openmpt_module_set_render_param(openmpt_dec->mod, OPENMPT_MODULE_RENDER_VOLUMERAMPING_STRENGTH, openmpt_dec->volume_ramping);
			GST_NONSTREAM_AUDIO_DECODER_STREAM_UNLOCK(dec);
			break;
		}
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
			break;
	}
}


static void gst_openmpt_dec_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	GstOpenMptDec *openmpt_dec = GST_OPENMPT_DEC(object);

	switch (prop_id)
	{
		case PROP_MASTER_GAIN:
		{
			g_value_set_int(value, openmpt_dec->master_gain);
			break;
		}
		case PROP_STEREO_SEPARATION:
		{
			g_value_set_int(value, openmpt_dec->stereo_separation);
			break;
		}
		case PROP_FILTER_LENGTH:
		{
			g_value_set_int(value, openmpt_dec->filter_length);
			break;
		}
		case PROP_VOLUME_RAMPING:
		{
			g_value_set_int(value, openmpt_dec->volume_ramping);
			break;
		}
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
			break;
	}
}


static gboolean gst_openmpt_dec_seek(GstNonstreamAudioDecoder *dec, GstClockTime new_position)
{
	GstOpenMptDec *openmpt_dec = GST_OPENMPT_DEC(dec);
	g_return_val_if_fail(openmpt_dec->mod != NULL, FALSE);

	openmpt_module_set_position_seconds(openmpt_dec->mod, (double)(new_position) / GST_SECOND);

	return TRUE;
}


static GstClockTime gst_openmpt_dec_tell(GstNonstreamAudioDecoder *dec)
{
	GstOpenMptDec *openmpt_dec = GST_OPENMPT_DEC(dec);
	g_return_val_if_fail(openmpt_dec->mod != NULL, GST_CLOCK_TIME_NONE);

	return (GstClockTime)(openmpt_module_get_position_seconds(openmpt_dec->mod) * GST_SECOND);
}


static void gst_openmpt_dec_log_func(char const *message, void *user)
{
	GST_LOG_OBJECT(GST_OBJECT(user), "%s", message);
}


static gboolean gst_openmpt_dec_load_from_buffer(GstNonstreamAudioDecoder *dec, GstBuffer *source_data, guint initial_subsong, GstClockTime *initial_position, GstNonstreamAudioOutputMode *initial_output_mode)
{
	GstMapInfo map;
	GstOpenMptDec *openmpt_dec;
	
	openmpt_dec = GST_OPENMPT_DEC(dec);

	openmpt_dec->sample_format = DEFAULT_SAMPLE_FORMAT;
	openmpt_dec->sample_rate = DEFAULT_SAMPLE_RATE;
	openmpt_dec->num_channels = DEFAULT_NUM_CHANNELS;
	gst_nonstream_audio_decoder_get_downstream_info(dec, &(openmpt_dec->sample_format), &(openmpt_dec->sample_rate), &(openmpt_dec->num_channels));

	/* Set output format */
	if (!gst_nonstream_audio_decoder_set_output_audioinfo_simple(
		dec,
		openmpt_dec->sample_rate,
		openmpt_dec->sample_format,
		openmpt_dec->num_channels
	))
		return FALSE;

	gst_buffer_map(source_data, &map, GST_MAP_READ);
	openmpt_dec->mod = openmpt_module_create_from_memory(map.data, map.size, gst_openmpt_dec_log_func, dec, NULL);
	gst_buffer_unmap(source_data, &map);

	if (openmpt_dec->mod == NULL)
	{
		GST_ERROR_OBJECT(dec, "loading module failed");
		return FALSE;
	}

	{
		char const *metadata_keys = openmpt_module_get_metadata_keys(openmpt_dec->mod);
		if (metadata_keys != NULL)
		{
			GST_DEBUG_OBJECT(dec, "metadata keys: [%s]", metadata_keys);
			openmpt_free_string(metadata_keys);
		}
		else
		{
			GST_DEBUG_OBJECT(dec, "no metadata keys found");
		}
	}

	openmpt_dec->num_subsongs = openmpt_module_get_num_subsongs(openmpt_dec->mod);
	if (G_UNLIKELY(initial_subsong >= openmpt_dec->num_subsongs))
	{
		GST_WARNING_OBJECT(openmpt_dec, "initial subsong %u out of bounds (there are %u subsongs) - setting it to 0", initial_subsong, openmpt_dec->num_subsongs);
		initial_subsong = 0;
	}

	GST_INFO_OBJECT(openmpt_dec, "%d subsong(s) available", openmpt_dec->num_subsongs);

	*initial_position = 0;
	*initial_output_mode = GST_NONSTREM_AUDIO_OUTPUT_MODE_STEADY;

	if (openmpt_dec->num_subsongs > 0)
	{
		guint i;

		openmpt_dec->subsong_durations = g_try_malloc(openmpt_dec->num_subsongs * sizeof(double));
		if (openmpt_dec->subsong_durations == NULL)
		{
			GST_ELEMENT_ERROR(openmpt_dec, RESOURCE, NO_SPACE_LEFT, ("could not allocate memory for subsong duration array"), (NULL));
			return FALSE;
		}

		for (i = 0; i < openmpt_dec->num_subsongs; ++i)
		{
			openmpt_module_select_subsong(openmpt_dec->mod, i);
			openmpt_dec->subsong_durations[i] = openmpt_module_get_duration_seconds(openmpt_dec->mod);
		}
	}

	openmpt_module_select_subsong(openmpt_dec->mod, initial_subsong);
	openmpt_module_set_repeat_count(openmpt_dec->mod, openmpt_dec->num_loops);
	openmpt_module_set_render_param(openmpt_dec->mod, OPENMPT_MODULE_RENDER_MASTERGAIN_MILLIBEL, openmpt_dec->master_gain);
	openmpt_module_set_render_param(openmpt_dec->mod, OPENMPT_MODULE_RENDER_STEREOSEPARATION_PERCENT, openmpt_dec->stereo_separation);
	openmpt_module_set_render_param(openmpt_dec->mod, OPENMPT_MODULE_RENDER_INTERPOLATIONFILTER_LENGTH, openmpt_dec->filter_length);
	openmpt_module_set_render_param(openmpt_dec->mod, OPENMPT_MODULE_RENDER_VOLUMERAMPING_STRENGTH, openmpt_dec->volume_ramping);

	{
		GstTagList *tags;
		char const *metadata;

		tags = gst_tag_list_new_empty();

#define GSTOPENMPT_ADD_TO_TAGS(KEY, TAG_TYPE) \
		metadata = openmpt_module_get_metadata(openmpt_dec->mod, (KEY)); \
		if (metadata && *metadata) \
		{ \
			gst_tag_list_add(tags, GST_TAG_MERGE_REPLACE, (TAG_TYPE), metadata, NULL); \
		} \
		if (metadata) \
		{ \
			openmpt_free_string(metadata); \
		}

		GSTOPENMPT_ADD_TO_TAGS("title", GST_TAG_TITLE);
		GSTOPENMPT_ADD_TO_TAGS("author", GST_TAG_ARTIST);
		GSTOPENMPT_ADD_TO_TAGS("tracker", GST_TAG_ENCODER);
		GSTOPENMPT_ADD_TO_TAGS("message", GST_TAG_COMMENT);

#undef GSTOPENMPT_ADD_TO_TAGS

		gst_pad_push_event(GST_NONSTREAM_AUDIO_DECODER_SRC_PAD(openmpt_dec), gst_event_new_tag(tags));
	}

	return TRUE;
}


static gboolean gst_openmpt_dec_set_current_subsong(GstNonstreamAudioDecoder *dec, guint subsong, GstClockTime *initial_position)
{
	GstOpenMptDec *openmpt_dec = GST_OPENMPT_DEC(dec);
	g_return_val_if_fail(openmpt_dec->mod != NULL, FALSE);

	openmpt_module_select_subsong(openmpt_dec->mod, subsong);

	openmpt_dec->cur_subsong = subsong;
	*initial_position = 0;

	return TRUE;
}


static guint gst_openmpt_dec_get_current_subsong(GstNonstreamAudioDecoder *dec)
{
	GstOpenMptDec *openmpt_dec = GST_OPENMPT_DEC(dec);
	return openmpt_dec->cur_subsong;
}


static guint gst_openmpt_dec_get_num_subsongs(GstNonstreamAudioDecoder *dec)
{
	GstOpenMptDec *openmpt_dec = GST_OPENMPT_DEC(dec);
	return openmpt_dec->num_subsongs;
}


static GstClockTime gst_openmpt_dec_get_subsong_duration(GstNonstreamAudioDecoder *dec, guint subsong)
{
	GstOpenMptDec *openmpt_dec = GST_OPENMPT_DEC(dec);
	return (GstClockTime)(openmpt_dec->subsong_durations[subsong] * GST_SECOND);
}


static GstTagList* gst_openmpt_dec_get_subsong_tags(GstNonstreamAudioDecoder *dec, guint subsong)
{
	GstOpenMptDec *openmpt_dec;
	char const *name;

	openmpt_dec = GST_OPENMPT_DEC(dec);

	name = openmpt_module_get_subsong_name(openmpt_dec->mod, subsong);
	if (name != NULL)
	{
		GstTagList *tags = gst_tag_list_new_empty();
		gst_tag_list_add(tags, GST_TAG_MERGE_REPLACE, "title", name, NULL);
		openmpt_free_string(name);
		return tags;
	}
	else
		return NULL;
}


static gboolean gst_openmpt_dec_set_num_loops(GstNonstreamAudioDecoder *dec, gint num_loops)
{
	GstOpenMptDec *openmpt_dec = GST_OPENMPT_DEC(dec);
	openmpt_dec->num_loops = num_loops;

	if (openmpt_dec->mod != NULL)
		return openmpt_module_set_repeat_count(openmpt_dec->mod, num_loops);
	else
		return TRUE;
}


static gint gst_openmpt_dec_get_num_loops(GstNonstreamAudioDecoder *dec)
{
	GstOpenMptDec *openmpt_dec = GST_OPENMPT_DEC(dec);
	return openmpt_dec->num_loops;
}


static guint gst_openmpt_dec_get_supported_output_modes(G_GNUC_UNUSED GstNonstreamAudioDecoder *dec)
{
	return 1u << GST_NONSTREM_AUDIO_OUTPUT_MODE_STEADY;
}


static gboolean gst_openmpt_dec_decode(GstNonstreamAudioDecoder *dec, GstBuffer **buffer, guint *num_samples)
{
	GstOpenMptDec *openmpt_dec;
	GstBuffer *outbuf;
	GstMapInfo map;
	size_t num_read_samples;
	GstAudioFormatInfo const *fmt_info;

	openmpt_dec = GST_OPENMPT_DEC(dec);

	fmt_info = gst_audio_format_get_info(openmpt_dec->sample_format);

	outbuf = gst_nonstream_audio_decoder_allocate_output_buffer(dec, NUM_SAMPLES_PER_OUTBUF * (fmt_info->width / 8) * openmpt_dec->num_channels);
	if (G_UNLIKELY(outbuf == NULL))
		return FALSE;

	gst_buffer_map(outbuf, &map, GST_MAP_WRITE);

	switch (openmpt_dec->sample_format)
	{
		case GST_AUDIO_FORMAT_S16:
		{
			int16_t *out_samples = (int16_t*)(map.data);
			switch (openmpt_dec->num_channels)
			{
				case 1:
					num_read_samples = openmpt_module_read_mono(openmpt_dec->mod, openmpt_dec->sample_rate, NUM_SAMPLES_PER_OUTBUF, out_samples);
					break;
				case 2:
					num_read_samples = openmpt_module_read_interleaved_stereo(openmpt_dec->mod, openmpt_dec->sample_rate, NUM_SAMPLES_PER_OUTBUF, out_samples);
					break;
				case 4:
					num_read_samples = openmpt_module_read_interleaved_quad(openmpt_dec->mod, openmpt_dec->sample_rate, NUM_SAMPLES_PER_OUTBUF, out_samples);
					break;
				default:
					g_assert_not_reached();
			}
			break;
		}
		case GST_AUDIO_FORMAT_F32:
		{
			float *out_samples = (float*)(map.data);
			switch (openmpt_dec->num_channels)
			{
				case 1:
					num_read_samples = openmpt_module_read_float_mono(openmpt_dec->mod, openmpt_dec->sample_rate, NUM_SAMPLES_PER_OUTBUF, out_samples);
					break;
				case 2:
					num_read_samples = openmpt_module_read_interleaved_float_stereo(openmpt_dec->mod, openmpt_dec->sample_rate, NUM_SAMPLES_PER_OUTBUF, out_samples);
					break;
				case 4:
					num_read_samples = openmpt_module_read_interleaved_float_quad(openmpt_dec->mod, openmpt_dec->sample_rate, NUM_SAMPLES_PER_OUTBUF, out_samples);
					break;
				default:
					g_assert_not_reached();
			}
			break;
		}
		default:
		{
			GST_ERROR_OBJECT(dec, "using unsupported sample format %s", fmt_info->name);
			g_assert_not_reached();
		}
	}

	gst_buffer_unmap(outbuf, &map);

	if (num_read_samples == 0)
		return FALSE;

	*buffer = outbuf;
	*num_samples = num_read_samples;

	return TRUE;
}



G_BEGIN_DECLS


static gboolean plugin_init(GstPlugin *plugin)
{
	if (!gst_element_register(plugin, "openmptdec", GST_RANK_PRIMARY + 2, gst_openmpt_dec_get_type())) return FALSE;
	return TRUE;
}

GST_PLUGIN_DEFINE(
	GST_VERSION_MAJOR,
	GST_VERSION_MINOR,
	openmptdec,
	"OpenMPT module player",
	plugin_init,
	"1.0",
	"LGPL",
	"package",
	"http://no-url-yet"
)


G_END_DECLS

