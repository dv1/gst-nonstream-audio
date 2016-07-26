#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <gst/gst.h>
#include "gstopenmptdec.h"


static gboolean plugin_init(GstPlugin *plugin)
{
	gboolean ret = TRUE;
	ret = ret && gst_element_register(plugin, "openmptdec", GST_RANK_PRIMARY + 2, gst_openmpt_dec_get_type());
	return ret;
}


GST_PLUGIN_DEFINE(
	GST_VERSION_MAJOR,
	GST_VERSION_MINOR,
	openmpt,
	"OpenMPT module player",
	plugin_init,
	VERSION,
	"LGPL",
	GST_PACKAGE_NAME,
	GST_PACKAGE_ORIGIN
)
