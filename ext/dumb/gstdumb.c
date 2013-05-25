#include <stdlib.h>
#include <string.h>
#include <config.h>

#include <gst/gst.h>

#include "gstdumb.h"



GST_DEBUG_CATEGORY_STATIC(dumb_debug);
#define GST_CAT_DEFAULT dumb_debug


#define DEFAULT_SAMPLE_RATE 48000
#define DEFAULT_NUM_CHANNELS 2

#define RENDER_BIT_DEPTH 16
#define AUDIO_FORMAT GST_AUDIO_FORMAT_S16



static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE(
	"sink",
	GST_PAD_SINK,
	GST_PAD_ALWAYS,
	GST_STATIC_CAPS(
		"audio/x-mod"
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



G_DEFINE_TYPE(GstDumb, gst_dumb, GST_TYPE_NONSTREAM_AUDIO_DECODER)



static gboolean gst_dumb_seek(GstNonstreamAudioDecoder *dec, GstClockTime new_position);
static GstClockTime gst_dumb_tell(GstNonstreamAudioDecoder *dec);

gboolean gst_dumb_load(GstNonstreamAudioDecoder *dec, GstBuffer *source_data);

gboolean gst_dumb_decode(GstNonstreamAudioDecoder *dec, GstBuffer **buffer, guint *num_samples);



void gst_dumb_class_init(GstDumbClass *klass)
{
	GstElementClass *element_class;
	GstNonstreamAudioDecoderClass *dec_class;

	GST_DEBUG_CATEGORY_INIT(dumb_debug, "dumb", 0, "DUMB module player");

	element_class = GST_ELEMENT_CLASS(klass);
	dec_class = GST_NONSTREAM_AUDIO_DECODER_CLASS(klass);

	gst_element_class_add_pad_template(element_class, gst_static_pad_template_get(&sink_template));
	gst_element_class_add_pad_template(element_class, gst_static_pad_template_get(&src_template));

	dec_class->seek = gst_dumb_seek;
	dec_class->tell = gst_dumb_tell;
	dec_class->load = gst_dumb_load;
	dec_class->decode = gst_dumb_decode;

	gst_element_class_set_static_metadata(
		element_class,
		"DUMB module player",
		"Codec/Decoder/Audio",
		"Playes module files (MOD/S3M/XM/IT/MTM/...) using the DUMB (Dynamic Universal Music Bibliotheque) library",
		"Carlos Rafael Giani <dv@pseudoterminal.org>"
	);
}


void gst_dumb_init(GstDumb *dumb)
{
	dumb->duh = NULL;
	dumb->duh_sigrenderer = NULL;
}


static gboolean gst_dumb_seek(GstNonstreamAudioDecoder *dec, GstClockTime new_position)
{
	return TRUE;
}


static GstClockTime gst_dumb_tell(GstNonstreamAudioDecoder *dec)
{
	return 0;
}


gboolean gst_dumb_load(GstNonstreamAudioDecoder *dec, GstBuffer *source_data)
{
	GstCaps *allowed_srccaps;
	guint structure_nr, num_structures;
	gboolean ds_rate_found, ds_channels_found;
	GstDumb *dumb;

	dumb = GST_DUMB(dec);

	/* TODO: move this code for evaluating downstream caps to a base class function:
	void gst_nonstream_audio_decoder_get_downstream_format(GstNonstreamAudioDecoder *dec, gint *sample_rate, gint *num_channels);
	*/

	/* Get the caps that are allowed by downstream */
	{
		GstCaps *allowed_srccaps_unnorm = gst_pad_get_allowed_caps(GST_NONSTREAM_AUDIO_DECODER_SRC_PAD(dumb));
		allowed_srccaps = gst_caps_normalize(allowed_srccaps_unnorm);
	}

	ds_rate_found = FALSE;
	ds_channels_found = FALSE;

	/* Go through all allowed caps, see if one of them has sample rate or number of channels set (or both) */
	num_structures = gst_caps_get_size(allowed_srccaps);
	for (structure_nr = 0; structure_nr < num_structures; ++structure_nr)
	{
		GstStructure *structure;
		gint sample_rate, num_channels;

		ds_rate_found = FALSE;
		ds_channels_found = FALSE;

		structure = gst_caps_get_structure(allowed_srccaps, structure_nr);

		if (gst_structure_get_int(structure, "rate", &sample_rate))
		{
			GST_DEBUG_OBJECT(dumb, "got sample rate from srccaps structure #%u/%u : %d Hz", structure_nr, num_structures, sample_rate);
			ds_rate_found = TRUE;
			continue;
		}
		if (gst_structure_get_int(structure, "channels", &num_channels))
		{
			GST_DEBUG_OBJECT(dumb, "got number of channels from srccaps structure #%u/%u", structure_nr, num_structures, num_channels);
			ds_channels_found = TRUE;
			continue;
		}

		dumb->sample_rate = sample_rate;
		dumb->num_channels = num_channels;

		break;
	}

	gst_caps_unref(allowed_srccaps);

	if (!ds_rate_found)
	{
		dumb->sample_rate = DEFAULT_SAMPLE_RATE;
		GST_DEBUG_OBJECT(dumb, "downstream did not specify sample rate - using default (%d Hz)", dumb->sample_rate);
	}
	if (!ds_channels_found)
	{
		dumb->num_channels = DEFAULT_NUM_CHANNELS;
		GST_DEBUG_OBJECT(dumb, "downstream did not specify number of channels - using default (%d channels)", dumb->num_channels);
	}

	{
		GstMapInfo map;
		DUMBFILE *dumbfile;

		gst_buffer_map(source_data, &map, GST_MAP_READ);
		dumbfile = dumbfile_open_memory((char const *)(map.data), map.size);

		dumb->duh = dumb_read_any(dumbfile, 0/*restrict_*/, 0/*subsong*/);

		dumbfile_close(dumbfile);
		gst_buffer_unmap(source_data, &map);

		if (dumb->duh == NULL)
		{
			GST_ELEMENT_ERROR(dumb, STREAM, DECODE, (NULL), ("DUMB failed to read module data"));
			return FALSE;
		}
	}

	if (dumb->duh_sigrenderer != NULL)
		duh_end_sigrenderer(dumb->duh_sigrenderer);
	dumb->duh_sigrenderer = duh_start_sigrenderer(dumb->duh, 0, dumb->num_channels, 0/*position*/);

	if (dumb->duh_sigrenderer == NULL)
	{
		GST_ELEMENT_ERROR(dumb, STREAM, DECODE, (NULL), ("cannot initialize DUMB decoding"));
		return FALSE;
	}

	{
		DUMB_IT_SIGRENDERER *itsr = duh_get_it_sigrenderer(dumb->duh_sigrenderer);
		dumb_it_set_loop_callback(itsr, &dumb_it_callback_terminate, NULL);
		dumb_it_set_xm_speed_zero_callback(itsr, &dumb_it_callback_terminate, NULL);
		dumb_it_set_global_volume_zero_callback(itsr, &dumb_it_callback_terminate, NULL);
	}

	/* Set output format */
	{
		GstAudioInfo audio_info;

		gst_audio_info_init(&audio_info);

		gst_audio_info_set_format(
			&audio_info,
			GST_AUDIO_FORMAT_S16,
			dumb->sample_rate,
			dumb->num_channels,
			NULL
		);

		if (!gst_nonstream_audio_decoder_set_output_format(dec, &audio_info))
			return FALSE;
	}

	gst_nonstream_audio_decoder_set_duration(dec, duh_get_length(dumb->duh) * GST_SECOND / 65536);

	return TRUE;
}


gboolean gst_dumb_decode(GstNonstreamAudioDecoder *dec, GstBuffer **buffer, guint *num_samples)
{
	GstDumb *dumb;
	GstBuffer *outbuf;
	GstMapInfo map;
	gint num_samples_per_outbuf, num_bytes_per_outbuf, actual_num_samples_read;

	dumb = GST_DUMB(dec);

	num_samples_per_outbuf = 1024;
	num_bytes_per_outbuf = num_samples_per_outbuf * dumb->num_channels * RENDER_BIT_DEPTH / 8;

	outbuf = gst_buffer_new_allocate(NULL, num_bytes_per_outbuf, NULL);
	gst_buffer_map(outbuf, &map, GST_MAP_WRITE);
	actual_num_samples_read = duh_render(dumb->duh_sigrenderer, RENDER_BIT_DEPTH, 0, 1.0f, 65536.0f / dumb->sample_rate, num_samples_per_outbuf, map.data);
	gst_buffer_unmap(outbuf, &map);

	if (actual_num_samples_read == 0)
	{
		gst_buffer_unref(outbuf);
		GST_INFO_OBJECT(dumb, "DUMB reached end of module");
		return FALSE;
	}
	else
	{
		*buffer = outbuf;
		*num_samples = actual_num_samples_read;
		return TRUE;
	}
}





static gboolean plugin_init(GstPlugin *plugin)
{
	if (!gst_element_register(plugin, "dumbaudioplay", GST_RANK_SECONDARY + 1, gst_dumb_get_type())) return FALSE;
	return TRUE;
}

GST_PLUGIN_DEFINE(
	GST_VERSION_MAJOR,
	GST_VERSION_MINOR,
	dumb,
	"DUMB (Dynamic Universal Music Bibliotheque) module player",
	plugin_init,
	"1.0",
	"LGPL",
	"package",
	"http://no-url-yet"
)

