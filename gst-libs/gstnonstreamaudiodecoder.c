#include <gst/gst.h>
#include <gst/audio/audio.h>
#include <gst/base/gstadapter.h>

#include "gstnonstreamaudiodecoder.h"

/**
 * SECTION:gstnonstreamaudiodecoder
 * @short_description: Base class for decoding of non-streaming audio
 *
 * This base class is for decoders which do not operate on a streaming model.
 * That is: they load the encoded media at once, as part of an initialization,
 * and afterwards can decode samples (sometimes referred to as "rendering the
 * samples").
 *
 * This sets it apart from GstAudioDecoder, which is a base class for
 * streaming audio decoders.
 *
 * The base class is conceptually a mix between decoder and parser. This is
 * unavoidable, since virtually no format that isn't streaming based has a
 * clear distinction between parsing and decoding. As a result, this class
 * also handles seeking.
 *
 * Non-streaming audio formats tend to have some characteristics unknown to
 * more "regular" bitstreams. These include subsongs and looping.
 *
 * Subsongs are a set of songs-within-a-song. An analogy would be a multitrack
 * recording, where each track is its own song. The first subsong is typically
 * the "main" one. Subsongs were popular for video games to enable context-
 * aware music; for example, subsong #0 would be the "main" song, #1 would be
 * an alternate song playing when a fight started, #2 would be heard during
 * conversations etc. The base class is designed to always have subsongs. If
 * the subclass doesn't provide any, the base class creates a pseudo subsong.
 * This "subsong" is actually the whole song.
 * Downstream is informed about the subsong using a table of contents (TOC),
 * but only if there are at least 2 subsongs.
 *
 * Looping refers to jumps within the song, typically backwards to the loop
 * start (although bi-directional looping is possible). The loop is defined
 * by a chronological start and end; once the playback position reaches the
 * loop end, it jumps back to the loop start.
 * Depending on the subclass, looping may not be possible at all, or it
 * may only be possible to enable/disable it (that is, either no looping, or
 * an infinite amount of loops), or it may allow for defining a finite number
 * of times the loop is repeated.
 * Looping can affect output in two ways. Either, the playback position is
 * reset to the start of the loop, similar to what happens after a seek event.
 * Or, it is not reset, so the pipeline sees playback steadily moving forwards,
 * the playback position monotonically increasing. However, seeking must
 * always happen within the confines of the defined subsong duration; for
 * example, if a subsong is 2 minutes long, steady playback is at 5 minutes
 * (because infinite looping is enabled), then it must not be possible to seek
 * past the 2 minute mark.
 *
 * If the initial subsong and loop count are set to values the subclass does
 * not support, the subclass has a chance to correct these values.
 * @get_property then reports the corrected versions.
 *
 * The base class operates as follows:
 * <orderedlist>
 * <listitem>
 *   <itemizedlist><title>Unloaded mode</title>
 *     <listitem><para>
 *       Initial values are set. If a current subsong has already been
 *       defined (for example over the command line with gst-launch), then
 *       the subsong index is copied over to initial_subsong .
 *       Same goes for the num-loops property.
 *       Media is NOT loaded yet.
 *     </para></listitem>
 *     <listitem><para>
 *       Once the sinkpad is activated, the process continues. If the sinkpad
 *       is activated in push mode, the class accumulates the incoming media
 *       data in an adapter inside the sinkpad's chain function until either an
 *       EOS event is received from upstream, or the number of bytes reported
 *       by upstream is reached. Then it loads the media, and starts the decoder
 *       loop task.
 *       If the sinkpad is activated in pull mode, it starts the task; inside
 *       the decoder loop, it pulls the entire data from upstream at once.
 *     <listitem><para>
 *       In both cases, if upstream cannot respond to the size query (in bytes)
 *       of @load_from_buffer fails, an error is reported, and the pipeline
 *       stops.
 *     </para></listitem>
 *     <listitem><para>
 *       Also, in both cases, if there are no errors, @load_from_buffer is
 *       called to load the media. The subclass must at least call
 *       @gst_nonstream_audio_decoder_set_output_audioinfo there, and is free
 *       to make use of the initial subsong, output mode, and position. If the
 *       actual output mode or position differs from the initial value,
 *       it must set the initial value to the actual one (for example, if the
 *       actual starting position is always 0, set *initial_position to 0).
 *       If loading is unsuccessful, an error is reported, and the pipeline
 *       stops. Otherwise, the base class @get_current_subsong to retrieve
 *       the actual current subsong, @get_subsong_duration to report the current
 *       subsong's duration in a duration event and message, and @get_subsong_tags
 *       to send tags downstream in an event (these functions are optional; if
 *       set to NULL, the associated operation is skipped). Afterwards, the base
 *       class switches to loaded mode.
 *     </para></listitem>
 *   </itemizedlist>
 *   <itemizedlist><title>Loaded mode</title>
 *     <listitem><para>
 *       Inside the decoder loop task, the base class repeatedly calls @decode,
 *       which returns a buffer with decoded, ready-to-play samples. If the
 *       subclass reached the end of playback, @decode returns FALSE, otherwise
 *       TRUE.
 *     </para></listitem>
 *     <listitem><para>
 *       Upon reaching a loop end, subclass either ignores that, or loops back
 *       to the beginning of the loop. In the latter case, if the output mode is
 *       set to LOOPING, the subclass must call @gst_nonstream_audio_decoder_handle_loop
 *       *after* the playback position moved to the start of the loop. In
 *       STEADY mode, the subclass must not call this function.
 *       Since many decoders only provide a callback for when the looping occurs,
 *       and that looping occurs inside the decoding operation itself, this
 *       mechanism for subclass is suggested: set a flag inside such a callback.
 *       Then, in the next @decode call, before doing the decoding, this flag is
 *       checked; if it is set, @gst_nonstream_audio_decoder_handle_loop is
 *       called, and the flag is cleared.
 *       (This function call is necessary in LOOPING mode because it updates the
 *       current segment and makes sure the next buffer that is sent downstream
 *       has its DISCONT flag set.)
 *     </para></listitem>
 *     <listitem><para>
 *       When the current subsong is switched, @set_current_subsong is called.
 *       If it fails, a warning is reported, and nothing else is done. Otherwise,
 *       it calls @get_subsong_duration to get the new current subsongs's
 *       duration, @get_subsong_tags to get its tags, reports a new duration
 *       (i.e. it sends a duration event downstream and generates a duration
 *       message), updates the current segment, and sends the subsong's tags in
 *       an event downstream. (If @set_current_subsong has been set to NULL by
 *       the subclass, attempts to set a current subsong are ignored; likewise,
 *       if @get_subsong_duration is NULL, no duration is reported, and if
 *       @get_subsong_tags is NULL, no tags are sent downstream.)
 *     </para></listitem>
 *     <listitem><para>
 *       When an attempt is made to switch the output mode, it is checked against
 *       the bitmask returned by @get_supported_output_modes. If the proposed
 *       new output mode is supported, the current segment is updated
 *       (it is open-ended in STEADY mode, and covers the (sub)song length in
 *       LOOPING mode), and the subclass' @set_output_mode function is called
 *       unless it is set to NULL. Subclasses should reset internal loop counters
 *       in this function.
 *     </para></listitem>
 *   </itemizedlist>
 * </listitem>
 * </orderedlist>
 *
 * The relationship between (sub)song duration, output mode, and number of loops
 * is defined this way (this is all done by the base class automatically):
 * <itemizedlist>
 * <listitem><para>
 *   Segments have their duration and stop values set to GST_CLOCK_TIME_NONE in
 *   STEADY mode, and to the duration of the (sub)song in LOOPING mode.
 * </para></listitem>
 * <listitem><para>
 *   The duration that is returned to a DURATION query is always the duration
 *   of the (sub)song, regardless of number of loops or output mode. The same
 *   goes for DURATION messages and tags.
 * </para></listitem>
 * <listitem><para>
 *   If the number of loops is >0 or -1, durations of TOC entries are set to
 *   the duration of the respective subsong in LOOPING mode and to G_MAXINT64 in
 *   STEADY mode. If the number of loops is 0, entry durations are set to the
 *   subsong duration regardless of the output mode.
 * </para></listitem>
 * </itemizedlist>
 */


