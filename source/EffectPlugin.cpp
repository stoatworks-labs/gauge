#include "Gauge.h"

/**
	The effect: the clip printed onto the strip, frame by frame.

	Listed directly in the GaugeEffect target, not in gauge_core: both plugins
	share the class and not the `CFFGLPluginInfo` below, and putting either
	registration in the shared library would register both plugins into both
	bundles. The core is an OBJECT library because this registers itself from a
	file-scope constructor nothing references (see CMakeLists.txt).

	`SW Gauge` is eight characters; the FFGL name field is char[ 16 ] and not
	null-terminated. `oxbow probe` reads it back the way a host does.
*/
namespace
{
class GaugeEffect : public gauge::GaugePlugin
{
public:
	GaugeEffect() :
		GaugePlugin( true )
	{
	}
};
} // namespace

static CFFGLPluginInfo PluginInfo(
	PluginFactory< GaugeEffect >,// Create method
	"GU01",                      // Plugin unique ID of maximum length 4
	"SW Gauge",                  // Plugin name
	2,                           // API major version number
	1,                           // API minor version number
	0,                           // Plugin major version number
	1,                           // Plugin minor version number
	FF_EFFECT,                   // Plugin type
	"The film strip itself: the clip printed onto 35 mm, 16 mm or Super 8, with the perforations, frame lines and "
	"soundtracks where the standards put them. One frame full screen, or a strip in motion over anything, each frame "
	"keeping the picture it was exposed with. An anamorphic squeeze, a countdown leader and a sync flash.",
	"Gauge FFGL effect"          // About
);

extern "C" const char* GaugeEffectBuildStamp()
{
	return "gauge " GAUGE_VERSION " effect, built " __DATE__ " " __TIME__;
}
