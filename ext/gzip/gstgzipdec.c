#ifdef HAVE_CONFIG_H
 #include <config.h>
#endif

#include <string.h>
#include <gst/gst.h>
#include <gst/base/gstadapter.h>
#include <gst/base/gsttypefindhelper.h>
#include <zlib.h>

#include "gstgzipdec.h"


GST_DEBUG_CATEGORY_STATIC(gzipdec_debug);
#define GST_CAT_DEFAULT gzipdec_debug


#define GST_GZIP_DEC_BUFFER_SIZE (64 * 1024)


static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE(
	"sink",
	GST_PAD_SINK,
	GST_PAD_ALWAYS,
	GST_STATIC_CAPS("application/x-gzip")
);

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE(
	"src",
	GST_PAD_SRC,
	GST_PAD_ALWAYS,
	GST_STATIC_CAPS_ANY
);



G_DEFINE_TYPE(GstGZipDec, gst_gzipdec, GST_TYPE_ELEMENT)


static void gst_gzipdec_finalize(GObject *object);
static GstStateChangeReturn gst_gzipdec_change_state(GstElement *element, GstStateChange transition);
static gboolean gst_gzipdec_sink_event(GstPad *pad, GstObject *parent, GstEvent *event);
static GstFlowReturn gst_gzipdec_chain(GstPad *pad, GstObject *parent, GstBuffer *gz_data);
static gboolean gst_gzipdec_src_query(GstPad *pad, GstObject *parent, GstQuery *query);

static GstCaps* gst_gzipdec_do_typefinding_from_adapter(GstGZipDec *gzipdec, GstAdapter *adapter);
static GstCaps* gst_gzipdec_do_typefinding_from_buffer(GstGZipDec *gzipdec, GstBuffer *buffer);

static gboolean gst_gzipdec_init_inflate(GstGZipDec *gzipdec);
static gboolean gst_gzipdec_shutdown_inflate(GstGZipDec *gzipdec);
static gboolean gst_gzipdec_inflate_buffer(GstGZipDec *gzipdec, GstBuffer *in_buffer, GstBuffer **out_buffer, gboolean *eos);



void gst_gzipdec_class_init(GstGZipDecClass *klass)
{
	GObjectClass *object_class;
	GstElementClass *element_class;

	GST_DEBUG_CATEGORY_INIT(gzipdec_debug, "gzipdec", 0, "zzip decompressor");

	object_class = G_OBJECT_CLASS(klass);
	element_class = GST_ELEMENT_CLASS(klass);

	object_class->finalize = GST_DEBUG_FUNCPTR(gst_gzipdec_finalize);
	element_class->change_state = GST_DEBUG_FUNCPTR(gst_gzipdec_change_state);

	gst_element_class_add_pad_template(element_class, gst_static_pad_template_get(&sink_template));
	gst_element_class_add_pad_template(element_class, gst_static_pad_template_get(&src_template));

	gst_element_class_set_static_metadata(
		element_class,
		"Gzip decompressor",
		"Codec/Demuxer",
		"Uncompresses Gzip-compressed content and sends the decompressed bytes downstream",
		"Carlos Rafael Giani <dv@pseudoterminal.org>"
	);
}


void gst_gzipdec_init(GstGZipDec *gzipdec)
{
	gzipdec->adapter = gst_adapter_new();
	gzipdec->typefind_done = FALSE;
	gzipdec->compressed_size_requested = FALSE;
	gzipdec->compressed_size_known = FALSE;
	gzipdec->compressed_size = 0;
	gzipdec->uncompressed_size = 0;

	gzipdec->sinkpad = gst_pad_new_from_static_template(&sink_template, "sink");
	gst_pad_set_event_function(gzipdec->sinkpad, GST_DEBUG_FUNCPTR(gst_gzipdec_sink_event));
	gst_pad_set_chain_function(gzipdec->sinkpad, GST_DEBUG_FUNCPTR(gst_gzipdec_chain));
	gst_element_add_pad(GST_ELEMENT(gzipdec), gzipdec->sinkpad);

	gzipdec->srcpad = gst_pad_new_from_static_template(&src_template, "src");
	gst_pad_set_query_function(gzipdec->srcpad, GST_DEBUG_FUNCPTR(gst_gzipdec_src_query));
	gst_pad_use_fixed_caps(gzipdec->srcpad);
	gst_element_add_pad(GST_ELEMENT(gzipdec), gzipdec->srcpad);
}