GST_DEBUG_CATEGORY (nonstream_audiodecoder_debug);
#define GST_CAT_DEFAULT nonstream_audiodecoder_debug


enum
{
	PROP_0,
	PROP_CURRENT_SUBSONG,
	PROP_NUM_LOOPS,
	PROP_OUTPUT_MODE
};

#define DEFAULT_CURRENT_SUBSONG 0
#define DEFAULT_NUM_SUBSONGS 0
#define DEFAULT_NUM_LOOPS 0
#define DEFAULT_OUTPUT_MODE GST_NONSTREM_AUDIO_OUTPUT_MODE_STEADY



static GType gst_nonstream_audio_decoder_output_mode_get_type(void);
#define GST_TYPE_NONSTREAM_AUDIO_DECODER_OUTPUT_MODE (gst_nonstream_audio_decoder_output_mode_get_type())



static void gst_nonstream_audio_decoder_class_init(GstNonstreamAudioDecoderClass *klass);
static void gst_nonstream_audio_decoder_init(GstNonstreamAudioDecoder *dec, GstNonstreamAudioDecoderClass *klass);

static GstStateChangeReturn gst_nonstream_audio_decoder_change_state(GstElement *element, GstStateChange transition);


static gboolean gst_nonstream_audio_decoder_sink_event(GstPad *pad, GstObject *parent, GstEvent *event);
static GstFlowReturn gst_nonstream_audio_decoder_chain(GstPad *pad, GstObject *parent, GstBuffer *buffer);
static gboolean gst_nonstream_audio_decoder_src_event(GstPad *pad, GstObject *parent, GstEvent *event);
static gboolean gst_nonstream_audio_decoder_src_query(GstPad *pad, GstObject *parent, GstQuery *query);

static gboolean gst_nonstream_audio_decoder_do_seek(GstNonstreamAudioDecoder *dec, GstEvent *event);

static gboolean gst_nonstream_audio_decoder_sinkpad_activate(GstPad *pad, GstObject *parent);
static gboolean gst_nonstream_audio_decoder_sinkpad_activate_mode(GstPad *pad, GstObject *parent, GstPadMode mode, gboolean active);

static gboolean gst_nonstream_audio_decoder_get_upstream_size(GstNonstreamAudioDecoder *dec, gint64 *length);
static gboolean gst_nonstream_audio_decoder_finish_load(GstNonstreamAudioDecoder *dec, gboolean load_ok, GstClockTime initial_position);
static gboolean gst_nonstream_audio_decoder_load_from_buffer(GstNonstreamAudioDecoder *dec, GstBuffer *buffer);
static gboolean gst_nonstream_audio_decoder_load_from_custom(GstNonstreamAudioDecoder *dec);
static void gst_nonstream_audio_decoder_update_toc(GstNonstreamAudioDecoder *dec, GstNonstreamAudioDecoderClass *klass);
static void gst_nonstream_audio_decoder_loop(GstNonstreamAudioDecoder *dec);
static void gst_nonstream_audio_decoder_update_duration(GstNonstreamAudioDecoder *dec, GstClockTime duration);
static void gst_nonstream_audio_decoder_update_cur_segment(GstNonstreamAudioDecoder *dec, GstClockTime start_position, gboolean set_stop_and_duration);

static gboolean gst_nonstream_audio_decoder_negotiate_default(GstNonstreamAudioDecoder *dec);
static gboolean gst_nonstream_audio_decoder_decide_allocation_default(GstNonstreamAudioDecoder *dec, GstQuery *query);
static gboolean gst_nonstream_audio_decoder_propose_allocation_default(GstNonstreamAudioDecoder *dec, GstQuery *query);

static void gst_nonstream_audio_decoder_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec);
static void gst_nonstream_audio_decoder_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);

static void gst_nonstream_audio_decoder_finalize(GObject *object);

static GstElementClass *gst_nonstream_audio_decoder_parent_class = NULL;




static GType gst_nonstream_audio_decoder_output_mode_get_type(void)
{
	static GType gst_nonstream_audio_decoder_output_mode_type = 0;

	if (!gst_nonstream_audio_decoder_output_mode_type)
	{
		static GEnumValue output_mode_values[] =
		{
			{ GST_NONSTREM_AUDIO_OUTPUT_MODE_LOOPING,              "Looping output",         "looping" },
			{ GST_NONSTREM_AUDIO_OUTPUT_MODE_STEADY,               "Steady output",          "steady"  },
			{ 0, NULL, NULL },
		};

		gst_nonstream_audio_decoder_output_mode_type = g_enum_register_static(
			"NonstreamAudioOutputMode",
			output_mode_values
		);
	}

	return gst_nonstream_audio_decoder_output_mode_type;
}




static char const * get_seek_type_name(GstSeekType seek_type)
{
	switch (seek_type)
	{
		case GST_SEEK_TYPE_NONE: return "none";
		case GST_SEEK_TYPE_SET: return "set";
		case GST_SEEK_TYPE_END: return "end";
		default: return "<unknown>";
	}
}



GType gst_nonstream_audio_decoder_get_type(void)
{
	static volatile gsize nonstream_audio_decoder_type = 0;

	if (g_once_init_enter(&nonstream_audio_decoder_type))
	{
		GType type_;
		static const GTypeInfo nonstream_audio_decoder_info =
		{
			sizeof(GstNonstreamAudioDecoderClass),
			NULL,
			NULL,
			(GClassInitFunc)gst_nonstream_audio_decoder_class_init,
			NULL,
			NULL,
			sizeof(GstNonstreamAudioDecoder),
			0,
			(GInstanceInitFunc)gst_nonstream_audio_decoder_init,
			NULL
		};

		type_ = g_type_register_static(
			GST_TYPE_ELEMENT,
			"GstNonstreamAudioDecoder",
			&nonstream_audio_decoder_info,
			G_TYPE_FLAG_ABSTRACT
		);
		g_once_init_leave(&nonstream_audio_decoder_type, type_);
	}

	return nonstream_audio_decoder_type;
}


static void gst_nonstream_audio_decoder_class_init(GstNonstreamAudioDecoderClass *klass)
{
	GObjectClass *object_class;
	GstElementClass *element_class;
	GstNonstreamAudioDecoderClass *dec_class;

	object_class = G_OBJECT_CLASS(klass);
	element_class = GST_ELEMENT_CLASS(klass);
	dec_class = GST_NONSTREAM_AUDIO_DECODER_CLASS(klass);

	gst_nonstream_audio_decoder_parent_class = g_type_class_peek_parent(klass);

	GST_DEBUG_CATEGORY_INIT(nonstream_audiodecoder_debug, "nonstreamaudiodecoder", 0, "nonstream audio decoder base class");

	object_class->finalize = GST_DEBUG_FUNCPTR(gst_nonstream_audio_decoder_finalize);
	element_class->change_state = GST_DEBUG_FUNCPTR(gst_nonstream_audio_decoder_change_state);
	object_class->set_property = GST_DEBUG_FUNCPTR(gst_nonstream_audio_decoder_set_property);
	object_class->get_property = GST_DEBUG_FUNCPTR(gst_nonstream_audio_decoder_get_property);

	g_object_class_install_property(
		object_class,
		PROP_CURRENT_SUBSONG,
		g_param_spec_uint(
			"current-subsong",
			"Currently active subsong",
			"Subsong that is currently selected for playback",
			0, G_MAXUINT,
			DEFAULT_CURRENT_SUBSONG,
			G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS
		)
	);

	g_object_class_install_property(
		object_class,
		PROP_NUM_LOOPS,
		g_param_spec_int(
			"num-loops",
			"Number of playback loops",
			"Number of times a playback loop shall be executed (special values: 0 = no looping; -1 = infinite loop)",
			-1, G_MAXINT,
			DEFAULT_NUM_LOOPS,
			G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS
		)
	);

	g_object_class_install_property(
		object_class,
		PROP_OUTPUT_MODE,
		g_param_spec_enum(
			"output-mode",
			"Output mode",
			"Which mode playback shall use when a loop is encountered; looping = reset position to start of loop, steady = do not reset position",
			GST_TYPE_NONSTREAM_AUDIO_DECODER_OUTPUT_MODE,
			DEFAULT_OUTPUT_MODE,
			G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS
		)
	);

	dec_class->seek = NULL;
	dec_class->tell = NULL;

	dec_class->load_from_buffer = NULL;
	dec_class->load_from_custom = NULL;

	dec_class->get_current_subsong = NULL;
	dec_class->set_current_subsong = NULL;

	dec_class->get_num_subsongs = NULL;
	dec_class->get_subsong_duration = NULL;
	dec_class->get_subsong_tags = NULL;

	dec_class->set_num_loops = NULL;
	dec_class->get_num_loops = NULL;

	dec_class->decode = NULL;

	dec_class->negotiate = GST_DEBUG_FUNCPTR(gst_nonstream_audio_decoder_negotiate_default);

	dec_class->decide_allocation = GST_DEBUG_FUNCPTR(gst_nonstream_audio_decoder_decide_allocation_default);
	dec_class->propose_allocation = GST_DEBUG_FUNCPTR(gst_nonstream_audio_decoder_propose_allocation_default);

	dec_class->loads_from_sinkpad = TRUE;
}


