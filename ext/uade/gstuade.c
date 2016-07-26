#ifdef HAVE_CONFIG_H
 #include <config.h>
#endif

#include "gstuade.h"
#include "gstuaderawdec.h"


GType gst_uade_filter_type_get_type(void)
{
	static GType gst_uade_filter_type_type = 0;

	if (!gst_uade_filter_type_type)
	{
		static GEnumValue filter_type_values[] =
		{
			{ GST_UADE_FILTER_TYPE_A500,  "Amiga 500 lowpass filter", "a500" },
			{ GST_UADE_FILTER_TYPE_A1200, "Amiga 1200 lowpass filter", "a1200" },
			{ 0, NULL, NULL },
		};

		gst_uade_filter_type_type = g_enum_register_static(
			"UadeFilterType",
			filter_type_values
		);
	}

	return gst_uade_filter_type_type;
}


GType gst_uade_headphone_mode_get_type(void)
{
	static GType gst_uade_headphone_mode_type = 0;

	if (!gst_uade_headphone_mode_type)
	{
		static GEnumValue headphone_mode_values[] =
		{
			{ GST_UADE_HEADPHONE_MODE_NONE, "No headphone mode", "none" },
			{ GST_UADE_HEADPHONE_MODE_1,    "Headphone mode 1", "mode1" },
			{ GST_UADE_HEADPHONE_MODE_2,    "Headphone mode 2", "mode2" },
			{ 0, NULL, NULL },
		};

		gst_uade_headphone_mode_type = g_enum_register_static(
			"UadeHeadphoneMode",
			headphone_mode_values
		);
	}

	return gst_uade_headphone_mode_type;
}




static gboolean plugin_init(GstPlugin *plugin)
{
	if (!gst_element_register(plugin, "uaderawdec", GST_RANK_NONE, gst_uade_raw_dec_get_type())) return FALSE;
	return TRUE;
}

GST_PLUGIN_DEFINE(
	GST_VERSION_MAJOR,
	GST_VERSION_MINOR,
	uadedec,
	"UADE Amiga music player",
	plugin_init,
	"1.0",
	"LGPL",
	"package",
	"http://no-url-yet"
)


