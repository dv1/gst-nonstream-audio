#ifndef GSTUADE_H
#define GSTUADE_H

#include <gst/gst.h>


G_BEGIN_DECLS


typedef enum
{
	GST_UADE_FILTER_TYPE_A500,
	GST_UADE_FILTER_TYPE_A1200
} gst_uade_filter_types;


typedef enum
{
	GST_UADE_HEADPHONE_MODE_NONE,
	GST_UADE_HEADPHONE_MODE_1,
	GST_UADE_HEADPHONE_MODE_2
} gst_uade_headphone_modes;


GType gst_uade_filter_type_get_type(void);
#define GST_TYPE_UADE_FILTER_TYPE (gst_uade_filter_type_get_type())

GType gst_uade_headphone_mode_get_type(void);
#define GST_TYPE_UADE_HEADPHONE_MODE (gst_uade_headphone_mode_get_type())


G_END_DECLS


#endif