static void gst_nonstream_audio_decoder_init(GstNonstreamAudioDecoder *dec, GstNonstreamAudioDecoderClass *klass)
{
	GstPadTemplate *pad_template;

	dec->duration = GST_CLOCK_TIME_NONE;
	dec->offset = 0;
	dec->num_decoded = 0;

	dec->initial_subsong = 0;
	dec->toc = NULL;

	dec->loaded = FALSE;

	dec->output_mode = GST_NONSTREM_AUDIO_OUTPUT_MODE_UNDEFINED;

	dec->discont = TRUE;

	dec->allocator = NULL;

	dec->adapter = gst_adapter_new();
	dec->upstream_size = -1;

	g_rec_mutex_init(&(dec->stream_lock));

	gst_audio_info_init(&(dec->audio_info));

	if (klass->loads_from_sinkpad)
	{
		pad_template = gst_element_class_get_pad_template(GST_ELEMENT_CLASS(klass), "sink");
		g_return_if_fail(pad_template != NULL);
		dec->sinkpad = gst_pad_new_from_template(pad_template, "sink");
		gst_pad_set_event_function(dec->sinkpad, GST_DEBUG_FUNCPTR(gst_nonstream_audio_decoder_sink_event));
		gst_pad_set_chain_function(dec->sinkpad, GST_DEBUG_FUNCPTR(gst_nonstream_audio_decoder_chain));
		gst_pad_set_activate_function(dec->sinkpad, GST_DEBUG_FUNCPTR(gst_nonstream_audio_decoder_sinkpad_activate));
		gst_pad_set_activatemode_function(dec->sinkpad, GST_DEBUG_FUNCPTR(gst_nonstream_audio_decoder_sinkpad_activate_mode));
		gst_element_add_pad(GST_ELEMENT(dec), dec->sinkpad);
	}

	pad_template = gst_element_class_get_pad_template(GST_ELEMENT_CLASS(klass), "src");
	g_return_if_fail(pad_template != NULL);
	dec->srcpad = gst_pad_new_from_template(pad_template, "src");
	gst_pad_set_event_function(dec->srcpad, GST_DEBUG_FUNCPTR(gst_nonstream_audio_decoder_src_event));
	gst_pad_set_query_function(dec->srcpad, GST_DEBUG_FUNCPTR(gst_nonstream_audio_decoder_src_query));
	gst_pad_use_fixed_caps(dec->srcpad);	
	gst_element_add_pad(GST_ELEMENT(dec), dec->srcpad);
}


static GstStateChangeReturn gst_nonstream_audio_decoder_change_state(GstElement *element, GstStateChange transition)
{
	GstStateChangeReturn ret;

	ret = GST_ELEMENT_CLASS(gst_nonstream_audio_decoder_parent_class)->change_state(element, transition);

	switch (transition)
	{
		case GST_STATE_CHANGE_READY_TO_PAUSED:
		{
			GstNonstreamAudioDecoder *dec = GST_NONSTREAM_AUDIO_DECODER(element);
			GstNonstreamAudioDecoderClass *dec_class = GST_NONSTREAM_AUDIO_DECODER_GET_CLASS(dec);

			if (!(dec_class->loads_from_sinkpad) && !(dec->loaded))
			{
				g_assert(dec_class->load_from_custom != NULL);

				if (!gst_nonstream_audio_decoder_load_from_custom(dec))
					return GST_STATE_CHANGE_FAILURE;

				if (!gst_pad_start_task(dec->srcpad, (GstTaskFunction)gst_nonstream_audio_decoder_loop, element, NULL))
					return GST_STATE_CHANGE_FAILURE;
			}

			break;
		}

		case GST_STATE_CHANGE_PAUSED_TO_READY:
			break;

		case GST_STATE_CHANGE_READY_TO_NULL:
			break;

		default:
			break;
	}

	return ret;
}


static gboolean gst_nonstream_audio_decoder_sink_event(GstPad *pad, GstObject *parent, GstEvent *event)
{
	GstNonstreamAudioDecoder *dec = GST_NONSTREAM_AUDIO_DECODER(parent);

	switch(GST_EVENT_TYPE (event))
	{
		case GST_EVENT_EOS:
		{
			gsize avail_size;
			GstBuffer *adapter_buffer;

			/* If media has already been loaded, then the decoder task has been started;
			 * the EOS event can be ignored */
			if (dec->loaded)
				return TRUE;

			avail_size = gst_adapter_available(dec->adapter);
			adapter_buffer = gst_adapter_take_buffer(dec->adapter, avail_size);

			if (!gst_nonstream_audio_decoder_load_from_buffer(dec, adapter_buffer))
				return FALSE;

			return gst_pad_start_task(dec->srcpad, (GstTaskFunction)gst_nonstream_audio_decoder_loop, parent, NULL);
		}
		default:
			return gst_pad_event_default(pad, parent, event);
	}
}


/* This function is used when the sink pad activates in push mode */
static GstFlowReturn gst_nonstream_audio_decoder_chain(G_GNUC_UNUSED GstPad *pad, GstObject *parent, GstBuffer *buffer)
{
	GstNonstreamAudioDecoder *dec = GST_NONSTREAM_AUDIO_DECODER(parent);

	if (dec->upstream_size < 0)
	{
		if (!gst_nonstream_audio_decoder_get_upstream_size(dec, &(dec->upstream_size)))
		{
			GST_ELEMENT_ERROR(dec, STREAM, DECODE, (NULL), ("Cannot load - upstream size (in bytes) could not be determined"));
			return GST_FLOW_ERROR;
		}
	}

	if (dec->loaded)
	{
		/* Media is already loaded - discard any incoming buffers, since they are not needed */

		gst_buffer_unref(buffer);
	}
	else
	{
		/* Accumulate data until end-of-stream or the upstream size is reached, then load media and commence playback. */

		gint64 avail_size;

		gst_adapter_push(dec->adapter, buffer);
		avail_size = gst_adapter_available(dec->adapter);
		if (avail_size >= dec->upstream_size)
		{
			GstBuffer *adapter_buffer = gst_adapter_take_buffer(dec->adapter, avail_size);

			if (!gst_nonstream_audio_decoder_load_from_buffer(dec, adapter_buffer))
				return GST_FLOW_ERROR;

			return gst_pad_start_task(dec->srcpad, (GstTaskFunction)gst_nonstream_audio_decoder_loop, parent, NULL) ? GST_FLOW_OK : GST_FLOW_ERROR;
		}
	}

	return GST_FLOW_OK;
}


