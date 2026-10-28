/*
    The page's way in to the plugin: what an FFGL host does to a plugin
    instance, as C functions JavaScript can call.

    Nothing here is the plugin, and nothing here does the plugin's work. The
    plugin is `source/Gauge.cpp` -- the class both SW Gauge and SW Gauge Leader
    are made from -- and everything it calls (Strip, Camera, HeldFrames,
    Controls, Font, Lettering, Shaders, Diag, Transport), compiled UNMODIFIED
    into the same WebAssembly module as this file, with the FFGL SDK pieces it
    needs (see demo/tools/build-wasm.sh for the list). This file only plays the
    host:

      - it constructs a `GaugePlugin( true )` (SW Gauge, the effect) or
        `GaugePlugin( false )` (SW Gauge Leader, the source), as
        EffectPlugin.cpp's and SourcePlugin.cpp's registrations would through
        CFFGLPluginInfo, and calls InitGL / DeInitGL;
      - it reads the parameter declarations back through the FFGL SDK's own
        host-facing getters (GetParamName, GetParamType, GetParamGroup,
        GetParamDefault, the element, range and usage getters -- the SDK
        compiled unmodified too), so the page's panel is built from the
        plugin's constructor rather than from a copy of it;
      - it forwards the host's calls: SetFloatParameter, and each frame
        SetTime, SetBeatInfo and ProcessOpenGL -- with one input texture for
        the effect and none for the source, which takes its size from the
        viewport the page has set, as from a host's -- in the order
        tools/gutest's Session makes them.

    Two things are the page's and not the plugin's, and the page says so:

      - The transport. SetBeatInfo is handed a BPM and a bar phase from a
        clock the page keeps (a Host BPM field under the picture, 120 at
        first, and a bar phase advanced from the page's own seconds), because
        a browser has no Resolume transport. The plugin's Transport cannot tell
        it from one: Rate Beat, Start On Beat and Bar and Auto Flash run on it.
      - The audio. SW Gauge declares an FFT buffer (Audio) that Resolume fills
        every frame. This file never writes it, so the bins stay zero and the
        optical track is a band of constant width. (The leader's pop on the 2
        is not audio in: the composite draws it.)

    The clock is the page's too: SetTime is handed the kit's seconds, and the
    plugin's own vote settles the unit (seconds, within four frames). Before
    it settles the plugin runs on its wall clock, which here is the browser's.

    The read-outs use the plugin's harness accessors (StateForTest,
    ContentForTest, IndexOf) or its own functions (Controls.cpp, the About
    block's table); they report and never change anything. Two more harness
    hooks are here and the page itself never calls them: gu_declare_seconds,
    SetClockScaleForTest( 1.0 ), which demo/tools/crosscheck uses exactly as
    gutest's Session does every frame, so the page and gutest run the same
    clock frame for frame; and gu_perturb, SetPerturbForTest, which only the
    crosscheck's negative control calls, to show its comparison can fail.

    GL calls go to the page's WebGL2 context through emscripten's GL library;
    demo/wasm/gl_shim.cpp replaces the one entry point that cannot be passed
    straight through (glShaderSource).
*/
#include "Controls.h"
#include "Diag.h"
#include "Gauge.h"

#include <emscripten/emscripten.h>

#include <sys/stat.h>

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>

using namespace gauge;

namespace
{
/// One plugin instance and the host-side structs it is handed.
struct Instance
{
	explicit Instance( bool effect ) :
		plugin( effect )
	{
	}

	GaugePlugin plugin;
	bool initialised = false;

	FFGLTextureStruct input {};
	FFGLTextureStruct* inputs[ 1 ] = { &input };
	ProcessOpenGLStruct process {};

	std::string scratch;///< a string handed back to JS lives here until the next call
};

float asFloat( FFMixed mixed )
{
	float value = 0.0f;
	static_assert( sizeof( value ) == sizeof( mixed.UIntValue ), "FFMixed carries a float in its bits" );
	std::memcpy( &value, &mixed.UIntValue, sizeof( value ) );
	return value;
}

const char* hold( Instance* instance, std::string text )
{
	instance->scratch = std::move( text );
	return instance->scratch.c_str();
}

constexpr double kNaN = std::numeric_limits< double >::quiet_NaN();

/// The control a host index is, or K_KIND_COUNT for none (the About block).
Kind kindAt( Instance* instance, int index )
{
	for( int k = 0; k < K_KIND_COUNT; ++k )
		if( instance->plugin.IndexOf( static_cast< Kind >( k ) ) == index )
			return static_cast< Kind >( k );
	return K_KIND_COUNT;
}
} // namespace

