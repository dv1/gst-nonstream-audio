#include <config.h>
#include "gstgzipdec.h"


static gboolean plugin_init(GstPlugin *plugin)
{
	if (!gst_element_register(plugin, "gzipdec", GST_RANK_PRIMARY, gst_gzipdec_get_type()))
		return FALSE;

	return TRUE;
}

GST_PLUGIN_DEFINE(
	GST_VERSION_MAJOR,
	GST_VERSION_MINOR,
	gzip,
	"GZip compression elements",
	plugin_init,
	"1.0",
	"LGPL",
	"package",
	"http://no-url-yet"
)