static gboolean gst_nonstream_audio_decoder_src_event(GstPad *pad, GstObject *parent, GstEvent *event)
{
	gboolean res;
	GstNonstreamAudioDecoder *dec;
	GstNonstreamAudioDecoderClass *dec_class;

	res = FALSE;
	dec = GST_NONSTREAM_AUDIO_DECODER(parent);

	switch (GST_EVENT_TYPE(event))
	{
		case GST_EVENT_SEEK:
			dec_class = GST_NONSTREAM_AUDIO_DECODER_GET_CLASS(dec);
			if (dec_class->seek != NULL)
				res = gst_nonstream_audio_decoder_do_seek(dec, event);
			break;
		default:
			break;
	}

	if (!res)
		res = gst_pad_event_default(pad, parent, event);

	return res;
}


static gboolean gst_nonstream_audio_decoder_src_query(GstPad *pad, GstObject *parent, GstQuery *query)
{
	gboolean res;
	GstNonstreamAudioDecoder *dec;
	GstNonstreamAudioDecoderClass *dec_class;
	GstFormat format;

	res = FALSE;
	dec = GST_NONSTREAM_AUDIO_DECODER(parent);
	dec_class = GST_NONSTREAM_AUDIO_DECODER_GET_CLASS(dec);

	switch (GST_QUERY_TYPE(query))
	{
		case GST_QUERY_DURATION:
		{
			if (!dec->loaded)
			{
				GST_DEBUG_OBJECT(parent, "cannot respond to duration query: nothing is loaded yet");
				break;
			}

			gst_query_parse_duration(query, &format, NULL);
			if ((format == GST_FORMAT_TIME) && (dec->duration != GST_CLOCK_TIME_NONE))
			{
				gst_query_set_duration(query, format, dec->duration);
				res = TRUE;
			}

			break;
		}
		case GST_QUERY_POSITION:
		{
			GST_DEBUG_OBJECT(parent, "position query received");

			if (!dec->loaded)
			{
				GST_DEBUG_OBJECT(parent, "cannot respond to position query: nothing is loaded yet");
				break;
			}

			if (dec_class->tell == NULL)
			{
				GST_DEBUG_OBJECT(parent, "cannot respond to position query: subclass does not have tell() function defined");
				break;
			}

			gst_query_parse_position(query, &format, NULL);
			if (format == GST_FORMAT_TIME)
			{
				GstClockTime pos = dec_class->tell(dec);
				GST_DEBUG_OBJECT(parent, "position query received with format TIME -> reporting position %" GST_TIME_FORMAT, GST_TIME_ARGS(pos));
        			gst_query_set_position(query, format, pos);
				res = TRUE;
			}
			else
			{
				GST_DEBUG_OBJECT(parent, "position query received with unsupported format %s -> not reporting anything", gst_format_get_name(format));
			}

			break;
		}
		case GST_QUERY_SEEKING:
		{
			GstFormat fmt;

			if (!dec->loaded)
			{
				GST_DEBUG_OBJECT(parent, "cannot respond to position query: nothing is loaded yet");
				break;
			}

			if (dec_class->seek == NULL)
			{
				GST_DEBUG_OBJECT(parent, "cannot respond to seeking query: subclass does not have seek() function defined");
				break;
			}

			gst_query_parse_seeking(query, &fmt, NULL, NULL, NULL);

			if (fmt == GST_FORMAT_TIME)
			{
				GST_DEBUG_OBJECT(parent, "seeking query received with format TIME -> can seek: yes");
				gst_query_set_seeking(query, GST_FORMAT_TIME, TRUE, dec->cur_segment.start, dec->cur_segment.stop);
				res = TRUE;
			}
			else
				GST_DEBUG_OBJECT(parent, "seeking query received with unsupported format %s -> can seek: no", gst_format_get_name(format));

			break;
		}
		default:
			break;
	}

	if (!res)
		res = gst_pad_query_default(pad, parent, query);

	return res;
}


static gboolean gst_nonstream_audio_decoder_do_seek(GstNonstreamAudioDecoder *dec, GstEvent *event)
{
	gboolean res;
	gdouble rate;
	GstFormat format;
	GstSeekFlags flags;
	GstSeekType start_type, stop_type;
	gint64 start, stop;
	GstSegment segment;
	gboolean flush;
	GstNonstreamAudioDecoderClass *dec_class;

	if (!dec->loaded)
	{
		GST_DEBUG_OBJECT(dec, "nothing loaded yet - cannot seek");
		return FALSE;
	}
	if (!GST_AUDIO_INFO_IS_VALID(&(dec->audio_info)))
	{
		GST_DEBUG_OBJECT(dec, "no valid audioinfo present - cannot seek");
		return FALSE;
	}

	dec_class = GST_NONSTREAM_AUDIO_DECODER_GET_CLASS(dec);

	GST_DEBUG_OBJECT(dec, "starting seek");

	gst_event_parse_seek(event, &rate, &format, &flags, &start_type, &start, &stop_type, &stop);

	GST_DEBUG_OBJECT(
		dec,
		"seek event data:  "
		"rate %f  format %s  "
		"start type %s  start %" GST_TIME_FORMAT "  "
		"stop type %s  stop %" GST_TIME_FORMAT,
		rate, gst_format_get_name(format),
		get_seek_type_name(start_type), GST_TIME_ARGS(start),
		get_seek_type_name(stop_type), GST_TIME_ARGS(stop)
	);

	if (format != GST_FORMAT_TIME)
	{
		GST_DEBUG_OBJECT(dec, "seeking is only supported in TIME format");
		return FALSE;
	}

	if (rate < 0)
	{
		GST_DEBUG_OBJECT(dec, "only positive seek rates are supported");
		return FALSE;
	}

	flush = ((flags & GST_SEEK_FLAG_FLUSH) == GST_SEEK_FLAG_FLUSH);

	if (flush)
	{
		gst_pad_push_event(dec->srcpad, gst_event_new_flush_start());
	        /* unlock upstream pull_range */
		if (dec_class->loads_from_sinkpad)
		        gst_pad_push_event(dec->sinkpad, gst_event_new_flush_start());
	}
	else
		gst_pad_pause_task(dec->srcpad);

	GST_PAD_STREAM_LOCK(dec->srcpad);

	segment = dec->cur_segment;

	if (!gst_segment_do_seek(
		&segment,
		rate,
		format,
		flags,
		start_type,
		start,
		stop_type,
		stop,
		NULL
	))
	{
    		GST_DEBUG_OBJECT(dec, "could not seek in segment");
		GST_NONSTREAM_AUDIO_DECODER_STREAM_UNLOCK(dec);
		GST_PAD_STREAM_UNLOCK(dec->srcpad);
		return FALSE;
	}

	if ((stop == -1) && (dec->duration > 0))
		stop = dec->duration;

	GST_DEBUG_OBJECT(
		dec,
		"segment data: "
		"seek event data:  "
		"rate %f  applied rate %f  "
		"format %s  "
		"base %" GST_TIME_FORMAT "  "
		"offset %" GST_TIME_FORMAT "  "
		"start %" GST_TIME_FORMAT "  "
		"stop %" GST_TIME_FORMAT "  "
		"time %" GST_TIME_FORMAT "  "
		"position %" GST_TIME_FORMAT "  "
		"duration %" GST_TIME_FORMAT,
		segment.rate, segment.applied_rate,
		gst_format_get_name(segment.format),
		GST_TIME_ARGS(segment.base),
		GST_TIME_ARGS(segment.offset),
		GST_TIME_ARGS(segment.start),
		GST_TIME_ARGS(segment.stop),
		GST_TIME_ARGS(segment.time),
		GST_TIME_ARGS(segment.position),
		GST_TIME_ARGS(segment.duration)
	);

	res = dec_class->seek(dec, segment.position);

	if (res)
	{
		dec->cur_segment = segment;
		dec->offset = gst_util_uint64_scale_int(dec->cur_segment.position, dec->audio_info.rate, GST_SECOND);
		dec->num_decoded = 0;
		dec->discont = TRUE;

		if (flags & GST_SEEK_FLAG_SEGMENT)
		{
			GST_DEBUG_OBJECT (dec, "posting SEGMENT_START message");

			gst_element_post_message(
				GST_ELEMENT(dec),
				gst_message_new_segment_start(
					GST_OBJECT(dec),
					GST_FORMAT_TIME, segment.start
				)
			);
		}
		if (flush)
		{
			gst_pad_push_event(dec->srcpad, gst_event_new_flush_stop(TRUE));
			if (dec_class->loads_from_sinkpad)
				gst_pad_push_event(dec->sinkpad, gst_event_new_flush_stop(TRUE));
		}

		gst_pad_push_event(dec->srcpad, gst_event_new_segment(&segment));

		GST_INFO_OBJECT(dec, "seek succeeded");

		gst_pad_start_task(dec->srcpad, (GstTaskFunction)gst_nonstream_audio_decoder_loop, dec, NULL);
	}
	else
	{
		GST_WARNING_OBJECT(dec, "seek failed");
	}


	GST_PAD_STREAM_UNLOCK(dec->srcpad);
	gst_event_unref(event);

	return res;
}


