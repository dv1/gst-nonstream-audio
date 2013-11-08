#ifndef GSTUADEDEC_H
#define GSTUADEDEC_H

#include <gst/gst.h>
#include "gstnonstreamaudiodecoder.h"
#include <linux/limits.h>
#include <uade/uade.h>


G_BEGIN_DECLS


typedef struct _GstUadeDec GstUadeDec;
typedef struct _GstUadeDecClass GstUadeDecClass;


#define GST_TYPE_UADE_DEC             (gst_uade_dec_get_type())
#define GST_UADE_DEC(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_UADE_DEC, GstUadeDec))
#define GST_UADE_DEC_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_UADE_DEC, GstUadeDecClass))
#define GST_IS_UADE_DEC(obj)          (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_UADE_DEC))
#define GST_IS_UADE_DEC_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_UADE_DEC))


typedef enum
{
	GST_UADE_DEC_FILTER_TYPE_A500,
	GST_UADE_DEC_FILTER_TYPE_A1200
} gst_uade_dec_filter_types;


typedef enum
{
	GST_UADE_DEC_HEADPHONE_MODE_NONE,
	GST_UADE_DEC_HEADPHONE_MODE_1,
	GST_UADE_DEC_HEADPHONE_MODE_2
} gst_uade_dec_headphone_modes;


struct _GstUadeDec
{
	GstNonstreamAudioDecoder parent;

	struct uade_state *state;
	struct uade_song_info const *info;

	gchar *location;
	gchar *uadecore_file;
	gchar *base_directory;
	gst_uade_dec_filter_types filter_type;
	gst_uade_dec_headphone_modes headphone_mode;
	gboolean use_filter;
	gdouble gain;
	gboolean use_postprocessing;
	gdouble panning;

	gboolean playback_started;
	guint current_subsong;
};


struct _GstUadeDecClass
{
	GstNonstreamAudioDecoderClass parent_class;
};


GType gst_uade_dec_get_type(void);


G_END_DECLS


#endif