static void gst_gzipdec_finalize(GObject *object)
{
	GstGZipDec *gzipdec = GST_GZIPDEC(object);

	g_object_unref(G_OBJECT(gzipdec->adapter));

	G_OBJECT_CLASS(gst_gzipdec_parent_class)->finalize(object);
}


static GstStateChangeReturn gst_gzipdec_change_state(GstElement *element, GstStateChange transition)
{
	GstGZipDec *gzipdec = GST_GZIPDEC(element);
	GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;

	switch (transition)
	{
		case GST_STATE_CHANGE_NULL_TO_READY:
		{
			if (!gst_gzipdec_init_inflate(gzipdec))
				return GST_STATE_CHANGE_FAILURE;
			break;
		}

		case GST_STATE_CHANGE_READY_TO_PAUSED:
			gst_adapter_clear(gzipdec->adapter);
			gzipdec->typefind_done = FALSE;
			gzipdec->compressed_size_requested = FALSE;
			gzipdec->compressed_size_known = FALSE;
			gzipdec->compressed_size = 0;
			gzipdec->uncompressed_size = 0;
			break;

		default:
			break;
	}

	ret = GST_ELEMENT_CLASS(gst_gzipdec_parent_class)->change_state(element, transition);
	if (ret == GST_STATE_CHANGE_FAILURE)
		return ret;

	switch (transition)
	{
		case GST_STATE_CHANGE_READY_TO_NULL:
		{
			if (!gst_gzipdec_shutdown_inflate(gzipdec))
				return GST_STATE_CHANGE_FAILURE;
			break;
		}

		default:
			break;
	}

	return ret;
}


static gboolean gst_gzipdec_sink_event(GstPad *pad, GstObject *parent, GstEvent *event)
{
	GstGZipDec *gzipdec = GST_GZIPDEC(parent);

	switch(GST_EVENT_TYPE(event))
	{
		case GST_EVENT_SEGMENT:
			/* Upstream sends in a byte segment, which is uninteresting here,
			 * since a custom segment event is generated anyway */
			gst_event_unref(event);
			return TRUE;
		case GST_EVENT_EOS:
			if (gzipdec->compressed_size_known && (gzipdec->uncompressed_size == 0))
			{
				GST_WARNING_OBJECT(parent, "compressed size known, but EOS event received before all compressed data got accumulated -> won't decompress");
				return gst_pad_event_default(pad, parent, event);
			}
			else
			{
				gst_event_unref(event);
				return TRUE;
			}
		default:
			return gst_pad_event_default(pad, parent, event);
	}
}


static gboolean gst_gzipdec_src_query(GstPad *pad, GstObject *parent, GstQuery *query)
{
	gboolean res;
	GstFormat format;
	GstGZipDec *gzipdec = GST_GZIPDEC(parent);

	res = FALSE;

	switch (GST_QUERY_TYPE(query))
	{
		case GST_QUERY_DURATION:
		{
			gst_query_parse_duration(query, &format, NULL);
			GST_DEBUG_OBJECT(gzipdec, "got duration query, format: %s", gst_format_get_name(format));

			if (!gzipdec->compressed_size_known)
			{
				GST_DEBUG_OBJECT(gzipdec, "cannot respond to query: uncompressed size not known since upstream did not report compressed size");
				break;
			}

			if (format == GST_FORMAT_BYTES)
			{
				GST_DEBUG_OBJECT(gzipdec, "responding to query with size %" G_GINT64_FORMAT, gzipdec->uncompressed_size);
				gst_query_set_duration(query, format, gzipdec->uncompressed_size);
				res = TRUE;
			}
			else
				GST_DEBUG_OBJECT(gzipdec, "cannot respond to query: format is not in bytes");

			break;
		}

		default:
			res = gst_pad_query_default(pad, parent, query);
			break;
	}

	return res;
}