static gboolean gst_nonstream_audio_decoder_sinkpad_activate(GstPad *pad, G_GNUC_UNUSED GstObject *parent)
{
	GstQuery *query;
	gboolean do_activate_pull;

	query = gst_query_new_scheduling();

	if (gst_pad_peer_query(pad, query))
		do_activate_pull = gst_query_has_scheduling_mode_with_flags(query, GST_PAD_MODE_PULL, GST_SCHEDULING_FLAG_SEEKABLE);
	else
		do_activate_pull = FALSE;

	gst_query_unref(query);

	GST_DEBUG_OBJECT(pad, "activating %s", do_activate_pull ? "pull" : "push");
	return gst_pad_activate_mode(pad, do_activate_pull ? GST_PAD_MODE_PULL : GST_PAD_MODE_PUSH, TRUE);
}


static gboolean gst_nonstream_audio_decoder_sinkpad_activate_mode(G_GNUC_UNUSED GstPad *pad, GstObject *parent, GstPadMode mode, gboolean active)
{
	gboolean res;
	GstNonstreamAudioDecoder *dec = GST_NONSTREAM_AUDIO_DECODER(parent);

	switch (mode)
	{
		case GST_PAD_MODE_PUSH:
			res = TRUE;
			if (!active)
				res = gst_pad_stop_task(dec->srcpad);
			/* the case active == TRUE is handled by the chain and sink_event function */
			break;
		case GST_PAD_MODE_PULL:
			if (active)
				res = gst_pad_start_task(dec->srcpad, (GstTaskFunction)gst_nonstream_audio_decoder_loop, parent, NULL);
			else
				res = gst_pad_stop_task(dec->srcpad);
			break;
		default:
			res = FALSE;
			break;
	}

	return res;
}


static gboolean gst_nonstream_audio_decoder_get_upstream_size(GstNonstreamAudioDecoder *dec, gint64 *length)
{
	return gst_pad_peer_query_duration(dec->sinkpad, GST_FORMAT_BYTES, length) && (*length >= 0);
}


static gboolean gst_nonstream_audio_decoder_finish_load(GstNonstreamAudioDecoder *dec, gboolean load_ok, GstClockTime initial_position)
{
	GstNonstreamAudioDecoderClass *dec_class;
	GstClockTime duration;

	dec_class = GST_NONSTREAM_AUDIO_DECODER_CLASS(G_OBJECT_GET_CLASS(dec));

	if (!load_ok)
	{
		GST_ELEMENT_ERROR(dec, STREAM, DECODE, (NULL), ("Loading failed"));
		return FALSE;
	}

	if (!GST_AUDIO_INFO_IS_VALID(&(dec->audio_info)))
	{
		GST_ELEMENT_ERROR(dec, STREAM, DECODE, (NULL), ("Audio info is invalid after loading"));
		return FALSE;
	}

	if (dec_class->get_current_subsong != NULL)
		dec->initial_subsong = dec_class->get_current_subsong(dec);

	if (dec_class->get_subsong_duration != NULL)
	{
		duration = dec_class->get_subsong_duration(dec, dec->initial_subsong);
		gst_nonstream_audio_decoder_update_duration(dec, duration);
	}
	else
		duration = GST_CLOCK_TIME_NONE;

	if (dec_class->get_subsong_tags != NULL)
	{
		GstTagList *tags = dec_class->get_subsong_tags(dec, dec->initial_subsong);
		if (tags != NULL)
			gst_pad_push_event(dec->srcpad, gst_event_new_tag(tags));
	}

	gst_nonstream_audio_decoder_update_toc(dec, dec_class);

	if (!gst_nonstream_audio_decoder_negotiate(dec))
	{
		GST_ERROR_OBJECT(dec, "negotiation failed - aborting load");
		return FALSE;
	}

	gst_segment_init(&(dec->cur_segment), GST_FORMAT_TIME);
	gst_nonstream_audio_decoder_update_cur_segment(dec, initial_position, TRUE);

	dec->loaded = TRUE;

	return TRUE;
}


static gboolean gst_nonstream_audio_decoder_load_from_buffer(GstNonstreamAudioDecoder *dec, GstBuffer *buffer)
{
	gboolean load_ok;
	GstClockTime initial_position;
	GstNonstreamAudioDecoderClass *dec_class;

	GST_LOG_OBJECT(dec, "Read %" G_GSIZE_FORMAT " bytes from upstream", gst_buffer_get_size(buffer));

	dec_class = GST_NONSTREAM_AUDIO_DECODER_CLASS(G_OBJECT_GET_CLASS(dec));

	initial_position = 0;
	load_ok = dec_class->load_from_buffer(dec, buffer, dec->initial_subsong, &initial_position, &(dec->output_mode));
	gst_buffer_unref(buffer);

	return gst_nonstream_audio_decoder_finish_load(dec, load_ok, initial_position);
}


static gboolean gst_nonstream_audio_decoder_load_from_custom(GstNonstreamAudioDecoder *dec)
{
	gboolean load_ok;
	GstClockTime initial_position;
	GstNonstreamAudioDecoderClass *dec_class;

	GST_LOG_OBJECT(dec, "Reading song from custom source defined by derived class");

	dec_class = GST_NONSTREAM_AUDIO_DECODER_CLASS(G_OBJECT_GET_CLASS(dec));

	initial_position = 0;
	load_ok = dec_class->load_from_custom(dec, dec->initial_subsong, &initial_position, &(dec->output_mode));

	return gst_nonstream_audio_decoder_finish_load(dec, load_ok, initial_position);
}


static void gst_nonstream_audio_decoder_update_toc(GstNonstreamAudioDecoder *dec, GstNonstreamAudioDecoderClass *klass)
{
	guint num_subsongs, i;
	gboolean update = FALSE;

	if (dec->toc != NULL)
	{
		gst_toc_unref(dec->toc);
		dec->toc = NULL;
		update = TRUE;
	}

	if ((klass->get_num_subsongs == NULL) || ((num_subsongs = klass->get_num_subsongs(dec)) <= 1))
		return;

	dec->toc = gst_toc_new(GST_TOC_SCOPE_GLOBAL);

	for (i = 0; i < num_subsongs; ++i)
	{
		gchar *uid;
		GstTocEntry *entry;
		GstClockTime duration;
		GstTagList *tags;
		
		uid = g_strdup_printf("%u", i);
		entry = gst_toc_entry_new(GST_TOC_ENTRY_TYPE_TITLE, uid);

		/* TODO: combine this with looping */
		duration = (klass->get_subsong_duration != NULL) ? klass->get_subsong_duration(dec, i) : GST_CLOCK_TIME_NONE;
		tags = (klass->get_subsong_tags != NULL) ? klass->get_subsong_tags(dec, i) : (GstTagList*)NULL;

		/* TOC does not allow GST_CLOCK_TIME_NONE as a stop value */
		if (duration == GST_CLOCK_TIME_NONE)
			duration = G_MAXINT64;

		gst_toc_entry_set_start_stop_times(entry, 0, duration);
		gst_toc_entry_set_tags(entry, tags);

		GST_DEBUG_OBJECT(
			dec,
			"new toc entry: uid: \"%s\" duration: %" GST_TIME_FORMAT " tags: %" GST_PTR_FORMAT,
			uid,
			GST_TIME_ARGS(duration),
			(gpointer)tags
		);

		gst_toc_append_entry(dec->toc, entry);

		g_free(uid);
	}

	gst_pad_push_event(dec->srcpad, gst_event_new_toc(dec->toc, update));
	gst_message_new_toc(GST_OBJECT(dec), dec->toc, update);
}


