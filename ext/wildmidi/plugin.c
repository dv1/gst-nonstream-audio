#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include "gstwildmididec.h"


static gboolean plugin_init(GstPlugin *plugin)
{
	gboolean ret = TRUE;
	ret = ret && gst_element_register(plugin, "wildmididec", GST_RANK_PRIMARY + 2, gst_wildmidi_dec_get_type());
	return ret;
}


GST_PLUGIN_DEFINE(
	GST_VERSION_MAJOR,
	GST_VERSION_MINOR,
	wildmidi,
	"WildMidi-based MIDI playback plugin",
	plugin_init,
	VERSION,
	"LGPL",
	GST_PACKAGE_NAME,
	GST_PACKAGE_ORIGIN
)
