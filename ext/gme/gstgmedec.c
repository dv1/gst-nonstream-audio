#include <config.h>
#include <gst/gst.h>

#include "gstgmedec.h"


GST_DEBUG_CATEGORY_STATIC(gmedec_debug);
#define GST_CAT_DEFAULT gmedec_debug



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

static gboolean gst_gme_dec_seek(GstNonstreamAudioDecoder *dec, GstClockTime new_position);
static GstClockTime gst_gme_dec_tell(GstNonstreamAudioDecoder *dec);

static gboolean gst_gme_dec_update_track_info(GstGmeDec *gme_dec, guint track_nr);

static gboolean gst_gme_dec_load_from_buffer(GstNonstreamAudioDecoder *dec, GstBuffer *source_data, guint initial_subsong, GstClockTime *initial_position, GstNonstreamAudioOutputMode *initial_output_mode);

static gboolean gst_gme_dec_set_current_subsong(GstNonstreamAudioDecoder *dec, guint subsong, GstClockTime *initial_position);
static guint gst_gme_dec_get_current_subsong(GstNonstreamAudioDecoder *dec);
static guint gst_gme_dec_get_num_subsongs(GstNonstreamAudioDecoder *dec);

static gboolean gst_gme_dec_set_num_loops(GstNonstreamAudioDecoder *dec, gint num_loops);
static gint gst_gme_dec_get_num_loops(GstNonstreamAudioDecoder *dec);

static guint gst_gme_dec_get_supported_output_modes(GstNonstreamAudioDecoder *dec);
static gboolean gst_gme_dec_decode(GstNonstreamAudioDecoder *dec, GstBuffer **buffer, guint *num_samples);



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

	gst_element_class_set_static_metadata(
		element_class,
		"video game music player",
		"Codec/Decoder/Audio",
		"Plays video game music using the Game Music Emulator library",
		"Carlos Rafael Giani <dv@pseudoterminal.org>"
	);
}


void gst_gme_dec_init(GstGmeDec *gme_dec)
{
	gme_dec->emu = NULL;
	gme_dec->cur_track = 0;
	gme_dec->num_tracks = 0;
	gme_dec->track_info = NULL;
}


static void gst_gme_dec_finalize(GObject *object)
{
	GstGmeDec *gme_dec;

	g_return_if_fail(GST_IS_GME_DEC(object));
	gme_dec = GST_GME_DEC(object);

	if (gme_dec->emu != NULL)
		gme_delete(gme_dec->emu);
}


static gboolean gst_gme_dec_seek(GstNonstreamAudioDecoder *dec, GstClockTime new_position)
{
	gme_err_t err;
	GstGmeDec *gme_dec = GST_GME_DEC(dec);
	g_return_val_if_fail(gme_dec->emu != NULL, FALSE);

	err = gme_seek(gme_dec->emu, new_position / GST_MSECOND);
	if (G_UNLIKELY(err != NULL))
	{
		GST_ERROR_OBJECT(dec, "error while seeking: %s", err);
		return FALSE;
	}
	else
		return TRUE;
}


static GstClockTime gst_gme_dec_tell(GstNonstreamAudioDecoder *dec)
{
	GstGmeDec *gme_dec = GST_GME_DEC(dec);
	g_return_val_if_fail(gme_dec->emu != NULL, GST_CLOCK_TIME_NONE);

	return (GstClockTime)(gme_tell(gme_dec->emu)) * GST_MSECOND;
}


static gboolean gst_gme_dec_update_track_info(GstGmeDec *gme_dec, guint track_nr)
{
	gme_err_t err;

	g_return_val_if_fail(gme_dec->emu != NULL, FALSE);

	err = gme_track_info(gme_dec->emu, &(gme_dec->track_info), track_nr);
	if (G_UNLIKELY(err != NULL))
	{
		GST_ERROR_OBJECT(gme_dec, "error while trying to get track information: %s", err);
		return FALSE;
	}

	gst_nonstream_audio_decoder_set_duration(
		GST_NONSTREAM_AUDIO_DECODER(gme_dec),
		(GstClockTime)(gme_dec->track_info->play_length) * GST_MSECOND
	);

	{
		GstTagList *tags;
		gme_info_t *info;

		tags = gst_tag_list_new_empty();
		info = gme_dec->track_info;

#define GME_ADD_TO_TAGS(INFO_FIELD, TAG_TYPE) \
		if (info->INFO_FIELD && *info->INFO_FIELD) \
			gst_tag_list_add(tags, GST_TAG_MERGE_REPLACE, (TAG_TYPE), info->INFO_FIELD, NULL);

		GME_ADD_TO_TAGS(system, GST_TAG_ENCODER);
		GME_ADD_TO_TAGS(game, GST_TAG_ALBUM);
		GME_ADD_TO_TAGS(song, GST_TAG_TITLE);
		GME_ADD_TO_TAGS(author, GST_TAG_ARTIST);
		GME_ADD_TO_TAGS(copyright, GST_TAG_COPYRIGHT);
		GME_ADD_TO_TAGS(comment, GST_TAG_COMMENT);
		GME_ADD_TO_TAGS(dumper, GST_TAG_CONTACT);

#undef GME_ADD_TO_TAGS

		gst_pad_push_event(GST_NONSTREAM_AUDIO_DECODER_SRC_PAD(gme_dec), gst_event_new_tag(tags));
	}

	return TRUE;
}


static gboolean gst_gme_dec_load_from_buffer(GstNonstreamAudioDecoder *dec, GstBuffer *source_data, guint initial_subsong, GstClockTime *initial_position, GstNonstreamAudioOutputMode *initial_output_mode)
{
	GstMapInfo map;
	gme_err_t err;
	GstGmeDec *gme_dec;
	gint sample_rate;
	
	gme_dec = GST_GME_DEC(dec);

	sample_rate = 48000;
	gst_nonstream_audio_decoder_get_downstream_info(dec, NULL, &sample_rate, NULL);

	/* Set output format */
	{
		GstAudioInfo audio_info;

		gst_audio_info_init(&audio_info);

		gst_audio_info_set_format(
			&audio_info,
			GST_AUDIO_FORMAT_S16,
			sample_rate,
			2,
			NULL
		);

		if (!gst_nonstream_audio_decoder_set_output_audioinfo(dec, &audio_info))
			return FALSE;
	}

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

	if (!gst_gme_dec_update_track_info(gme_dec, initial_subsong))
		return FALSE;

	err = gme_start_track(gme_dec->emu, initial_subsong);
	if (G_UNLIKELY(err != NULL))
	{
		GST_ERROR_OBJECT(dec, "error while starting track: %s", err);
		return FALSE;
	}

	*initial_position = 0;
	*initial_output_mode = GST_NONSTREM_AUDIO_OUTPUT_MODE_STEADY;

	return TRUE;
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

	if (!gst_gme_dec_update_track_info(gme_dec, subsong))
		return FALSE;

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

	static gint const num_samples_per_outbuf = 1024;
	static gint const num_bytes_per_outbuf = num_samples_per_outbuf * 2 * 2; // 2 bytes per sample, 2 channels

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



G_BEGIN_DECLS


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


G_END_DECLS

