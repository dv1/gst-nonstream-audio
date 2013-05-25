#ifndef GSTDUMB_H
#define GSTDUMB_H

#include <gst/gst.h>
#include <dumb.h>
#include "gstnonstreamaudiodecoder.h"


G_BEGIN_DECLS


typedef struct _GstDumb GstDumb;
typedef struct _GstDumbClass GstDumbClass;


#define GST_TYPE_DUMB             (gst_dumb_get_type())
#define GST_DUMB(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_DUMB, GstDumb))
#define GST_DUMB_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_DUMB, GstDumbClass))
#define GST_IS_DUMB(obj)          (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_DUMB))
#define GST_IS_DUMB_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_DUMB))


struct _GstDumb
{
	GstNonstreamAudioDecoder parent;

	gint sample_rate, num_channels;
	DUH *duh;
	DUH_SIGRENDERER *duh_sigrenderer;
};


struct _GstDumbClass
{
	GstNonstreamAudioDecoderClass parent_class;
};


GType gst_dumb_get_type(void);


G_END_DECLS


#endif