static GstFlowReturn gst_gzipdec_chain(G_GNUC_UNUSED GstPad *pad, GstObject *parent, GstBuffer *gz_data)
{
	GstFlowReturn flow_ret = GST_FLOW_OK;
	gboolean ok, eos;
	GstBuffer *out_buffer;
	GstGZipDec *gzipdec = GST_GZIPDEC(parent);

	if (!(gzipdec->compressed_size_requested))
	{
		gzipdec->compressed_size_known = gst_pad_peer_query_duration(gzipdec->sinkpad, GST_FORMAT_BYTES, &(gzipdec->compressed_size)) && (gzipdec->compressed_size >= 0);
		gzipdec->uncompressed_size = 0;
		gzipdec->compressed_size_requested = TRUE;

		if (gzipdec->compressed_size_known)
			GST_DEBUG_OBJECT(gzipdec->sinkpad, "upstream reported compressed size: %" G_GINT64_FORMAT " byte", gzipdec->compressed_size);
		else
			GST_DEBUG_OBJECT(gzipdec->sinkpad, "upstream did not report compressed size");
	}

	eos = FALSE;

	if (gzipdec->compressed_size_known)
	{
		/* total compressed size is known; accumulate all bytes
		 * until the compressed size is reached, then decompress,
		 * perform typefinding, and store the decompressed length
		 * example: Gzip compressed files */

		gsize num_bytes;
		GstBuffer *accum_in_buffer;
		GstCaps *caps;
		GstSegment segment;

		flow_ret = GST_FLOW_OK;

		/* This should not happen: data is received after decompression
		 * was complete. This implies that all data from upstream got
		 * processed. If more data is received after that, it means
		 * the compressed size reported by upstream wasn't correct.
		 * Discard upstream buffers in that case, and try to deal with
		 * what was received before. */
		if (gzipdec->uncompressed_size != 0)
		{
			GST_WARNING_OBJECT(gzipdec, "received data from upstream even though decompression is finished - discarding buffer");
			gst_buffer_unref(gz_data);
			flow_ret = GST_FLOW_EOS;
			goto cleanup;
		}

		gst_adapter_push(gzipdec->adapter, gz_data);
		num_bytes = gst_adapter_available(gzipdec->adapter);

		GST_LOG_OBJECT(gzipdec, "pushed compressed data to adapter; %" G_GINT64_FORMAT " byte in adapter", num_bytes);

		if ((gint64)num_bytes < gzipdec->compressed_size)
			goto cleanup;

		GST_LOG_OBJECT(gzipdec, "accumulated compressed data reached expected size of %" G_GINT64_FORMAT " byte -> decompress", gzipdec->compressed_size);

		accum_in_buffer = gst_adapter_take_buffer(gzipdec->adapter, num_bytes);
		ok = gst_gzipdec_inflate_buffer(gzipdec, accum_in_buffer, &out_buffer, &eos);
		gst_buffer_unref(accum_in_buffer);

		if (!ok)
		{
			flow_ret = GST_FLOW_ERROR;
			goto cleanup;
		}

		if (out_buffer == NULL)
		{
			GST_LOG_OBJECT(gzipdec, "empty decompressed buffer; nothing to push downstream");
			flow_ret = GST_FLOW_EOS;
			goto cleanup;
		}

		gzipdec->uncompressed_size = gst_buffer_get_size(out_buffer);
		GST_LOG_OBJECT(gzipdec, "uncompressed size: %" G_GINT64_FORMAT " byte", gzipdec->uncompressed_size);

		if ((caps = gst_gzipdec_do_typefinding_from_buffer(gzipdec, out_buffer)) != NULL)
		{
			gzipdec->typefind_done = TRUE;

			/* the new caps */
			gst_pad_push_event(gzipdec->srcpad, gst_event_new_caps(caps));
		}
		else
			GST_DEBUG_OBJECT(gzipdec, "could not identify contents - not setting caps");

		/* the new segment */
		gst_segment_init(&segment, GST_FORMAT_BYTES);
		gst_pad_push_event(gzipdec->srcpad, gst_event_new_segment(&segment));

		flow_ret = gst_pad_push(gzipdec->srcpad, out_buffer);

		if (flow_ret != GST_FLOW_OK)
		{
			GST_ERROR_OBJECT(gzipdec, "failed to push decompressed data downstream: %s (%d)", gst_flow_get_name(flow_ret), flow_ret);
			goto cleanup;
		}

		/* once all the decompressed data is pushed downstream,
		 * signal EOS, since there no more data to be delivered */
		eos = TRUE;
	}
	else
	{
		/* total compressed size not known, therefore it is not
		 * possible to know what the decompressed size is
		 * -> treat this as an open-ended, GZip compressed stream */

		ok = gst_gzipdec_inflate_buffer(gzipdec, gz_data, &out_buffer, &eos);

		/* input data is no longer needed */
		gst_buffer_unref(gz_data);

		/* if inflating went wrong, exit with error */
		if (!ok)
		{
			flow_ret = GST_FLOW_ERROR;
			goto cleanup;
		}

		/* empty buffers are not an error, but cannot be processed - just return OK */
		if (out_buffer == NULL)
		{
			flow_ret = GST_FLOW_OK;
			goto cleanup;
		}

		if (gzipdec->typefind_done)
		{
			/* type is known, normal mode - just push the
			 * decompressed buffers downstream */

			flow_ret = gst_pad_push(gzipdec->srcpad, out_buffer);

			if (flow_ret != GST_FLOW_OK)
			{
				GST_ERROR_OBJECT(gzipdec, "failed to push decompressed data downstream: %s (%d)", gst_flow_get_name(flow_ret), flow_ret);
				goto cleanup;
			}
		}
		else
		{
			/* typefind mode - accumulate data in the adapter
			 * until the type can be determined */

			GstCaps *caps;

			flow_ret = GST_FLOW_OK;

			gst_adapter_push(gzipdec->adapter, out_buffer);

			if ((caps = gst_gzipdec_do_typefinding_from_adapter(gzipdec, gzipdec->adapter)) != NULL)
			{
				/* the type is now known; send caps downstream */

				gzipdec->typefind_done = TRUE;

				/* the new caps */
				gst_pad_push_event(gzipdec->srcpad, gst_event_new_caps(caps));
			}

			if (!(gzipdec->typefind_done) && (gst_adapter_available(gzipdec->adapter) > (64 * 1024)))
			{
				GST_DEBUG_OBJECT(gzipdec, "adapter size reaching limit, type still not known -> assuming identification is not possible; not setting any caps");
				gzipdec->typefind_done = TRUE;
			}

			if (gzipdec->typefind_done)
			{
				 /*start a new open-ended BYTES segment
				 * (open-ended since no finite size is known) */

				GstSegment segment;

				/* the new segment */
				gst_segment_init(&segment, GST_FORMAT_BYTES);
				gst_pad_push_event(gzipdec->srcpad, gst_event_new_segment(&segment));

				/* push all data that was accumulated in the buffer */
				out_buffer = gst_adapter_take_buffer(gzipdec->adapter, gst_adapter_available(gzipdec->adapter));
				flow_ret = gst_pad_push(gzipdec->srcpad, out_buffer);
			}
		}
	}

cleanup:
	if (eos)
		gst_pad_push_event(gzipdec->srcpad, gst_event_new_eos());

	return flow_ret;
}


