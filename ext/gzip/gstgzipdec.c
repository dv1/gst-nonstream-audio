#include <string.h>
#include <config.h>
#include <gst/gst.h>
#include <gst/base/gstadapter.h>
#include <gst/base/gsttypefindhelper.h>
#include <archive.h>
#include <archive_entry.h>

#include "gstgzipdec.h"


GST_DEBUG_CATEGORY_STATIC(gzipdec_debug);
#define GST_CAT_DEFAULT gzipdec_debug


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

static gboolean gst_gzipdec_sink_event(GstPad *pad, GstObject *parent, GstEvent *event);
static GstFlowReturn gst_gzipdec_chain(GstPad *pad, GstObject *parent, GstBuffer *buffer);
static gboolean gst_gzipdec_src_query(GstPad *pad, GstObject *parent, GstQuery *query);

static gboolean gst_gzipdec_get_upstream_size(GstGZipDec *gzipdec, gint64 *length);

static GstFlowReturn gst_gzipdec_read(GstGZipDec *gzipdec, GstBuffer *gz_data);



void gst_gzipdec_class_init(GstGZipDecClass *klass)
{
	GObjectClass *object_class;
	GstElementClass *element_class;

	GST_DEBUG_CATEGORY_INIT(gzipdec_debug, "gzipdec", 0, "Unreal UMX parser");

	object_class = G_OBJECT_CLASS(klass);
	element_class = GST_ELEMENT_CLASS(klass);

	gst_element_class_add_pad_template(element_class, gst_static_pad_template_get(&sink_template));
	gst_element_class_add_pad_template(element_class, gst_static_pad_template_get(&src_template));

	object_class->finalize = GST_DEBUG_FUNCPTR(gst_gzipdec_finalize);

	gst_element_class_set_static_metadata(
		element_class,
		"GZip decompressor",
		"Codec/Demuxer",
		"Uncompress GZip-compressed content and sends the decompressed bytes downstream",
		"Carlos Rafael Giani <dv@pseudoterminal.org>"
	);
}


void gst_gzipdec_init(GstGZipDec *gzipdec)
{
	gzipdec->upstream_eos = FALSE;
	gzipdec->decompressed_size = 0;

	gzipdec->in_adapter = gst_adapter_new();
	gzipdec->upstream_size = -1;

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

	g_object_unref(G_OBJECT(gzipdec->in_adapter));

	G_OBJECT_CLASS(gst_gzipdec_parent_class)->finalize(object);
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
		{
			gzipdec->upstream_eos = TRUE;

			return TRUE;
		}
		default:
			return gst_pad_event_default(pad, parent, event);
	}
}


static GstFlowReturn gst_gzipdec_chain(G_GNUC_UNUSED GstPad *pad, GstObject *parent, GstBuffer *buffer)
{
	GstGZipDec *gzipdec = GST_GZIPDEC(parent);

	GST_TRACE_OBJECT(gzipdec, "entered chain function");

	if (gzipdec->upstream_size < 0)
	{
		if (!gst_gzipdec_get_upstream_size(gzipdec, &(gzipdec->upstream_size)))
		{
			GST_ELEMENT_ERROR(gzipdec, STREAM, DECODE, (NULL), ("Cannot load - upstream size (in bytes) could not be determined"));
			return GST_FLOW_ERROR;
		}
	}

	/* Accumulate data until end-of-stream or the upstream size is reached, then load media and commence playback. */

	gint64 avail_size;

	gst_adapter_push(gzipdec->in_adapter, buffer);
	buffer = NULL;
	avail_size = gst_adapter_available(gzipdec->in_adapter);
	if (gzipdec->upstream_eos || (avail_size >= gzipdec->upstream_size))
	{
		GstBuffer *in_adapter_buffer = gst_adapter_take_buffer(gzipdec->in_adapter, avail_size);

		return gst_gzipdec_read(gzipdec, in_adapter_buffer);
	}

	return GST_FLOW_OK;
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
			GST_TRACE_OBJECT(gzipdec, "got duration query, format: %s", gst_format_get_name(format));
			if ((format == GST_FORMAT_BYTES) && (gzipdec->decompressed_size >= 0))
			{
				GST_TRACE_OBJECT(gzipdec, "responding to query with size %" G_GINT64_FORMAT, gzipdec->decompressed_size);
				gst_query_set_duration(query, format, gzipdec->decompressed_size);
				res = TRUE;
			}
			else
				GST_TRACE_OBJECT(gzipdec, "cannot respond to query, no size set or query format is not in bytes");

			break;
		}
		default:
			break;
	}

	if (!res)
		res = gst_pad_query_default(pad, parent, query);

	return res;
}