/* NOTE: not to be confused with song loops - this function is the looping srcpad task
 * There are four possibilities for when this task is started:
 * 1. sinkpad runs in push mode, and the chain function starts this task after it loaded the media
 * 2. sinkpad is activated in pull mode
 * 3. EOS event is reached
 * 4. A seek event occurs
 */
static void gst_nonstream_audio_decoder_loop(GstNonstreamAudioDecoder *dec)
{
	GstNonstreamAudioDecoderClass *dec_class;
	dec_class = GST_NONSTREAM_AUDIO_DECODER_CLASS(G_OBJECT_GET_CLASS(dec));

	GST_NONSTREAM_AUDIO_DECODER_STREAM_LOCK(dec);

	if (!dec->loaded)
	{
		g_assert(dec_class->loads_from_sinkpad);

		/* This branch is only reached in pull mode; in push mode, the media is loaded
		 * first (inside the chain function), and then this task is started, so
		 * it cannot reach this branch then */

		gint64 size;
		GstBuffer *buffer;
		GstFlowReturn flow;

		g_assert(dec_class->load_from_buffer != NULL);

		if (!gst_nonstream_audio_decoder_get_upstream_size(dec, &size))
		{
			GST_ELEMENT_ERROR(dec, STREAM, DECODE, (NULL), ("Cannot load - upstream size (in bytes) could not be determined"));
			goto pause;
		}

		buffer = NULL;
		flow = gst_pad_pull_range(dec->sinkpad, 0, size, &buffer);
		if (flow != GST_FLOW_OK)
		{
			GST_ELEMENT_ERROR(dec, STREAM, DECODE, (NULL), ("Cannot load - pulling data from upstream failed (flow error: %s)", gst_flow_get_name(flow)));
			goto pause;
		}

		if (!gst_nonstream_audio_decoder_load_from_buffer(dec, buffer))
			goto pause;
	}

	/* loading is done at this point -> send samples downstream */
	{
		GstFlowReturn flow;
		GstBuffer *outbuf;
		guint num_samples;

		g_assert(dec_class->decode != NULL);

		if (dec_class->decode(dec, &outbuf, &num_samples))
		{
			g_assert(outbuf != NULL);

			if (G_UNLIKELY(
				dec->output_format_changed ||
				(GST_AUDIO_INFO_IS_VALID(&(dec->audio_info)) && gst_pad_check_reconfigure(dec->srcpad))
			))
			{
				if (!gst_nonstream_audio_decoder_negotiate(dec))
				{
					gst_buffer_unref(outbuf);
					GST_LOG_OBJECT(dec, "could not push output buffer: negotiation failed");
					goto pause_unlock;
				}
			}

			GST_BUFFER_DURATION(outbuf)  = gst_util_uint64_scale_int(num_samples, GST_SECOND, dec->audio_info.rate);
			GST_BUFFER_OFFSET(outbuf)    = dec->offset;
			GST_BUFFER_PTS(outbuf)       = gst_util_uint64_scale_int(dec->offset, GST_SECOND, dec->audio_info.rate);
			GST_BUFFER_DTS(outbuf)       = GST_BUFFER_PTS(outbuf);

			GST_LOG_OBJECT(
				dec,
				"output buffer stats: num_samples = %u  duration = %" GST_TIME_FORMAT "  offset = %" G_GUINT64_FORMAT "  timestamp = %" GST_TIME_FORMAT,
				num_samples,
				GST_TIME_ARGS(GST_BUFFER_DURATION(outbuf)),
				dec->offset,
				GST_TIME_ARGS(GST_BUFFER_TIMESTAMP(outbuf))
			);

			if (G_UNLIKELY(dec->discont))
			{
				GST_BUFFER_FLAG_SET(outbuf, GST_BUFFER_FLAG_DISCONT);
				dec->discont = FALSE;
			}

			dec->offset += num_samples;
			dec->num_decoded += num_samples;

			flow = gst_pad_push(dec->srcpad, outbuf);

			if (flow != GST_FLOW_OK)
			{
				/* no need to unref buffer - gst_pad_push() does it in all cases (success and failure) */
				GST_LOG_OBJECT(dec, "flow error when pushing output buffer: %s", gst_flow_get_name(flow));
				goto pause_unlock;
			}
		}
		else
		{
			GST_INFO_OBJECT(dec, "decode() reports end -> sending EOS event");
			gst_pad_push_event(dec->srcpad, gst_event_new_eos());
			goto pause;
		}
	}

	GST_NONSTREAM_AUDIO_DECODER_STREAM_UNLOCK(dec);

	return;

pause:
	GST_INFO_OBJECT(dec, "pausing task");
	/* NOT using stop_task here, since that would cause a deadlock */
	gst_pad_pause_task(dec->srcpad);
	return;
pause_unlock:
	GST_NONSTREAM_AUDIO_DECODER_STREAM_UNLOCK(dec);
	goto pause;
}


static gboolean gst_nonstream_audio_decoder_negotiate_default(GstNonstreamAudioDecoder *dec)
{
	GstCaps *caps;
	GstNonstreamAudioDecoderClass *dec_class;
	gboolean res = TRUE;
	GstQuery *query = NULL;
	GstAllocator *allocator;
	GstAllocationParams allocation_params;

	g_return_val_if_fail(GST_IS_NONSTREAM_AUDIO_DECODER(dec), FALSE);
	g_return_val_if_fail(GST_AUDIO_INFO_IS_VALID(&(dec->audio_info)), FALSE);

	dec_class = GST_NONSTREAM_AUDIO_DECODER_CLASS(G_OBJECT_GET_CLASS(dec));

	caps = gst_audio_info_to_caps(&(dec->audio_info));

	GST_DEBUG_OBJECT(dec, "setting src caps %" GST_PTR_FORMAT, (gpointer)caps);

	res = gst_pad_push_event(dec->srcpad, gst_event_new_caps(caps));

	if (!res)
		goto done;

	dec->output_format_changed = FALSE;

	query = gst_query_new_allocation(caps, TRUE);
	if (!gst_pad_peer_query(dec->srcpad, query))
	{
		GST_DEBUG_OBJECT (dec, "didn't get downstream ALLOCATION hints");
	}

	g_assert(dec_class->decide_allocation != NULL);
	res = dec_class->decide_allocation (dec, query);

	GST_DEBUG_OBJECT(dec, "ALLOCATION (%d) params: %" GST_PTR_FORMAT, res, (gpointer)query);

	if (!res)
		goto no_decide_allocation;

	/* we got configuration from our peer or the decide_allocation method,
	 * parse them */
	if (gst_query_get_n_allocation_params(query) > 0)
	{
		gst_query_parse_nth_allocation_param(query, 0, &allocator, &allocation_params);
	}
	else
	{
		allocator = NULL;
		gst_allocation_params_init(&allocation_params);
	}

	if (dec->allocator != NULL)
		gst_object_unref(dec->allocator);
	dec->allocator = allocator;
	dec->allocation_params = allocation_params;

done:
	if (query != NULL)
		gst_query_unref(query);
	gst_caps_unref(caps);

	return res;

no_decide_allocation:
	{
		GST_WARNING_OBJECT(dec, "subclass failed to decide allocation");
		goto done;
	}
}


static gboolean gst_nonstream_audio_decoder_decide_allocation_default(G_GNUC_UNUSED GstNonstreamAudioDecoder *dec, GstQuery *query)
{
	GstAllocator *allocator = NULL;
	GstAllocationParams params;
	gboolean update_allocator;

	/* we got configuration from our peer or the decide_allocation method,
	 * parse them */
	if (gst_query_get_n_allocation_params(query) > 0)
	{
		/* try the allocator */
		gst_query_parse_nth_allocation_param(query, 0, &allocator, &params);
		update_allocator = TRUE;
	}
	else
	{
		allocator = NULL;
		gst_allocation_params_init(&params);
		update_allocator = FALSE;
	}

	if (update_allocator)
		gst_query_set_nth_allocation_param(query, 0, allocator, &params);
	else
		gst_query_add_allocation_param(query, allocator, &params);
	if (allocator)
		gst_object_unref(allocator);

	return TRUE;
}


