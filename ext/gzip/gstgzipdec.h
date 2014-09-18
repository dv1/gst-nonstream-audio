#ifndef GSTGZIPDEC_H
#define GSTGZIPDEC_H

#include <gst/gst.h>
#include <gst/base/gstadapter.h>
#include <zlib.h>


G_BEGIN_DECLS


typedef struct _GstGZipDec GstGZipDec;
typedef struct _GstGZipDecClass GstGZipDecClass;


#define GST_TYPE_GZIPDEC             (gst_gzipdec_get_type())
#define GST_GZIPDEC(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_GZIPDEC, GstGZipDec))
#define GST_GZIPDEC_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_GZIPDEC, GstGZipDecClass))
#define GST_IS_GZIPDEC(obj)          (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_GZIPDEC))
#define GST_IS_GZIPDEC_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_GZIPDEC))


struct _GstGZipDec
{
	GstElement parent;
	GstPad *sinkpad, *srcpad;

	z_stream strm;

	GstAdapter *adapter;
	gboolean typefind_done;
	gboolean compressed_size_requested, compressed_size_known;
	gint64 compressed_size, uncompressed_size;
};


struct _GstGZipDecClass
{
	GstElementClass parent_class;
};


GType gst_gzipdec_get_type(void);


G_END_DECLS


#endif