extern "C"
{
//---------------------------------------------------------------------------
// The plugin's log. Diag.cpp opens it in its constructor (diag::init), so
// this must run before the first gu_new: a directory in the page's in-memory
// file system, named through the override Diag.cpp reads. Its own `mkdir -p`
// goes through system(), which a browser does not have.
//---------------------------------------------------------------------------
EMSCRIPTEN_KEEPALIVE void gu_prepare_log()
{
	setenv( "GAUGE_LOG_DIR", "/gauge/logs", 1 );
	mkdir( "/gauge", 0755 );
	mkdir( "/gauge/logs", 0755 );
}

EMSCRIPTEN_KEEPALIVE const char* gu_log_path()
{
	static std::string path;
	path = diag::logPath();
	return path.c_str();
}

//---------------------------------------------------------------------------
// Lifetime. `effect` 1 is SW Gauge (GU01, FF_EFFECT), 0 SW Gauge Leader
// (GU02, FF_SOURCE): the flag EffectPlugin.cpp and SourcePlugin.cpp pass.
//---------------------------------------------------------------------------
EMSCRIPTEN_KEEPALIVE Instance* gu_new( int effect )
{
	return new Instance( effect != 0 );
}

EMSCRIPTEN_KEEPALIVE void gu_delete( Instance* instance )
{
	if( instance == nullptr )
		return;
	if( instance->initialised )
		instance->plugin.DeInitGL();
	delete instance;
}

/// InitGL, with the viewport a host would pass. 1 on success.
EMSCRIPTEN_KEEPALIVE int gu_init_gl( Instance* instance, int width, int height )
{
	FFGLViewportStruct viewport {};
	viewport.width        = static_cast< GLuint >( width );
	viewport.height       = static_cast< GLuint >( height );
	instance->initialised = instance->plugin.InitGL( &viewport ) == FF_SUCCESS;
	return instance->initialised ? 1 : 0;
}

//---------------------------------------------------------------------------
// The declarations, read as a host reads them.
//---------------------------------------------------------------------------
EMSCRIPTEN_KEEPALIVE int gu_param_count( Instance* instance )
{
	return static_cast< int >( instance->plugin.GetNumParams() );
}

EMSCRIPTEN_KEEPALIVE const char* gu_param_name( Instance* instance, int index )
{
	const char* name = instance->plugin.GetParamName( static_cast< unsigned int >( index ) );
	return name ? name : "";
}

EMSCRIPTEN_KEEPALIVE int gu_param_type( Instance* instance, int index )
{
	return static_cast< int >( instance->plugin.GetParamType( static_cast< unsigned int >( index ) ) );
}

EMSCRIPTEN_KEEPALIVE int gu_param_usage( Instance* instance, int index )
{
	return static_cast< int >( instance->plugin.GetParamUsage( static_cast< unsigned int >( index ) ) );
}

EMSCRIPTEN_KEEPALIVE const char* gu_param_group( Instance* instance, int index )
{
	return hold( instance, instance->plugin.GetParamGroup( static_cast< unsigned int >( index ) ) );
}

/// The declared default of a numeric parameter (FF_TYPE_STANDARD already
/// clamped into 0..1 by the SDK, as a host sees it).
EMSCRIPTEN_KEEPALIVE float gu_param_default( Instance* instance, int index )
{
	return asFloat( instance->plugin.GetParamDefault( static_cast< unsigned int >( index ) ) );
}

/// The declared default of a text parameter.
EMSCRIPTEN_KEEPALIVE const char* gu_param_default_text( Instance* instance, int index )
{
	const FFMixed mixed = instance->plugin.GetParamDefault( static_cast< unsigned int >( index ) );
	return mixed.PointerValue ? static_cast< const char* >( mixed.PointerValue ) : "";
}

EMSCRIPTEN_KEEPALIVE int gu_param_element_count( Instance* instance, int index )
{
	return static_cast< int >( instance->plugin.GetNumParamElements( static_cast< unsigned int >( index ) ) );
}

EMSCRIPTEN_KEEPALIVE const char* gu_param_element_name( Instance* instance, int index, int element )
{
	const char* name = instance->plugin.GetParamElementName( static_cast< unsigned int >( index ), static_cast< unsigned int >( element ) );
	return name ? name : "";
}

EMSCRIPTEN_KEEPALIVE float gu_param_element_value( Instance* instance, int index, int element )
{
	return asFloat( instance->plugin.GetParamElementDefault( static_cast< unsigned int >( index ), static_cast< unsigned int >( element ) ) );
}

EMSCRIPTEN_KEEPALIVE int gu_max_inputs( Instance* instance )
{
	return static_cast< int >( instance->plugin.GetMaxInputs() );
}

//---------------------------------------------------------------------------
// The host's calls.
//---------------------------------------------------------------------------
EMSCRIPTEN_KEEPALIVE int gu_set_float( Instance* instance, int index, float value )
{
	return instance->plugin.SetFloatParameter( static_cast< unsigned int >( index ), value ) == FF_SUCCESS ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE float gu_get_float( Instance* instance, int index )
{
	return instance->plugin.GetFloatParameter( static_cast< unsigned int >( index ) );
}

EMSCRIPTEN_KEEPALIVE const char* gu_get_text( Instance* instance, int index )
{
	const char* text = instance->plugin.GetTextParameter( static_cast< unsigned int >( index ) );
	return text ? text : "";
}

/// One frame, in the order tools/gutest's Session makes the calls: SetTime,
/// SetBeatInfo with the page's transport, then ProcessOpenGL drawing into
/// framebuffer `hostFbo` (0 for the page's canvas) and whatever viewport the
/// page has bound, as a host's. The effect gets one input of clipWidth x
/// clipHeight whose GL name is `clip` (a texture the page registered with
/// emscripten's GL tables); the source gets none. 1 on success.
EMSCRIPTEN_KEEPALIVE int gu_process( Instance* instance, double seconds, float bpm, float barPhase, int clip, int clipWidth,
                                     int clipHeight, int hostFbo )
{
	instance->plugin.SetTime( seconds );
	instance->plugin.SetBeatInfo( bpm, barPhase );
	instance->input.Width = instance->input.HardwareWidth = static_cast< FFUInt32 >( clipWidth );
	instance->input.Height = instance->input.HardwareHeight = static_cast< FFUInt32 >( clipHeight );
	instance->input.Handle                                 = static_cast< GLuint >( clip );
	const bool effect                                      = instance->plugin.IsEffect();
	instance->process.numInputTextures                     = effect ? 1 : 0;
	instance->process.inputTextures                        = effect ? instance->inputs : nullptr;
	instance->process.HostFBO                              = static_cast< GLuint >( hostFbo );
	return instance->plugin.ProcessOpenGL( &instance->process ) == FF_SUCCESS ? 1 : 0;
}

/// The crosscheck's only: SetClockScaleForTest( 1.0 ), as gutest's Session
/// calls before every frame, so the host's seconds are taken as seconds from
/// the first frame instead of after the plugin's four-frame vote. The page
/// itself never calls this.
EMSCRIPTEN_KEEPALIVE void gu_declare_seconds( Instance* instance )
{
	instance->plugin.SetClockScaleForTest( 1.0 );
}

/// The crosscheck's negative control only: SetPerturbForTest( bits ), the
/// broken models Gauge.h's Perturb enum names (gutest --negative's), on the
/// page's side alone, so the comparison can be shown to fail. The page itself
/// never calls this; the plugins carry 0.
EMSCRIPTEN_KEEPALIVE void gu_perturb( Instance* instance, int bits )
{
	instance->plugin.SetPerturbForTest( bits );
}

//---------------------------------------------------------------------------
// For the panel's read-outs: Controls.cpp's conversion for a parameter, the
// one Gauge.cpp's ProcessOpenGL applies to it (Rack in frames, Drift and
// Travel in frames a second, Size as a fraction of the output's height, Angle
// and Shutter in degrees, Position in half-heights, Track Level as a gain).
// NaN where the plugin uses the host's 0..1 as it is (Framing, Mix), and for
// the controls that are not ranged.
//---------------------------------------------------------------------------
EMSCRIPTEN_KEEPALIVE double gu_convert( Instance* instance, int index, float v )
{
	switch( kindAt( instance, index ) )
	{
	case K_RACK: return controls::RackFrames( v );
	case K_DRIFT: return controls::DriftFps( v );
	case K_SIZE: return controls::SizeFraction( v );
	case K_ANGLE: return controls::AngleDegrees( v );
	case K_POSITION: return controls::PositionFraction( v );
	case K_TRAVEL: return controls::TravelFps( v );
	case K_SHUTTER: return controls::ShutterDegrees( v );
	case K_TRACK_LEVEL: return controls::TrackLevel( v );
	default: return kNaN;
	}
}

//---------------------------------------------------------------------------
// The About block's buttons: the URL each one opens, from the
// StoatworksAboutLinks.h the plugin is built with. The host getters give only
// a button's label; the page needs the address the plugin's own press would
// have opened, because that press calls system(), which a browser does not
// have.
//---------------------------------------------------------------------------
EMSCRIPTEN_KEEPALIVE int gu_about_first( Instance* instance )
{
	return static_cast< int >( instance->plugin.AboutFirst() );
}

EMSCRIPTEN_KEEPALIVE const char* gu_about_button_label( int button )
{
	const auto& list = stoatworks::about::buttons();
	return button >= 0 && static_cast< size_t >( button ) < list.size() ? list[ static_cast< size_t >( button ) ].label : "";
}

EMSCRIPTEN_KEEPALIVE const char* gu_about_button_url( int button )
{
	const auto& list = stoatworks::about::buttons();
	return button >= 0 && static_cast< size_t >( button ) < list.size() ? list[ static_cast< size_t >( button ) ].url : "";
}

//---------------------------------------------------------------------------
// The film's state after the last frame, through StateForTest. 0 = the film
// clock, frames; 1 = the frame in the gate; 2 = the film's rate, frames a
// second; 3 = the strip's centre, frames; 4 = the PICTURE START frame (NaN for
// none); 5, 6 = the first and last visible frame; 7, 8, 9 = the held ring's
// cell width, height and layers; 10 = the camera's mm per pixel. Frame
// indices are 64-bit in the plugin and come back as doubles, exact to 2^53.
//---------------------------------------------------------------------------
EMSCRIPTEN_KEEPALIVE double gu_state( Instance* instance, int what )
{
	const GaugePlugin::State& s = instance->plugin.StateForTest();
	switch( what )
	{
	case 0: return s.film;
	case 1: return static_cast< double >( s.gate );
	case 2: return s.rate;
	case 3: return s.stripFrames;
	case 4: return s.leaderStart == INT64_MIN ? kNaN : static_cast< double >( s.leaderStart );
	case 5: return static_cast< double >( s.visibleLo );
	case 6: return static_cast< double >( s.visibleHi );
	case 7: return s.heldWidth;
	case 8: return s.heldHeight;
	case 9: return s.heldLayers;
	case 10: return s.camera.mmPerPixel;
	default: return kNaN;
	}
}

/// What frame `index` showed in the last composite (ContentForTest): kind *
/// 1000 + value, the kinds Gauge.h's Content enum (0 the clip live, 1 a held
/// slot, 2 leader frame `value`, 3 a flash, 4 black, 5 clear), or -1 for a
/// frame that was not in view.
EMSCRIPTEN_KEEPALIVE int gu_content( Instance* instance, double index )
{
	int kind = 0, value = 0;
	if( !instance->plugin.ContentForTest( static_cast< int64_t >( index ), kind, value ) )
		return -1;
	return kind * 1000 + value;
}

/// The number Leader::NumberAt puts on leader frame j: 0 for PICTURE START,
/// -1 for black.
EMSCRIPTEN_KEEPALIVE int gu_leader_number( int j )
{
	return Leader::NumberAt( j );
}

} // extern "C"