static gboolean gst_nonstream_audio_decoder_propose_allocation_default(G_GNUC_UNUSED GstNonstreamAudioDecoder *dec, G_GNUC_UNUSED GstQuery *query)
{
	return TRUE;
}


static void gst_nonstream_audio_decoder_set_property(GObject *object, guint prop_id, G_GNUC_UNUSED const GValue *value, GParamSpec *pspec)
{
 	GstNonstreamAudioDecoder *dec = GST_NONSTREAM_AUDIO_DECODER(object);
	GstNonstreamAudioDecoderClass *klass = GST_NONSTREAM_AUDIO_DECODER_GET_CLASS(dec);

	switch (prop_id)
	{
		case PROP_OUTPUT_MODE:
		{
			GstNonstreamAudioOutputMode new_output_mode;
			new_output_mode = g_value_get_enum(value);

			g_assert(klass->get_supported_output_modes);

			if ((klass->get_supported_output_modes(dec) & (1u << new_output_mode)) == 0)
			{
				GST_WARNING_OBJECT(dec, "could not set output mode to %s (not supported by subclass)", (new_output_mode == GST_NONSTREM_AUDIO_OUTPUT_MODE_STEADY) ? "steady" : "looping");
				break;
			}

			GST_NONSTREAM_AUDIO_DECODER_STREAM_LOCK(dec);
			if (new_output_mode != dec->output_mode)
			{
				if (dec->loaded)
				{
					GstClockTime cur_position;
					gboolean proceed = TRUE;

					if (klass->set_output_mode != NULL)
					{
						if (klass->set_output_mode(dec, new_output_mode, &cur_position))
							proceed = TRUE;
						else
						{
							proceed = FALSE;
							GST_WARNING_OBJECT(dec, "switching to new output mode failed");
						}
					}

					if (proceed)
					{
						gst_nonstream_audio_decoder_update_cur_segment(dec, cur_position, TRUE);
						dec->output_mode = new_output_mode;
					}
				}
				else
					dec->output_mode = new_output_mode;
			}
			GST_NONSTREAM_AUDIO_DECODER_STREAM_UNLOCK(dec);
			break;
		}
		case PROP_CURRENT_SUBSONG:
		{
			guint new_subsong;
			GST_NONSTREAM_AUDIO_DECODER_STREAM_LOCK(dec);
			new_subsong = g_value_get_uint(value);
			if (dec->loaded)
			{
				if (klass->set_current_subsong != NULL)
				{
					GstClockTime new_position;
					if (klass->set_current_subsong(dec, new_subsong, &new_position))
					{
						if (klass->get_subsong_duration != NULL)
						{
							GstClockTime duration = klass->get_subsong_duration(dec, new_subsong);
							gst_nonstream_audio_decoder_update_duration(dec, duration);
						}

						gst_nonstream_audio_decoder_update_cur_segment(dec, new_position, TRUE);

						if (klass->get_subsong_tags != NULL)
						{
							GstTagList *tags = klass->get_subsong_tags(dec, new_subsong);
							if (tags != NULL)
								gst_pad_push_event(dec->srcpad, gst_event_new_tag(tags));
						}
					}
					else
						GST_WARNING_OBJECT(dec, "switching to new subsong %u failed", new_subsong);
				}
				else
					GST_INFO_OBJECT(dec, "cannot set current subsong - set_current_subsong is NULL");
			}
			else
			{
				GST_INFO_OBJECT(dec, "setting initial subsong to %u", new_subsong);
				dec->initial_subsong = new_subsong;
			}
			GST_NONSTREAM_AUDIO_DECODER_STREAM_UNLOCK(dec);
			break;
		}
		case PROP_NUM_LOOPS:
		{
			gint new_num_loops = g_value_get_int(value);

			GST_NONSTREAM_AUDIO_DECODER_STREAM_LOCK(dec);
			if (klass->set_num_loops != NULL)
			{
				if (!(klass->set_num_loops(dec, new_num_loops)))
					GST_WARNING_OBJECT(dec, "setting number of loops to %u failed", new_num_loops);
			}
			else
				GST_INFO_OBJECT(dec, "cannot set number of loops - set_num_loops is NULL");
			GST_NONSTREAM_AUDIO_DECODER_STREAM_UNLOCK(dec);
			break;
		}
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
			break;
	}
}


static void gst_nonstream_audio_decoder_get_property(GObject *object, guint prop_id, G_GNUC_UNUSED GValue *value, GParamSpec *pspec)
{
 	GstNonstreamAudioDecoder *dec = GST_NONSTREAM_AUDIO_DECODER(object);
	GstNonstreamAudioDecoderClass *klass = GST_NONSTREAM_AUDIO_DECODER_GET_CLASS(dec);

	switch (prop_id)
	{
		case PROP_OUTPUT_MODE:
			g_value_set_enum(value, dec->output_mode);
			break;
		case PROP_CURRENT_SUBSONG:
		{
			GST_NONSTREAM_AUDIO_DECODER_STREAM_LOCK(dec);
			if (klass->get_current_subsong != NULL)
				g_value_set_uint(value, klass->get_current_subsong(dec));
			else
			{
				GST_INFO_OBJECT(dec, "cannot get current subsong - get_current_subsong is NULL -> returning 0 as subsong index");
				g_value_set_uint(value, 0);
			}
			GST_NONSTREAM_AUDIO_DECODER_STREAM_UNLOCK(dec);
			break;
		}
		case PROP_NUM_LOOPS:
		{
			GST_NONSTREAM_AUDIO_DECODER_STREAM_LOCK(dec);
			if (klass->get_num_loops != NULL)
				g_value_set_int(value, klass->get_num_loops(dec));
			else
			{
				GST_INFO_OBJECT(dec, "cannot get number of loops - get_num_loops is NULL -> returning 0 as number of loops");
				g_value_set_int(value, 0);
			}
			GST_NONSTREAM_AUDIO_DECODER_STREAM_UNLOCK(dec);
			break;
		}
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
			break;
	}
}


static void gst_nonstream_audio_decoder_finalize(GObject *object)
{
	GstNonstreamAudioDecoder *dec;
	
	g_return_if_fail(GST_IS_NONSTREAM_AUDIO_DECODER(object));
	dec = GST_NONSTREAM_AUDIO_DECODER(object);

	g_object_unref(dec->adapter);

	g_rec_mutex_clear(&(dec->stream_lock));

	if (dec->toc != NULL)
		gst_toc_unref(dec->toc);

	G_OBJECT_CLASS(gst_nonstream_audio_decoder_parent_class)->finalize(object);
}


static void gst_nonstream_audio_decoder_update_duration(GstNonstreamAudioDecoder *dec, GstClockTime duration)
{
	GstTagList *tags;

	tags = gst_tag_list_new_empty();
	gst_tag_list_add(tags, GST_TAG_MERGE_REPLACE, GST_TAG_DURATION, duration, NULL);
	gst_pad_push_event(dec->srcpad, gst_event_new_tag(tags));
	dec->duration = duration;

	gst_element_post_message(GST_ELEMENT(dec), gst_message_new_duration_changed(GST_OBJECT(dec)));
}


static void gst_nonstream_audio_decoder_update_cur_segment(GstNonstreamAudioDecoder *dec, GstClockTime start_position, gboolean set_stop_and_duration)
{
	dec->cur_segment.base = gst_util_uint64_scale_int(dec->num_decoded, GST_SECOND, dec->audio_info.rate);
	dec->cur_segment.start = start_position;
	dec->cur_segment.time = start_position;
	dec->offset = gst_util_uint64_scale_int(start_position, dec->audio_info.rate, GST_SECOND);
	if (set_stop_and_duration)
	{
		gboolean open_ended = (dec->output_mode == GST_NONSTREM_AUDIO_OUTPUT_MODE_STEADY);
		dec->cur_segment.stop = open_ended ? GST_CLOCK_TIME_NONE : dec->duration;
		dec->cur_segment.duration = open_ended ? GST_CLOCK_TIME_NONE : dec->duration;
	}

	gst_pad_push_event(dec->srcpad, gst_event_new_segment(&(dec->cur_segment)));	
}