static gboolean gst_gzipdec_get_upstream_size(GstGZipDec *gzipdec, gint64 *length)
{
	return gst_pad_peer_query_duration(gzipdec->sinkpad, GST_FORMAT_BYTES, length) && (*length >= 0);
}


static GstFlowReturn gst_gzipdec_read(GstGZipDec *gzipdec, GstBuffer *gz_data)
{
	struct archive *arch;
	struct archive_entry *entry;
	GstMapInfo in_map;
	GstSegment segment;
	GstFlowReturn ret;
	GstAdapter *out_adapter;
	GstBuffer *out_buffer;
	gsize out_size;
	GstCaps *caps;
	GstTypeFindProbability typefind_probability;

	if (gzipdec->decompressed_size > 0)
	{
		GST_DEBUG_OBJECT(gzipdec, "data already decompressed, ignoring read call");
		return GST_FLOW_OK;
	}

	ret = GST_FLOW_OK;
	out_adapter = NULL;

	gst_buffer_map(gz_data, &in_map, GST_MAP_READ);

	arch = archive_read_new();
	archive_read_support_filter_gzip(arch);
	archive_read_support_format_raw(arch);
	if (archive_read_open_memory(arch, in_map.data, in_map.size) != ARCHIVE_OK)
	{
		GST_ERROR_OBJECT(gzipdec, "could not open gzip-compressed memory block: %s", archive_error_string(arch));
		ret = GST_FLOW_ERROR;
		goto cleanup;
	}

	if (archive_read_next_header(arch, &entry) != ARCHIVE_OK)
	{
		GST_ERROR_OBJECT(gzipdec, "could not read next header: %s", archive_error_string(arch));
		ret = GST_FLOW_ERROR;
		goto cleanup;
	}

	out_adapter = gst_adapter_new();

	while (TRUE)
	{
		GstMapInfo tmp_map;
		ssize_t read_size;
		gsize const buffer_size = 32768;

		GstBuffer *buffer = gst_buffer_new_allocate(NULL, buffer_size, NULL);
		gst_buffer_map(buffer, &tmp_map, GST_MAP_WRITE);
		read_size = archive_read_data(arch, tmp_map.data, tmp_map.size);
		gst_buffer_unmap(buffer, &tmp_map);

		if (read_size <= 0)
		{
			if (read_size < 0)
			{
				GST_ERROR_OBJECT(gzipdec, "error while reading compressed data: %s", archive_error_string(arch));
				ret = GST_FLOW_ERROR;
			}
			else
			{
				GST_INFO_OBJECT(gzipdec, "end of data reached");
			}

			gst_buffer_unref(buffer);

			break;
		}
		else
		{
			gst_buffer_set_size(buffer, read_size);
			gst_adapter_push(out_adapter, buffer);
		}
	}

	out_size = gst_adapter_available(out_adapter);
	out_buffer = gst_adapter_take_buffer(out_adapter, out_size);

	g_assert(out_buffer != NULL);

	GST_INFO_OBJECT(gzipdec, "data size:  compressed: %" G_GSIZE_FORMAT "  uncompressed: %" G_GSIZE_FORMAT, in_map.size, out_size);

	/* Send caps event downstream */
	caps = gst_type_find_helper_for_buffer(GST_OBJECT(gzipdec), out_buffer, &typefind_probability);
	if (caps != NULL)
	{
		GST_INFO_OBJECT(gzipdec, "typefind recognized uncompressed data as %" GST_PTR_FORMAT " with probability %d", (gpointer)caps, typefind_probability);
		gst_pad_push_event(gzipdec->srcpad, gst_event_new_caps(caps));
		gst_caps_unref(caps);
	}
	else
	{
		GST_ERROR_OBJECT(gzipdec, "typefind could not recognize uncompressed data");
		ret = GST_FLOW_ERROR;
		goto cleanup;
	}

	/* Send segment event downstream */
	gst_segment_init(&segment, GST_FORMAT_BYTES);
	segment.duration = out_size;
	gst_pad_push_event(gzipdec->srcpad, gst_event_new_segment(&segment));

	gzipdec->decompressed_size = out_size;

	/* Send data downstream */
	if ((ret = gst_pad_push(gzipdec->srcpad, out_buffer)) != GST_FLOW_OK)
	{
		GST_ERROR_OBJECT(gzipdec, "failed to push decompressed data downstream: %s (%d)", gst_flow_get_name(ret), ret);
		goto cleanup;
	}

cleanup:
	gst_buffer_unmap(gz_data, &in_map);
	if (out_adapter != NULL)
		g_object_unref(G_OBJECT(out_adapter));

	return ret;
}
