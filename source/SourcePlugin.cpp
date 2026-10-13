#include "Gauge.h"

/**
	The source: a countdown leader on its own strip -- PICTURE START, eight to
	two with the sweep, the 2 for one frame, black -- looping, or once on a
	press, on the beat at Rate Beat.

	See EffectPlugin.cpp for why this file is listed in its own target.
	`SW Gauge Leader` is fifteen characters, inside the name field's sixteen.
*/
namespace
{
class GaugeSource : public gauge::GaugePlugin
{
public:
	GaugeSource() :
		GaugePlugin( false )
	{
	}
};
} // namespace

static CFFGLPluginInfo PluginInfo(
	PluginFactory< GaugeSource >,// Create method
	"GU02",                      // Plugin unique ID of maximum length 4
	"SW Gauge Leader",           // Plugin name
	2,                           // API major version number
	1,                           // API minor version number
	0,                           // Plugin major version number
	1,                           // Plugin minor version number
	FF_SOURCE,                   // Plugin type
	"A countdown leader on a strip of film: PICTURE START, eight to two with the sweep, the 2 for a single frame and "
	"black to the picture at exactly eight seconds -- or eight beats, landing on the bar. Full screen or as a strip in "
	"motion, with a sync flash on a press, a beat or a bar.",
	"Gauge Leader FFGL source"   // About
);

extern "C" const char* GaugeSourceBuildStamp()
{
	return "gauge " GAUGE_VERSION " source, built " __DATE__ " " __TIME__;
}