void gst_nonstream_audio_decoder_handle_loop(GstNonstreamAudioDecoder *dec, GstClockTime new_position)
{
	/* NOTE: handle_loop must be called AFTER the last samples of the loop have been decoded and pushed downstream */

	GstNonstreamAudioDecoderClass *klass = GST_NONSTREAM_AUDIO_DECODER_GET_CLASS(dec);
	if (dec->output_mode == GST_NONSTREM_AUDIO_OUTPUT_MODE_STEADY)
	{
		/* handle_loop makes no sense with open-ended decoders */
		GST_WARNING_OBJECT(dec, "ignoring handle_loop() call, since the decoder output mode is \"steady\"");
		return;
	}

	g_return_if_fail(GST_IS_NONSTREAM_AUDIO_DECODER_CLASS(klass));

	dec->discont = TRUE;

	GST_DEBUG_OBJECT(dec, "handle_loop() invoked with new_position = %" GST_TIME_FORMAT, GST_TIME_ARGS(new_position));

	GST_NONSTREAM_AUDIO_DECODER_STREAM_LOCK(dec);
	gst_nonstream_audio_decoder_update_cur_segment(dec, new_position, FALSE);
	GST_NONSTREAM_AUDIO_DECODER_STREAM_UNLOCK(dec);
}


gboolean gst_nonstream_audio_decoder_set_output_audioinfo(GstNonstreamAudioDecoder *dec, GstAudioInfo const *audio_info)
{
	GstCaps *caps;
	GstCaps *templ_caps;
	gboolean caps_ok;
	gboolean res = TRUE;

	g_return_val_if_fail(GST_IS_NONSTREAM_AUDIO_DECODER(dec), FALSE);

	GST_NONSTREAM_AUDIO_DECODER_STREAM_LOCK(dec);

	caps = gst_audio_info_to_caps(audio_info);
	if (caps == NULL)
	{
		GST_WARNING_OBJECT(dec, "Could not create caps out of audio info");
		res = FALSE;
		goto done;
	}

	templ_caps = gst_pad_get_pad_template_caps(dec->srcpad);
	caps_ok = gst_caps_is_subset(caps, templ_caps);

	if (caps_ok)
	{
		dec->audio_info = *audio_info;
		dec->output_format_changed = TRUE;

		GST_INFO_OBJECT(dec, "setting output format to %" GST_PTR_FORMAT, (gpointer)caps);
	}
	else
	{
		GST_WARNING_OBJECT(
			dec,
			"requested output format %" GST_PTR_FORMAT " do not match template %" GST_PTR_FORMAT,
			(gpointer)caps, (gpointer)templ_caps
		);

		res = FALSE;
	}

	gst_caps_unref(caps);
	gst_caps_unref(templ_caps);

done:
	GST_NONSTREAM_AUDIO_DECODER_STREAM_UNLOCK(dec);
	return res;
}


gboolean gst_nonstream_audio_decoder_set_output_audioinfo_simple(GstNonstreamAudioDecoder *dec, guint sample_rate, GstAudioFormat sample_format, guint num_channels)
{
	GstAudioInfo audio_info;

	gst_audio_info_init(&audio_info);

	gst_audio_info_set_format(
		&audio_info,
		sample_format,
		sample_rate,
		num_channels,
		NULL
	);

	return gst_nonstream_audio_decoder_set_output_audioinfo(dec, &audio_info);
}


gboolean gst_nonstream_audio_decoder_negotiate(GstNonstreamAudioDecoder *dec)
{
	GstNonstreamAudioDecoderClass *klass;
	gboolean res = TRUE;

	g_return_val_if_fail(GST_IS_NONSTREAM_AUDIO_DECODER(dec), FALSE);

	klass = GST_NONSTREAM_AUDIO_DECODER_GET_CLASS(dec);

	GST_NONSTREAM_AUDIO_DECODER_STREAM_LOCK(dec);
	if (klass->negotiate != NULL)
		res = klass->negotiate(dec);
	GST_NONSTREAM_AUDIO_DECODER_STREAM_UNLOCK(dec);

	return res;
}


void gst_nonstream_audio_decoder_get_downstream_info(GstNonstreamAudioDecoder *dec, GstAudioFormat *format, gint *sample_rate, gint *num_channels)
{
	GstCaps *allowed_srccaps;
	guint structure_nr, num_structures;
	gboolean ds_format_found, ds_rate_found, ds_channels_found;

	g_return_if_fail(GST_IS_NONSTREAM_AUDIO_DECODER(dec));

	/* Get the caps that are allowed by downstream */
	{
		GstCaps *allowed_srccaps_unnorm = gst_pad_get_allowed_caps(dec->srcpad);
		allowed_srccaps = gst_caps_normalize(allowed_srccaps_unnorm);
	}

	ds_format_found = FALSE;
	ds_rate_found = FALSE;
	ds_channels_found = FALSE;

	/* Go through all allowed caps, see if one of them has sample rate or number of channels set (or both) */
	num_structures = gst_caps_get_size(allowed_srccaps);
	GST_DEBUG_OBJECT(dec, "%u structure(s) in downstream caps", num_structures);
	for (structure_nr = 0; structure_nr < num_structures; ++structure_nr)
	{
		GstStructure *structure;
		gchar const *format_str;

		ds_rate_found = FALSE;
		ds_channels_found = FALSE;

		structure = gst_caps_get_structure(allowed_srccaps, structure_nr);

		if ((format != NULL) && (format_str = gst_structure_get_string(structure, "format")))
		{
			GstAudioFormat fmt = gst_audio_format_from_string(format_str);
			if (fmt == GST_AUDIO_FORMAT_UNKNOWN)
				GST_WARNING_OBJECT(dec, "caps structure %" GST_PTR_FORMAT " does not contain a valid format", (gpointer)structure);
			else
			{
				GST_DEBUG_OBJECT(dec, "got format from structure #%u : %s", structure_nr, format_str);
				ds_format_found = TRUE;
			}
		}
		if ((sample_rate != NULL) && gst_structure_get_int(structure, "rate", sample_rate))
		{
			GST_DEBUG_OBJECT(dec, "got sample rate from structure #%u : %d Hz", structure_nr, *sample_rate);
			ds_rate_found = TRUE;
		}
		if ((num_channels != NULL) && gst_structure_get_int(structure, "channels", num_channels))
		{
			GST_DEBUG_OBJECT(dec, "got number of channels from structure #%u : %u channels", structure_nr, *num_channels);
			ds_channels_found = TRUE;
		}

		if (ds_format_found || ds_rate_found || ds_channels_found)
			break;
	}

	gst_caps_unref(allowed_srccaps);

	if ((format != NULL) && !ds_format_found)
		GST_INFO_OBJECT(dec, "downstream did not specify format - using default (%s)", gst_audio_format_to_string(*format));
	if ((sample_rate != NULL) && !ds_rate_found)
		GST_INFO_OBJECT(dec, "downstream did not specify sample rate - using default (%d Hz)", *sample_rate);
	if ((num_channels != NULL) && !ds_channels_found)
		GST_INFO_OBJECT(dec, "downstream did not specify number of channels - using default (%d channels)", *num_channels);

	
}


GstBuffer * gst_nonstream_audio_decoder_allocate_output_buffer(GstNonstreamAudioDecoder *dec, gsize size)
{
	GstBuffer *buffer = NULL;

	GST_NONSTREAM_AUDIO_DECODER_STREAM_LOCK(dec);

	if (G_UNLIKELY(
		dec->output_format_changed ||
		(GST_AUDIO_INFO_IS_VALID(&(dec->audio_info)) && gst_pad_check_reconfigure(dec->srcpad))
	))
	{
		if (!gst_nonstream_audio_decoder_negotiate(dec))
			goto done;
	}

	buffer = gst_buffer_new_allocate(dec->allocator, size, &(dec->allocation_params));

done:
	GST_NONSTREAM_AUDIO_DECODER_STREAM_UNLOCK(dec);
	return buffer;
}