static GstCaps* gst_gzipdec_do_typefinding_from_adapter(GstGZipDec *gzipdec, GstAdapter *adapter)
{
	GstCaps *caps;
	guint8 const *data;
	gsize num_bytes;
	GstTypeFindProbability probability;

	num_bytes = gst_adapter_available(adapter);
	data = gst_adapter_map(adapter, num_bytes);
	caps = gst_type_find_helper_for_data(GST_OBJECT(gzipdec), data, num_bytes, &probability);
	gst_adapter_unmap(adapter);

	if (caps != NULL)
		GST_DEBUG_OBJECT(gzipdec, "typefinder reports caps: %" GST_PTR_FORMAT " with probability %d", (gpointer)caps, probability);

	if ((caps != NULL) && (probability < 1))
	{
		gst_caps_unref(caps);
		caps = NULL;
	}

	return caps;
}


static GstCaps* gst_gzipdec_do_typefinding_from_buffer(GstGZipDec *gzipdec, GstBuffer *buffer)
{
	GstCaps *caps;
	GstMapInfo in_map;
	GstTypeFindProbability probability;

	gst_buffer_map(buffer, &in_map, GST_MAP_READ);
	caps = gst_type_find_helper_for_data(GST_OBJECT(gzipdec), in_map.data, in_map.size, &probability);
	gst_buffer_unmap(buffer, &in_map);

	if (caps != NULL)
		GST_DEBUG_OBJECT(gzipdec, "typefinder reports caps: %" GST_PTR_FORMAT " with probability %d", (gpointer)caps, probability);

	if ((caps != NULL) && (probability < 1))
	{
		gst_caps_unref(caps);
		caps = NULL;
	}

	return caps;
}


