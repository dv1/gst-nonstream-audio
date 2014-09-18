#ifndef UMXPARSE_H
#define UMXPARSE_H

#include <gst/gst.h>


G_BEGIN_DECLS


typedef struct _GstUmxParse GstUmxParse;
typedef struct _GstUmxParseClass GstUmxParseClass;


#define GST_TYPE_UMX_PARSE             (gst_umx_parse_get_type())
#define GST_UMX_PARSE(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_UMX_PARSE, GstUmxParse))
#define GST_UMX_PARSE_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_UMX_PARSE, GstUmxParseClass))
#define GST_IS_UMX_PARSE(obj)          (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_UMX_PARSE))
#define GST_IS_UMX_PARSE_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_UMX_PARSE))


struct _GstUmxParse
{
	GstElement parent;

	GstPad *sinkpad, *srcpad;

	gboolean upstream_eos;

	gint64 module_data_size;

	/* these two values are used in push mode only, for loading */
	GstAdapter *adapter;
	gint64 upstream_size;
};


struct _GstUmxParseClass
{
	GstElementClass parent_class;
};


GType gst_umx_parse_get_type(void);


G_END_DECLS


#endif

