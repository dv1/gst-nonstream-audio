#ifndef GSTUADERAWDEC_H
#define GSTUADERAWDEC_H

#include <gst/gst.h>
#include "gst/audio/gstnonstreamaudiodecoder.h"
#include <linux/limits.h>
#include <sys/types.h>
#include <uade/uade.h>

#include "gstuade.h"


G_BEGIN_DECLS


typedef struct _GstUadeRawDec GstUadeRawDec;
typedef struct _GstUadeRawDecClass GstUadeRawDecClass;


#define GST_TYPE_UADE_RAW_DEC             (gst_uade_raw_dec_get_type())
#define GST_UADE_RAW_DEC(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_UADE_RAW_DEC, GstUadeRawDec))
#define GST_UADE_RAW_DEC_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_UADE_RAW_DEC, GstUadeRawDecClass))
#define GST_IS_UADE_RAW_DEC(obj)          (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_UADE_RAW_DEC))
#define GST_IS_UADE_RAW_DEC_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_UADE_RAW_DEC))


struct _GstUadeRawDec
{
	GstNonstreamAudioDecoder parent;

	struct uade_state *state;
	struct uade_song_info const *info;

	gchar *location;
	gchar *uadecore_file;
	gchar *base_directory;
	gst_uade_filter_types filter_type;
	gst_uade_headphone_modes headphone_mode;
	gboolean use_filter;
	gdouble gain;
	gboolean use_postprocessing;
	gdouble panning;

	gboolean playback_started;
	guint current_subsong;
};


struct _GstUadeRawDecClass
{
	GstNonstreamAudioDecoderClass parent_class;
};


GType gst_uade_raw_dec_get_type(void);


G_END_DECLS


#endif