static gboolean gst_gzipdec_init_inflate(GstGZipDec *gzipdec)
{
	int ret;

	gzipdec->strm.zalloc = Z_NULL;
	gzipdec->strm.zfree  = Z_NULL;
	gzipdec->strm.opaque = Z_NULL;

	gzipdec->strm.avail_in = 0;
	gzipdec->strm.next_in = NULL;

	ret = inflateInit2(&(gzipdec->strm), 16 + MAX_WBITS);
	if (ret != Z_OK)
	{
		GST_ERROR_OBJECT(gzipdec, "inflateInit2 failed: %s (%d)", gzipdec->strm.msg, ret);
		return FALSE;
	}

	return TRUE;
}


static gboolean gst_gzipdec_shutdown_inflate(GstGZipDec *gzipdec)
{
	int ret = inflateEnd(&(gzipdec->strm));
	if (ret != Z_OK)
	{
		GST_ERROR_OBJECT(gzipdec, "inflateEnd failed: %s (%d)", gzipdec->strm.msg, ret);
		return FALSE;
	}

	return TRUE;
}


static gboolean gst_gzipdec_inflate_buffer(GstGZipDec *gzipdec, GstBuffer *in_buffer, GstBuffer **out_buffer, gboolean *eos)
{
	GstMapInfo in_map;
	gboolean ret = TRUE;

	g_assert(gzipdec != NULL);
	g_assert(in_buffer != NULL);
	g_assert(out_buffer != NULL);

	gst_buffer_map(in_buffer, &in_map, GST_MAP_READ);

	gzipdec->strm.avail_in = in_map.size;
	gzipdec->strm.next_in = (z_const Bytef *)(in_map.data);

	*out_buffer = gst_buffer_new();
	if (eos != NULL)
		*eos = FALSE;

	GST_LOG_OBJECT(gzipdec, "decompressing %d byte", gzipdec->strm.avail_in);
	do
	{
		int inflate_ret;
		GstMapInfo out_map;
		GstBuffer *dec_buffer;

		dec_buffer = gst_buffer_new_allocate(NULL, GST_GZIP_DEC_BUFFER_SIZE, NULL);
		if (dec_buffer == NULL)
		{
			GST_ERROR_OBJECT(gzipdec, "could not allocate output buffer");
			ret = FALSE;
			goto cleanup;
		}

		gst_buffer_map(dec_buffer, &out_map, GST_MAP_WRITE);

		gzipdec->strm.avail_out = out_map.size;
		gzipdec->strm.next_out = (Bytef *)(out_map.data);

		inflate_ret = inflate(&(gzipdec->strm), Z_NO_FLUSH);

		gst_buffer_unmap(dec_buffer, &out_map);

		GST_LOG_OBJECT(
			gzipdec,
			"avail_in: %d (used: %" G_GSIZE_FORMAT ")  avail_out: %d (used: %" G_GSIZE_FORMAT ")  inflate ret: %d",
			gzipdec->strm.avail_in, in_map.size - gzipdec->strm.avail_in,
			gzipdec->strm.avail_out, out_map.size - gzipdec->strm.avail_out,
			inflate_ret
		);

		if ((inflate_ret != Z_OK) && (inflate_ret != Z_STREAM_END))
		{
			GST_ERROR_OBJECT(gzipdec, "inflate failed: %s (%d)", gzipdec->strm.msg, inflate_ret);
			ret = FALSE;
			gst_buffer_unref(dec_buffer);
			goto cleanup;
		}

		if (gzipdec->strm.avail_out != GST_GZIP_DEC_BUFFER_SIZE)
		{
			gst_buffer_set_size(dec_buffer, GST_GZIP_DEC_BUFFER_SIZE - gzipdec->strm.avail_out);
			*out_buffer = gst_buffer_append(*out_buffer, dec_buffer);
		}

		if (inflate_ret == Z_STREAM_END)
		{
			if (eos != NULL)
				*eos = TRUE;
			break;
		}
	}
	while (gzipdec->strm.avail_out == 0);

cleanup:
	gst_buffer_unmap(in_buffer, &in_map);

	if ((!ret && (*out_buffer != NULL)) || (gst_buffer_n_memory(*out_buffer) == 0))
	{
		gst_buffer_unref(*out_buffer);
		*out_buffer = NULL;
	}

	return ret;
}
