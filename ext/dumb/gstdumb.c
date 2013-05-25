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

static gboolean gst_dumb_load(GstNonstreamAudioDecoder *dec, GstBuffer *source_data);

static gboolean gst_dumb_decode(GstNonstreamAudioDecoder *dec, GstBuffer **buffer, guint *num_samples, gdouble rate);

static gboolean gst_dumb_init_sigrenderer(GstDumb *dumb, GstClockTime seek_pos);



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
	if (!gst_dumb_init_sigrenderer(GST_DUMB(dec), new_position))
	{
		GST_ELEMENT_ERROR(dec, STREAM, DECODE, (NULL), ("cannot reinitialize DUMB decoding"));
		return FALSE;
	}

	return TRUE;
}


static GstClockTime gst_dumb_tell(GstNonstreamAudioDecoder *dec)
{
	GstClockTime pos;
	GstDumb *dumb = GST_DUMB(dec);

	if (dumb->duh_sigrenderer == NULL)
		return 0;

	pos = duh_sigrenderer_get_position(dumb->duh_sigrenderer);
	pos = pos * GST_SECOND / 65536;

	return pos;
}


static gboolean gst_dumb_load(GstNonstreamAudioDecoder *dec, GstBuffer *source_data)
{
	GstDumb *dumb = GST_DUMB(dec);

	dumb->sample_rate = DEFAULT_SAMPLE_RATE;
	dumb->num_channels = DEFAULT_NUM_CHANNELS;
	gst_nonstream_audio_decoder_get_downstream_format(dec, &(dumb->sample_rate), &(dumb->num_channels));

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

	if (!gst_dumb_init_sigrenderer(dumb, 0))
	{
		GST_ELEMENT_ERROR(dumb, STREAM, DECODE, (NULL), ("cannot initialize DUMB decoding"));
		return FALSE;
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

		if (!gst_nonstream_audio_decoder_set_output_audioinfo(dec, &audio_info))
			return FALSE;
	}

	gst_nonstream_audio_decoder_set_duration(dec, duh_get_length(dumb->duh) * GST_SECOND / 65536);

	{
		char const *title;
		GstTagList *tags;

		tags = gst_tag_list_new_empty();

		title = duh_get_tag(dumb->duh, "TITLE");
		if (title != NULL)
			gst_tag_list_add(tags, GST_TAG_MERGE_APPEND, GST_TAG_TITLE, title, NULL);

		gst_pad_push_event(GST_NONSTREAM_AUDIO_DECODER_SRC_PAD(dumb), gst_event_new_tag(tags));
	}


	return TRUE;
}


static gboolean gst_dumb_decode(GstNonstreamAudioDecoder *dec, GstBuffer **buffer, guint *num_samples, gdouble rate)
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
	actual_num_samples_read = duh_render(dumb->duh_sigrenderer, RENDER_BIT_DEPTH, 0, 1.0f, 65536.0f / dumb->sample_rate * rate, num_samples_per_outbuf, map.data);
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


static gboolean gst_dumb_init_sigrenderer(GstDumb *dumb, GstClockTime seek_pos)
{
	g_return_val_if_fail(dumb->duh != NULL, FALSE);

	if (dumb->duh_sigrenderer != NULL)
		duh_end_sigrenderer(dumb->duh_sigrenderer);
	dumb->duh_sigrenderer = duh_start_sigrenderer(dumb->duh, 0, dumb->num_channels, seek_pos * 65536 / GST_SECOND);

	if (dumb->duh_sigrenderer == NULL)
		return FALSE;

	{
		DUMB_IT_SIGRENDERER *itsr = duh_get_it_sigrenderer(dumb->duh_sigrenderer);
		dumb_it_set_loop_callback(itsr, &dumb_it_callback_terminate, NULL);
		dumb_it_set_xm_speed_zero_callback(itsr, &dumb_it_callback_terminate, NULL);
		dumb_it_set_global_volume_zero_callback(itsr, &dumb_it_callback_terminate, NULL);
	}

	return TRUE;
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

