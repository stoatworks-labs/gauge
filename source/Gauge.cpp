#include "Gauge.h"

#include "Controls.h"
#include "Diag.h"
#include "Font.h"
#include "Lettering.h"
#include "Shaders.h"

#include <ffglex/FFGLScopedFBOBinding.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <string>

using namespace ffglex;

namespace gauge
{
namespace
{
/// Frames that must agree before the host's clock unit is settled.
constexpr int kClockVotes = 4;

/// Seconds of host time one frame may advance the clock by.
constexpr double kMaxFrameDelta = 0.25;

constexpr double kPi = 3.14159265358979323846;

/// The FFT's bins, assumed linear from 0 to a 44.1 kHz Nyquist. Nobody in the
/// fleet has measured Resolume's 64 bins; this only sets how finely the
/// optical track wiggles, and AGENTS.md says so.
constexpr double kBinHz = 22050.0 / 64.0;

/// The ring's budget of held pixels, all layers together: 40 MPix of RGBA8 is
/// 160 MB at the very worst (a 4K cell view with many frames).
constexpr double kHeldBudget = 40.0e6;

int64_t floorDiv( int64_t a, int64_t b )
{
	const int64_t q = a / b;
	return ( a % b != 0 && ( ( a < 0 ) != ( b < 0 ) ) ) ? q - 1 : q;
}

int64_t floorMod( int64_t a, int64_t b )
{
	return a - floorDiv( a, b ) * b;
}

double wallSeconds()
{
	using namespace std::chrono;
	static const steady_clock::time_point start = steady_clock::now();
	return duration_cast< duration< double > >( steady_clock::now() - start ).count();
}

std::string glStringOrUnknown( GLenum name )
{
	const GLubyte* v = glGetString( name );
	return v ? reinterpret_cast< const char* >( v ) : "unknown";
}

/// What a stock looks like where it carries no picture.
struct StockLook
{
	float base[ 3 ];
	float ink[ 3 ];
	float dense[ 3 ];
	float clear[ 3 ];
	float black[ 3 ];
	const char* code;
	bool mono;
};

const StockLook& lookOf( int stock )
{
	//A release print's rebate is black: the printer exposes it through the
	//negative's clear edge. The edge print and the key numbers print through
	//as light. A colour print's analogue track is a cyan dye track, so its
	//dense part is a deep blue-green. Clear base is unexposed print film on a
	//light box. The codes are invented.
	static const StockLook looks[ controls::kStockCount ] = {
		{ { 0.018f, 0.016f, 0.020f }, { 0.80f, 0.78f, 0.70f }, { 0.02f, 0.07f, 0.10f }, { 0.92f, 0.95f, 0.96f },
		  { 0.012f, 0.011f, 0.014f }, "GP 7712", false },
		{ { 0.020f, 0.020f, 0.020f }, { 0.82f, 0.82f, 0.82f }, { 0.03f, 0.03f, 0.03f }, { 0.93f, 0.93f, 0.93f },
		  { 0.015f, 0.015f, 0.015f }, "GB 7302", true },
		{ { 0.86f, 0.86f, 0.84f }, { 0.20f, 0.20f, 0.20f }, { 0.10f, 0.10f, 0.10f }, { 0.95f, 0.95f, 0.95f },
		  { 0.020f, 0.020f, 0.020f }, "GC 7015", false },
	};
	return looks[ std::clamp( stock, 0, controls::kStockCount - 1 ) ];
}

const char* groupOf( Kind kind )
{
	if( kind <= K_AUDIO )
		return "Film";
	if( kind <= K_SHUTTER )
		return "View";
	if( kind <= K_FRAMES )
		return "Frames";
	if( kind <= K_AFTER )
		return "Leader";
	return "Surround";
}
} // namespace

//---------------------------------------------------------------------------
GaugePlugin::GaugePlugin( bool effect ) :
	isEffect( effect )
{
	SetMinInputs( effect ? 1 : 0 );
	SetMaxInputs( effect ? 1 : 0 );

	//The film clock is a function of the host's clock: a re-render of the same
	//composition must expose the same frames at the same times.
	SetTimeSupported( true );

	if( effect )
		order = { K_FORMAT, K_STOCK, K_SQUEEZE, K_FIT, K_EDGE_PRINT, K_SOUNDTRACKS, K_TRACK_LEVEL, K_AUDIO,
		          K_MODE, K_FRAMING, K_RACK, K_DRIFT, K_SIZE, K_ANGLE, K_POSITION, K_TRAVEL, K_SHUTTER,
		          K_RATE, K_FRAMES,
		          K_ROLL_LEADER, K_START_ON, K_POP_FRAME, K_FLASH, K_AUTO_FLASH,
		          K_HOLES, K_SURROUND, K_MIX };
	else
		order = { K_FORMAT, K_STOCK, K_SQUEEZE, K_EDGE_PRINT, K_SOUNDTRACKS, K_TRACK_LEVEL, K_AUDIO,
		          K_MODE, K_FRAMING, K_RACK, K_DRIFT, K_SIZE, K_ANGLE, K_POSITION, K_TRAVEL, K_SHUTTER,
		          K_RATE,
		          K_LOOP, K_ROLL_LEADER, K_START_ON, K_POP_FRAME, K_FLASH, K_AUTO_FLASH, K_AFTER,
		          K_HOLES, K_SURROUND };
	hostIndex.fill( -1 );
	for( size_t i = 0; i < order.size(); ++i )
		hostIndex[ order[ i ] ] = static_cast< int >( i );

	//-----------------------------------------------------------------------
	// Defaults: a 35 mm Academy release print, one frame filling the output
	// with its holes and track either side, at 24 fps, the clip held frame by
	// frame. A strip, when Mode says so, a third of the output high, running
	// at two frames a second with a half-open shutter.
	//-----------------------------------------------------------------------
	values[ K_FORMAT ]      = static_cast< float >( strip::kAcademy );
	values[ K_STOCK ]       = 0.0f;
	values[ K_SQUEEZE ]     = 0.0f;
	values[ K_FIT ]         = 0.0f;
	values[ K_EDGE_PRINT ]  = 1.0f;
	values[ K_SOUNDTRACKS ] = 1.0f;
	values[ K_TRACK_LEVEL ] = controls::TrackLevelParam( 1.0f );
	values[ K_MODE ]        = 0.0f;
	values[ K_FRAMING ]     = 0.7f;
	values[ K_RACK ]        = controls::RackParam( 0.0 );
	values[ K_DRIFT ]       = controls::DriftParam( 0.0 );
	values[ K_SIZE ]        = controls::SizeParam( 0.3 );
	values[ K_ANGLE ]       = controls::AngleParam( 0.0 );
	values[ K_POSITION ]    = controls::PositionParam( 0.0 );
	values[ K_TRAVEL ]      = controls::TravelParam( 2.0 );
	values[ K_SHUTTER ]     = controls::ShutterParam( 180.0 );
	values[ K_RATE ]        = 0.0f;
	values[ K_FRAMES ]      = 0.0f;
	values[ K_LOOP ]        = 1.0f;
	values[ K_START_ON ]    = 0.0f;
	values[ K_POP_FRAME ]   = 0.0f;
	values[ K_AUTO_FLASH ]  = 0.0f;
	values[ K_AFTER ]       = 0.0f;
	values[ K_HOLES ]       = 0.0f;
	values[ K_SURROUND ]    = 0.0f;
	values[ K_MIX ]         = 1.0f;

	declare();

	FFGLLog::LogToHost( effect ? "Created Gauge effect" : "Created Gauge Leader source" );
	diag::init();
}

void GaugePlugin::declare()
{
	auto options = [ this ]( unsigned int id, const char* name, int count, const char* ( *nameAt )( int ), Kind kind ) {
		SetOptionParamInfo( id, name, static_cast< unsigned int >( count ), values[ kind ] );
		for( int i = 0; i < count; ++i )
			SetParamElementInfo( id, static_cast< unsigned int >( i ), nameAt( i ), static_cast< float >( i ) );
	};

	for( size_t i = 0; i < order.size(); ++i )
	{
		const unsigned int id = static_cast< unsigned int >( i );
		const Kind kind       = order[ i ];
		switch( kind )
		{
		case K_FORMAT: options( id, "Format", strip::kFormatCount, strip::FormatName, kind ); break;
		case K_STOCK: options( id, "Stock", controls::kStockCount, controls::StockName, kind ); break;
		case K_SQUEEZE: options( id, "Squeeze", controls::kSqueezeCount, controls::SqueezeName, kind ); break;
		case K_FIT: options( id, "Fit", controls::kFitCount, controls::FitName, kind ); break;
		case K_EDGE_PRINT: SetParamInfo( id, "Edge Print", FF_TYPE_BOOLEAN, values[ kind ] > 0.5f ); break;
		case K_SOUNDTRACKS: SetParamInfo( id, "Soundtracks", FF_TYPE_BOOLEAN, values[ kind ] > 0.5f ); break;
		case K_TRACK_LEVEL: SetParamInfo( id, "Track Level", FF_TYPE_STANDARD, values[ kind ] ); break;
		case K_AUDIO:
			//Declared with a real element list so the host knows how many bins to
			//fill. With no audio routed the bins are zero and the track is silent.
			SetBufferParamInfo( id, "Audio", kAudioBins, FF_USAGE_FFT );
			for( int b = 0; b < kAudioBins; ++b )
				SetParamElementInfo( id, static_cast< unsigned int >( b ), "", 0.0f );
			break;
		case K_MODE: options( id, "Mode", controls::kModeCount, controls::ModeName, kind ); break;
		case K_FRAMING: SetParamInfo( id, "Framing", FF_TYPE_STANDARD, values[ kind ] ); break;
		case K_RACK: SetParamInfo( id, "Rack", FF_TYPE_STANDARD, values[ kind ] ); break;
		case K_DRIFT: SetParamInfo( id, "Drift", FF_TYPE_STANDARD, values[ kind ] ); break;
		case K_SIZE: SetParamInfo( id, "Size", FF_TYPE_STANDARD, values[ kind ] ); break;
		case K_ANGLE: SetParamInfo( id, "Angle", FF_TYPE_STANDARD, values[ kind ] ); break;
		case K_POSITION: SetParamInfo( id, "Position", FF_TYPE_STANDARD, values[ kind ] ); break;
		case K_TRAVEL: SetParamInfo( id, "Travel", FF_TYPE_STANDARD, values[ kind ] ); break;
		case K_SHUTTER: SetParamInfo( id, "Shutter", FF_TYPE_STANDARD, values[ kind ] ); break;
		case K_RATE: options( id, "Rate", controls::kRateCount, controls::RateName, kind ); break;
		case K_FRAMES: options( id, "Frames", controls::kFramesCount, controls::FramesName, kind ); break;
		case K_LOOP: SetParamInfo( id, "Loop", FF_TYPE_BOOLEAN, values[ kind ] > 0.5f ); break;
		case K_ROLL_LEADER: SetParamInfo( id, "Roll Leader", FF_TYPE_EVENT, false ); break;
		case K_START_ON: options( id, "Start On", controls::kStartOnCount, controls::StartOnName, kind ); break;
		case K_POP_FRAME: options( id, "Pop Frame", controls::kPopFrameCount, controls::PopFrameName, kind ); break;
		case K_FLASH: SetParamInfo( id, "Flash", FF_TYPE_EVENT, false ); break;
		case K_AUTO_FLASH: options( id, "Auto Flash", controls::kAutoFlashCount, controls::AutoFlashName, kind ); break;
		case K_AFTER: options( id, "After", controls::kAfterCount, controls::AfterName, kind ); break;
		case K_HOLES: options( id, "Holes", controls::kHolesCount, controls::HolesName, kind ); break;
		case K_SURROUND:
			//The source has no clip to show round the film.
			options( id, "Surround", isEffect ? controls::kSurroundCount : controls::kSurroundCount - 1, controls::SurroundName, kind );
			break;
		case K_MIX: SetParamInfo( id, "Mix", FF_TYPE_STANDARD, values[ kind ] ); break;
		default: break;
		}
		SetParamGroup( id, groupOf( kind ) );
	}

	//The About block. Inline: SetParamInfo is protected on CFFGLPlugin.
	const unsigned int about = AboutFirst();
	SetParamInfo( about, "About", FF_TYPE_TEXT, stoatworks::about::defaultText() );
	{
		unsigned int id = about + 1;
		for( const auto& b : stoatworks::about::buttons() )
			SetParamInfo( id++, b.label, FF_TYPE_EVENT, false );
	}
	for( unsigned int i = about; i < ParamCount(); ++i )
		SetParamGroup( i, "About" );
}

int GaugePlugin::option( Kind kind, int count ) const
{
	return controls::OptionIndex( values[ kind ], count );
}

//---------------------------------------------------------------------------
FFResult GaugePlugin::InitGL( const FFGLViewportStruct* vp )
{
	diag::info( std::string( "GL vendor=" ) + glStringOrUnknown( GL_VENDOR ) + " renderer=" + glStringOrUnknown( GL_RENDERER )
	            + " version=" + glStringOrUnknown( GL_VERSION ) );

	if( !compileShaders() )
	{
		DeInitGL();
		return FF_FAIL;
	}
	if( !quad.Initialise() )
	{
		diag::error( "quad geometry failed to initialise" );
		FFGLLog::LogToHost( "Gauge: quad geometry failed to initialise" );
		DeInitGL();
		return FF_FAIL;
	}

	//The fixed textures: the edge print's 5x7 font, the leader's atlas and its
	//metrics. Uploaded once.
	{
		const std::vector< uint8_t > font = font::Texture();
		glGenTextures( 1, &fontTexture );
		glBindTexture( GL_TEXTURE_2D, fontTexture );
		glPixelStorei( GL_UNPACK_ALIGNMENT, 1 );
		glTexImage2D( GL_TEXTURE_2D, 0, GL_R8, font::kTextureWidth, font::kTextureHeight, 0, GL_RED, GL_UNSIGNED_BYTE, font.data() );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );

		const lettering::Atlas& atlas = lettering::BuiltinAtlas();
		glGenTextures( 1, &glyphTexture );
		glBindTexture( GL_TEXTURE_2D, glyphTexture );
		glTexImage2D( GL_TEXTURE_2D, 0, GL_R8, lettering::Atlas::kWidth, lettering::Atlas::kHeight, 0, GL_RED, GL_UNSIGNED_BYTE,
		              atlas.pixels.data() );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );

		std::vector< float > metrics( static_cast< size_t >( lettering::kGlyphCount ) * 2 * 4, 0.0f );
		for( int g = 0; g < lettering::kGlyphCount; ++g )
		{
			const lettering::Atlas::Glyph& glyph = atlas.glyph[ g ];
			float* row0 = metrics.data() + static_cast< size_t >( g ) * 4;
			float* row1 = metrics.data() + static_cast< size_t >( lettering::kGlyphCount + g ) * 4;
			row0[ 0 ] = glyph.originX;
			row0[ 1 ] = glyph.originY;
			row0[ 2 ] = glyph.advance;
			row0[ 3 ] = glyph.inkX0;
			row1[ 0 ] = glyph.inkX1;
		}
		glGenTextures( 1, &metricsTexture );
		glBindTexture( GL_TEXTURE_2D, metricsTexture );
		glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA32F, lettering::kGlyphCount, 2, 0, GL_RGBA, GL_FLOAT, metrics.data() );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST );

		glGenTextures( 1, &contentTexture );
		glBindTexture( GL_TEXTURE_2D, contentTexture );
		glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA32I, kMaxVisible, 1, 0, GL_RGBA_INTEGER, GL_INT, nullptr );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST );

		glGenTextures( 1, &spectrumTexture );
		glBindTexture( GL_TEXTURE_2D, spectrumTexture );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
		glBindTexture( GL_TEXTURE_2D, 0 );
		spectrumRows = 0;
	}

	started = false;
	diag::info( "initialised" );
	return CFFGLPlugin::InitGL( vp );
}

bool GaugePlugin::compileShaders()
{
	const std::string vertex = shaders::Vertex();
	struct
	{
		FFGLShader* shader;
		std::string fragment;
		const char* name;
	} const stages[] = {
		{ &captureShader, shaders::Capture(), "capture" },
		{ &compositeShader, shaders::Composite(), "composite" },
		{ &copyShader, shaders::Copy(), "copy" },
	};
	for( const auto& stage : stages )
	{
		if( stage.shader->Compile( vertex, stage.fragment ) )
			continue;
		//FF_FAIL here is invisible to the operator: the plugin simply does
		//nothing. These two lines are the only record of which pass it was.
		diag::error( std::string( "the " ) + stage.name + " shader failed to compile - the plugin will do nothing" );
		FFGLLog::LogToHost( "Gauge: shader failed to compile" );
		return false;
	}
	return true;
}

FFResult GaugePlugin::DeInitGL()
{
	captureShader.FreeGLResources();
	compositeShader.FreeGLResources();
	copyShader.FreeGLResources();
	quad.Release();
	ring.Release();
	for( GLuint* t : { &contentTexture, &spectrumTexture, &fontTexture, &glyphTexture, &metricsTexture } )
	{
		if( *t != 0 )
			glDeleteTextures( 1, t );
		*t = 0;
	}
	spectrumRows = 0;
	return FF_SUCCESS;
}

//---------------------------------------------------------------------------
FFResult GaugePlugin::SetTime( double time )
{
	hostTimeSeen = true;
	return CFFGLPlugin::SetTime( time );
}

void GaugePlugin::SetBeatInfo( float bpm, float barPhase )
{
	CFFGLPlugin::SetBeatInfo( bpm, barPhase );
	transport.SetBeatInfo( bpm, barPhase );
}

//The unit voting is readout's, unchanged: the ratio of the host's clock delta
//to a steady clock's names the unit outright.
double GaugePlugin::nowSeconds()
{
	const double wallNow = wallSeconds();
	if( wallStart < 0.0 )
		wallStart = wallNow;
	if( !hostTimeSeen || hostTime < 0.0 )
		return wallNow - wallStart;

	const double raw = hostTime;
	if( clockScale == 0.0 && lastRawTime >= 0.0 && lastWallTime >= 0.0 )
	{
		const double hostDelta = raw - lastRawTime;
		const double wallDelta = wallNow - lastWallTime;
		if( hostDelta > 0.0 && wallDelta >= 0.0005 )
		{
			const double ratio = hostDelta / wallDelta;
			if( ratio > 0.1 && ratio < 10.0 )
				++secondsVotes;
			else if( ratio > 100.0 && ratio < 10000.0 )
				++millisVotes;
			if( secondsVotes >= kClockVotes || millisVotes >= kClockVotes )
				clockScale = millisVotes > secondsVotes ? 0.001 : 1.0;
		}
	}
	lastRawTime  = raw;
	lastWallTime = wallNow;
	return clockScale != 0.0 ? raw * clockScale : wallNow - wallStart;
}

void GaugePlugin::readAudio()
{
	if( audioForTest )
		return;
	const int index = hostIndex[ K_AUDIO ];
	const ParamInfo* info = index >= 0 ? FindParamInfo( static_cast< unsigned int >( index ) ) : nullptr;
	audio.fill( 0.0f );
	if( info == nullptr )
		return;
	const size_t bins = std::min< size_t >( info->elements.size(), kAudioBins );
	for( size_t i = 0; i < bins; ++i )
		//sqrt, as regauss does: bin magnitudes bunch hard against zero, and a
		//spectrum used raw moves the track for the kick drum and nothing else.
		audio[ i ] = std::sqrt( std::max( 0.0f, info->elements[ i ].value ) );
}

//---------------------------------------------------------------------------
// What frame `index` shows. The leader and the flashes are spliced onto the
// film, so they win over the picture; then the picture, live or held.
//---------------------------------------------------------------------------
void GaugePlugin::contentFor( int64_t index, int& kind, int& valueOut ) const
{
	valueOut = 0;
	for( int64_t f : flashes )
		if( index == f || ( ( perturb & kPerturbFlashLong ) && index == f + 1 ) )
		{
			kind = C_FLASH;
			return;
		}

	const bool loop = !isEffect && values[ K_LOOP ] > 0.5f;
	if( leaderStart != INT64_MIN )
	{
		const int64_t j = ( index - leaderStart ) * leaderDirection;
		if( loop )
		{
			kind     = C_LEADER;
			valueOut = static_cast< int >( floorMod( j, Leader::kLength ) );
			return;
		}
		if( j >= 0 && j < Leader::kLength )
		{
			kind     = C_LEADER;
			valueOut = static_cast< int >( j );
			return;
		}
	}

	if( !isEffect )
	{
		kind = option( K_AFTER, controls::kAfterCount ) == controls::kAfterClear ? C_CLEAR : C_BLACK;
		return;
	}

	if( option( K_FRAMES, controls::kFramesCount ) == controls::kFramesLive )
	{
		kind = C_LIVE;
		return;
	}
	if( ring.Holds( index ) )
	{
		kind     = C_HELD;
		valueOut = ring.SlotOf( index );
		return;
	}
	kind = C_LIVE;
}

bool GaugePlugin::ContentForTest( int64_t index, int& kind, int& valueOut ) const
{
	if( index < drawnLo || index >= drawnLo + static_cast< int64_t >( drawnKind.size() ) )
		return false;
	kind     = drawnKind[ static_cast< size_t >( index - drawnLo ) ];
	valueOut = drawnValue[ static_cast< size_t >( index - drawnLo ) ];
	return true;
}

//---------------------------------------------------------------------------
// The frame a start lands on, `overshoot` seconds after the boundary it
// waited for (0 for a press, which takes the next frame). Cell: the first
// frame whose pull-down is at or after the boundary, so at Rate Beat on a
// bar it is the frame that starts exactly on the bar. Strip: the next frame
// to come into view, from whichever end the film is coming.
//---------------------------------------------------------------------------
int64_t GaugePlugin::startIndex( bool press, double overshoot ) const
{
	if( option( K_MODE, controls::kModeCount ) == controls::kModeStrip )
		return travelSign >= 0 ? state.visibleHi + 1 : state.visibleLo - 1;
	if( press )
		return state.gate + 1;
	const double boundary = state.film - state.rate * overshoot;
	return static_cast< int64_t >( std::ceil( boundary - 1e-6 ) );
}

void GaugePlugin::fire( bool leader, int64_t at )
{
	if( leader )
	{
		leaderStart     = at;
		leaderDirection = option( K_MODE, controls::kModeCount ) == controls::kModeStrip && travelSign < 0 ? -1 : 1;
		state.leaderStart = at;
		return;
	}
	flashes.push_back( at );
	while( static_cast< int >( flashes.size() ) > kMaxFlashes )
		flashes.pop_front();
}

//---------------------------------------------------------------------------
FFResult GaugePlugin::ProcessOpenGL( ProcessOpenGLStruct* pGL )
{
	const FFGLTextureStruct* input = nullptr;
	if( isEffect )
	{
		if( pGL->numInputTextures < 1 || pGL->inputTextures[ 0 ] == nullptr )
			return FF_FAIL;
		input = pGL->inputTextures[ 0 ];
		if( input->Width == 0 || input->Height == 0 )
			return FF_FAIL;
	}

	//The host's viewport, before anything of ours changes it.
	GLint hostViewport[ 4 ] = { 0, 0, 0, 0 };
	glGetIntegerv( GL_VIEWPORT, hostViewport );
	const int width  = isEffect ? static_cast< int >( input->Width ) : std::max( hostViewport[ 2 ], 1 );
	const int height = isEffect ? static_cast< int >( input->Height ) : std::max( hostViewport[ 3 ], 1 );

	//-----------------------------------------------------------------------
	// The clock. Everything absolute stays here, in double.
	//-----------------------------------------------------------------------
	const double now = nowSeconds();
	double dt        = 0.0;
	if( lastNow >= 0.0 )
		dt = std::clamp( now - lastNow, 0.0, kMaxFrameDelta );
	lastNow = now;
	transport.Advance( dt );
	readAudio();

	//-----------------------------------------------------------------------
	// What the controls say.
	//-----------------------------------------------------------------------
	const int formatId = option( K_FORMAT, strip::kFormatCount );
	strip::Format format = strip::FormatAt( formatId );
	if( perturb & kPerturbFrameLine )
		format.holeOffset = 0.0;//through a hole, as 16 mm is
	if( ( perturb & kPerturbImageCentred ) && format.dts.Present() )
	{
		const double w  = format.ImageWidth();
		format.imageX0 = 0.5 * format.width - 0.5 * w;
		format.imageX1 = 0.5 * format.width + 0.5 * w;
	}

	const int stock       = option( K_STOCK, controls::kStockCount );
	const StockLook& look = lookOf( stock );
	double squeeze        = controls::SqueezeFactor( option( K_SQUEEZE, controls::kSqueezeCount ), format.squeeze );
	if( perturb & kPerturbSqueeze )
		squeeze = 1.0;
	const int fit       = isEffect ? option( K_FIT, controls::kFitCount ) : controls::kFitCrop;
	const int mode      = option( K_MODE, controls::kModeCount );
	const int rateIndex = option( K_RATE, controls::kRateCount );
	const bool held     = isEffect && option( K_FRAMES, controls::kFramesCount ) == controls::kFramesHeld;
	const int startOn   = option( K_START_ON, controls::kStartOnCount );
	const int autoFlash = option( K_AUTO_FLASH, controls::kAutoFlashCount );

	const double rate = rateIndex == controls::kRateBeat ? Leader::kCount * transport.Bpm() / 60.0 : controls::RateFps( rateIndex );

	//-----------------------------------------------------------------------
	// The film. At Rate Beat the clock IS the transport, 24 frames a beat from
	// beat 0, so a count is a beat and frame 0 of every 96 is a bar. At the
	// fixed rates it runs at the rate.
	//-----------------------------------------------------------------------
	if( rateIndex == controls::kRateBeat )
		filmClock = Leader::kCount * transport.Beats();
	else
		filmClock += rate * dt;
	const int64_t gate = static_cast< int64_t >( std::floor( filmClock + 1e-9 ) );

	if( formatId != lastFormat )
	{
		//Exposures on another gauge are another film.
		ring.Forget();
		lastFormat = formatId;
	}
	if( mode != lastMode )
	{
		//A strip starts with the gate's frame in the middle, so the frames the
		//cell held are the ones the strip shows first.
		if( mode == controls::kModeStrip )
			stripOffset = static_cast< double >( gate ) + 0.5;
		lastMode = mode;
	}

	const double travel = controls::TravelFps( values[ K_TRAVEL ] );
	const double driftFps = controls::DriftFps( values[ K_DRIFT ] );
	drift += driftFps * dt;
	drift -= std::floor( drift + 0.5 );//a rolling frame line, wrapped to a frame
	stripOffset += travel * dt;
	if( travel != 0.0 )
		travelSign = travel > 0.0 ? 1 : -1;

	state.film = filmClock;
	state.gate = gate;
	state.rate = rate;
	state.stripFrames = stripOffset;

	//-----------------------------------------------------------------------
	// Presses, and the beats they wait for. A press with Start On Press takes
	// the next frame; otherwise it waits for the next beat or bar (one this
	// frame sits on counts) and lands on the frame that boundary starts.
	//-----------------------------------------------------------------------
	const double beats  = transport.Beats();
	const double period = startOn == controls::kStartBar ? Transport::kBeatsPerBar : 1.0;
	auto pendingTarget  = [ & ]() {
		const double t = std::ceil( beats / period - 1e-9 ) * period;
		return t;
	};
	if( rollPresses > 0 )
	{
		rollPresses = 0;
		if( startOn == controls::kStartPress )
			fire( true, startIndex( true, 0.0 ) );
		else
		{
			pendingLeader     = true;
			pendingLeaderBeat = pendingTarget();
		}
	}
	if( flashPresses > 0 )
	{
		flashPresses = 0;
		if( startOn == controls::kStartPress )
			fire( false, startIndex( true, 0.0 ) );
		else
		{
			pendingFlash     = true;
			pendingFlashBeat = pendingTarget();
		}
	}
	const double beatSeconds = transport.BeatSeconds();
	if( pendingLeader && beats >= pendingLeaderBeat - 1e-9 )
	{
		pendingLeader = false;
		fire( true, startIndex( false, ( beats - pendingLeaderBeat ) * beatSeconds ) );
	}
	if( pendingFlash && beats >= pendingFlashBeat - 1e-9 )
	{
		pendingFlash = false;
		fire( false, startIndex( false, ( beats - pendingFlashBeat ) * beatSeconds ) );
	}
	if( autoFlash != controls::kAutoOff && started )
	{
		const double every = autoFlash == controls::kAutoBar ? Transport::kBeatsPerBar : 1.0;
		const double crossed = std::floor( beats / every + 1e-9 );
		if( crossed > std::floor( lastBeats / every + 1e-9 ) )
			fire( false, startIndex( false, ( beats - crossed * every ) * beatSeconds ) );
	}
	lastBeats = beats;

	//The source's leader runs from the start: looping, from frame 0 at Rate
	//Beat (so it lands on the bars) or from the first frame otherwise; once,
	//from the first frame.
	if( !isEffect && !started && leaderStart == INT64_MIN )
	{
		leaderStart     = rateIndex == controls::kRateBeat && values[ K_LOOP ] > 0.5f ? 0 : gate;
		leaderDirection = 1;
	}
	state.leaderStart = leaderStart;

	//-----------------------------------------------------------------------
	// The camera, and the frames it can see -- through the whole shutter.
	//-----------------------------------------------------------------------
	camera::Camera view;
	double moveFrames = 0.0;//how far the film moves during the exposure, frames
	const double shutter = controls::ShutterDegrees( values[ K_SHUTTER ] ) / 360.0;
	if( mode == controls::kModeStrip )
	{
		view = camera::Strip( format, width, height, controls::SizeFraction( values[ K_SIZE ] ),
		                      controls::AngleDegrees( values[ K_ANGLE ] ), controls::PositionFraction( values[ K_POSITION ] ),
		                      stripOffset );
		moveFrames = travel * dt * shutter;
	}
	else
	{
		const double centre = static_cast< double >( gate ) + 0.5 + controls::RackFrames( values[ K_RACK ] ) + drift;
		view = camera::Cell( format, width, height, controls::Framing( values[ K_FRAMING ] ), centre );
		moveFrames = driftFps * dt * shutter;
	}
	const double moveMm = moveFrames * format.pitch;
	int taps            = 1;
	double tapStep      = 0.0;
	if( std::fabs( moveMm ) > 0.0 )
	{
		taps    = std::clamp( static_cast< int >( std::ceil( std::fabs( moveMm ) / view.mmPerPixel ) ), 1, 16 );
		tapStep = taps > 1 ? moveMm / taps : 0.0;
	}

	double lo = 0.0, hi = 0.0;
	camera::AlongRange( view, width, height, lo, hi );
	lo = std::min( lo, lo - taps * tapStep );
	hi = std::max( hi, hi - taps * tapStep );
	int64_t dLo = static_cast< int64_t >( std::floor( lo / format.pitch ) ) - 1;
	int64_t dHi = static_cast< int64_t >( std::floor( hi / format.pitch ) ) + 1;
	if( dHi - dLo + 1 > kMaxVisible )
	{
		//More frames than the table holds (a tiny strip): keep the middle.
		const int64_t mid = ( dLo + dHi ) / 2;
		dLo = mid - kMaxVisible / 2;
		dHi = dLo + kMaxVisible - 1;
	}
	//Re-base the camera on the first visible frame, so the shader's frame
	//numbers run from 0.
	view.originY -= static_cast< double >( dLo ) * format.pitch;
	view.base += dLo;
	const int count       = static_cast< int >( dHi - dLo + 1 );
	const int64_t first   = view.base;
	state.camera          = view;
	state.visibleLo       = first;
	state.visibleHi       = first + count - 1;

	//-----------------------------------------------------------------------
	// The held frames: big enough for every visible frame and a few more, at
	// the size the camera draws a picture (with some hysteresis, so dragging a
	// slider does not reallocate every frame), within a budget.
	//-----------------------------------------------------------------------
	const double pictureW = format.imageAcross ? format.imageHeight : format.ImageWidth();
	const double pictureH = format.imageAcross ? format.ImageWidth() : format.imageHeight;
	if( isEffect )
	{
		double cellW      = pictureW / view.mmPerPixel;
		double cellH      = pictureH / view.mmPerPixel;
		const int layers  = count + 4;
		const double load = cellW * cellH * layers;
		if( load > kHeldBudget )
		{
			const double k = std::sqrt( kHeldBudget / load );
			cellW *= k;
			cellH *= k;
		}
		auto quantise = []( double px ) { return std::clamp( 16 * static_cast< int >( std::ceil( px / 16.0 ) ), 16, 2048 ); };
		int wantW = quantise( cellW ), wantH = quantise( cellH );
		const bool tooSmall = wantW > ring.Width() * 1.19 || wantH > ring.Height() * 1.19;
		const bool tooBig   = wantW < ring.Width() / 1.68 || wantH < ring.Height() / 1.68;
		if( ring.Texture() != 0 && !tooSmall && !tooBig )
		{
			wantW = ring.Width();
			wantH = ring.Height();
		}
		const int before = ring.Width() * 100000 + ring.Height();
		if( !ring.Ensure( wantW, wantH, layers, copyShader.GetGLID(), quad ) )
		{
			diag::error( "could not allocate the held frames at " + std::to_string( wantW ) + "x" + std::to_string( wantH ) + "x"
			             + std::to_string( layers ) );
			return FF_FAIL;
		}
		if( ( perturb & kPerturbResizeForget ) && before != ring.Width() * 100000 + ring.Height() )
			ring.Forget();
	}
	else if( ring.Texture() == 0 && !ring.Ensure( 16, 16, 1, 0, quad ) )
		return FF_FAIL;
	state.heldWidth  = ring.Width();
	state.heldHeight = ring.Height();
	state.heldLayers = ring.Layers();

	//-----------------------------------------------------------------------
	// The window of the clip the picture shows: the aperture's aspect as it is
	// meant to be seen, which is the aperture's own times the squeeze.
	//-----------------------------------------------------------------------
	const double displayAspect = pictureW / pictureH * squeeze;
	float window[ 4 ]          = { 0.0f, 0.0f, 1.0f, 1.0f };
	const double inputAspect   = static_cast< double >( width ) / static_cast< double >( height );
	if( fit == controls::kFitCrop || fit == controls::kFitLetterbox )
	{
		const bool wider = inputAspect > displayAspect;
		const double k   = wider ? displayAspect / inputAspect : inputAspect / displayAspect;
		//Crop shows a part (k < 1 of one side); letterbox shows all and pads
		//the other side (1 / k of it).
		const double extent = fit == controls::kFitCrop ? k : 1.0 / k;
		if( wider == ( fit == controls::kFitCrop ) )
		{
			window[ 2 ] = static_cast< float >( extent );
			window[ 0 ] = static_cast< float >( 0.5 * ( 1.0 - extent ) );
		}
		else
		{
			window[ 3 ] = static_cast< float >( extent );
			window[ 1 ] = static_cast< float >( 0.5 * ( 1.0 - extent ) );
		}
	}

	//-----------------------------------------------------------------------
	// What each visible frame shows, and which need exposing now: in Cell the
	// gate's frame every host frame (it is being exposed until it is pulled
	// down), on a Strip every frame the first time it is seen.
	//-----------------------------------------------------------------------
	std::vector< int64_t > expose;
	drawnLo = first;
	drawnKind.assign( static_cast< size_t >( count ), 0 );
	drawnValue.assign( static_cast< size_t >( count ), 0 );
	auto isPicture = [ & ]( int64_t index ) {
		int kind = 0, v = 0;
		contentFor( index, kind, v );
		return kind == C_LIVE || kind == C_HELD;
	};
	if( held )
	{
		if( mode == controls::kModeCell )
		{
			if( isPicture( gate ) )
				expose.push_back( gate );
		}
		else
			for( int64_t i = first; i < first + count; ++i )
				if( isPicture( i ) && ( !ring.Holds( i ) || ( perturb & kPerturbLiveHeld ) ) )
					expose.push_back( i );
	}

	if( !expose.empty() )
	{
		const GLuint program = captureShader.GetGLID();
		glUseProgram( program );
		glActiveTexture( GL_TEXTURE0 );
		glBindTexture( GL_TEXTURE_2D, input->Handle );
		const FFGLTexCoords maxCoords = GetMaxGLTexCoords( *input );
		glUniform1i( glGetUniformLocation( program, "InputTexture" ), 0 );
		glUniform2f( glGetUniformLocation( program, "MaxUV" ), maxCoords.s, maxCoords.t );
		glUniform4f( glGetUniformLocation( program, "Window" ), window[ 0 ], window[ 1 ], window[ 2 ], window[ 3 ] );
		glUniform2f( glGetUniformLocation( program, "InputSize" ), static_cast< float >( width ), static_cast< float >( height ) );
		glUniform2i( glGetUniformLocation( program, "CellSize" ), ring.Width(), ring.Height() );
		for( int64_t i : expose )
		{
			const int slot = ring.BeginSlot( i );
			quad.Draw();
			ring.Tag( i );
			//The track is printed with the picture: the spectrum as it was, its
			//phases at this frame's place on the film.
			const size_t row = static_cast< size_t >( slot ) * 128;
			if( spectrum.size() < row + 128 )
				spectrum.resize( row + 128, 0.0f );
			for( int k = 0; k < kAudioBins; ++k )
			{
				const double f = ( k + 0.5 ) * kBinHz;
				spectrum[ row + static_cast< size_t >( k ) ] = audio[ static_cast< size_t >( k ) ];
				const double phase = 2.0 * kPi * std::fmod( f * static_cast< double >( i ) / std::max( rate, 1e-6 ), 1.0 )
				                     + 2.0 * kPi * ( ( k * 0.618034 ) - std::floor( k * 0.618034 ) );
				spectrum[ row + 64 + static_cast< size_t >( k ) ] = static_cast< float >( std::fmod( phase, 2.0 * kPi ) );
			}
		}
		ring.Unbind();
		glBindTexture( GL_TEXTURE_2D, 0 );
		glUseProgram( 0 );
	}

	//-----------------------------------------------------------------------
	// The tables the composite reads: content per frame, and the spectrum.
	//-----------------------------------------------------------------------
	const int liveRow = ring.Layers();
	const int stepRow = liveRow + 1;
	const int rows    = stepRow + 1;
	spectrum.resize( static_cast< size_t >( rows ) * 128, 0.0f );
	for( int k = 0; k < kAudioBins; ++k )
	{
		const double f    = ( k + 0.5 ) * kBinHz;
		const double step = 2.0 * kPi * std::fmod( f / std::max( rate, 1e-6 ), 1.0 );
		const double at   = 2.0 * kPi * std::fmod( f * static_cast< double >( first ) / std::max( rate, 1e-6 ), 1.0 )
		                  + 2.0 * kPi * ( ( k * 0.618034 ) - std::floor( k * 0.618034 ) );
		spectrum[ static_cast< size_t >( liveRow ) * 128 + static_cast< size_t >( k ) ]      = audio[ static_cast< size_t >( k ) ];
		spectrum[ static_cast< size_t >( liveRow ) * 128 + 64 + static_cast< size_t >( k ) ] = static_cast< float >( std::fmod( at, 2.0 * kPi ) );
		spectrum[ static_cast< size_t >( stepRow ) * 128 + static_cast< size_t >( k ) ]      = static_cast< float >( step );
	}

	std::vector< int > table( static_cast< size_t >( kMaxVisible ) * 4, 0 );
	for( int d = 0; d < count; ++d )
	{
		int kind = 0, v = 0;
		contentFor( first + d, kind, v );
		const int row = kind == C_HELD ? v : kind == C_LIVE ? liveRow : -1;
		table[ static_cast< size_t >( d ) * 4 + 0 ] = kind;
		table[ static_cast< size_t >( d ) * 4 + 1 ] = v;
		table[ static_cast< size_t >( d ) * 4 + 2 ] = row;
		drawnKind[ static_cast< size_t >( d ) ]  = kind;
		drawnValue[ static_cast< size_t >( d ) ] = v;
	}
	glBindTexture( GL_TEXTURE_2D, contentTexture );
	glTexSubImage2D( GL_TEXTURE_2D, 0, 0, 0, kMaxVisible, 1, GL_RGBA_INTEGER, GL_INT, table.data() );
	glBindTexture( GL_TEXTURE_2D, spectrumTexture );
	if( rows != spectrumRows )
	{
		glTexImage2D( GL_TEXTURE_2D, 0, GL_R32F, 128, rows, 0, GL_RED, GL_FLOAT, spectrum.data() );
		spectrumRows = rows;
	}
	else
		glTexSubImage2D( GL_TEXTURE_2D, 0, 0, 0, 128, rows, GL_RED, GL_FLOAT, spectrum.data() );
	glBindTexture( GL_TEXTURE_2D, 0 );

	//-----------------------------------------------------------------------
	// The composite, straight to the host.
	//-----------------------------------------------------------------------
	glBindFramebuffer( GL_FRAMEBUFFER, pGL->HostFBO );
	glViewport( hostViewport[ 0 ], hostViewport[ 1 ], hostViewport[ 2 ], hostViewport[ 3 ] );
	{
		FFGLShader& s = compositeShader;
		ScopedShaderBinding shaderBinding( s.GetGLID() );
		ScopedSamplerActivation unit0( 0 );
		//The source has no clip; its sampler gets a real texture it never reads,
		//because a sampler on texture 0 is "unloadable" to some drivers.
		Scoped2DTextureBinding inputBinding( isEffect ? input->Handle : fontTexture );
		ScopedSamplerActivation unit1( 1 );
		ScopedTextureBinding heldBinding( GL_TEXTURE_2D_ARRAY, ring.Texture() );
		ScopedSamplerActivation unit2( 2 );
		Scoped2DTextureBinding contentBinding( contentTexture );
		ScopedSamplerActivation unit3( 3 );
		Scoped2DTextureBinding spectrumBinding( spectrumTexture );
		ScopedSamplerActivation unit4( 4 );
		Scoped2DTextureBinding fontBinding( fontTexture );
		ScopedSamplerActivation unit5( 5 );
		Scoped2DTextureBinding glyphBinding( glyphTexture );
		ScopedSamplerActivation unit6( 6 );
		Scoped2DTextureBinding metricsBinding( metricsTexture );

		s.Set( "InputTexture", 0 );
		s.Set( "Held", 1 );
		s.Set( "ContentTable", 2 );
		s.Set( "Spectrum", 3 );
		s.Set( "Font", 4 );
		s.Set( "Glyphs", 5 );
		s.Set( "Metrics", 6 );

		glUniform2i( s.FindUniform( "Size" ), width, height );
		s.Set( "Origin", static_cast< float >( view.originX ), static_cast< float >( view.originY ) );
		s.Set( "AxisX", static_cast< float >( view.axisXx ), static_cast< float >( view.axisXy ) );
		s.Set( "AxisY", static_cast< float >( view.axisYx ), static_cast< float >( view.axisYy ) );
		s.Set( "MmPerPixel", static_cast< float >( view.mmPerPixel ) );
		s.Set( "Taps", taps );
		s.Set( "TapStep", static_cast< float >( tapStep ) );

		s.Set( "FilmWidth", static_cast< float >( format.width ) );
		s.Set( "Pitch", static_cast< float >( format.pitch ) );
		s.Set( "HoleSize", static_cast< float >( format.hole.across ), static_cast< float >( format.hole.along ),
		       static_cast< float >( format.hole.radius ) );
		s.Set( "HoleGrid", static_cast< float >( format.holePitch ), static_cast< float >( format.holeOffset ),
		       static_cast< float >( format.holeEdge ) );
		glUniform2i( s.FindUniform( "HoleRows" ), format.holesNear ? 1 : 0, format.holesFar ? 1 : 0 );
		s.Set( "Image", static_cast< float >( format.imageX0 ), static_cast< float >( format.imageX1 ),
		       static_cast< float >( format.ImageY0() ), static_cast< float >( format.ImageY1() ) );
		s.Set( "ImageAcross", format.imageAcross ? 1 : 0 );
		s.Set( "DisplayAspect", static_cast< float >( displayAspect ) );

		const bool tracks = values[ K_SOUNDTRACKS ] > 0.5f;
		s.Set( "Soundtracks", tracks ? 1 : 0 );
		s.Set( "Analogue", static_cast< float >( format.analogue.x0 ), static_cast< float >( format.analogue.x1 ) );
		s.Set( "Channels", format.analogue.Present() ? format.channels : 0 );
		s.Set( "Dts", static_cast< float >( format.dts.x0 ), static_cast< float >( format.dts.x1 ) );
		s.Set( "Dolby", format.dolby ? 1 : 0 );
		s.Set( "Sdds", static_cast< float >( format.sdds.x0 ), static_cast< float >( format.sdds.x1 ) );

		s.Set( "EdgePrint", values[ K_EDGE_PRINT ] > 0.5f && format.print.Present() ? 1 : 0 );
		s.Set( "PrintFar", static_cast< float >( format.print.x0 ), static_cast< float >( format.print.x1 ) );
		s.Set( "PrintNear", static_cast< float >( format.printNear.x0 ), static_cast< float >( format.printNear.x1 ) );
		s.Set( "FramesPerLabel", format.framesPerLabel );
		s.Set( "GlyphUp", format.glyphUp );
		s.Set( "GlyphAdvance", format.glyphAdvance );
		int code[ 8 ] = {};
		for( int i = 0; i < 8 && look.code[ i ] != '\0'; ++i )
			code[ i ] = static_cast< unsigned char >( look.code[ i ] );
		glUniform4i( s.FindUniform( "Code0" ), code[ 0 ], code[ 1 ], code[ 2 ], code[ 3 ] );
		glUniform4i( s.FindUniform( "Code1" ), code[ 4 ], code[ 5 ], code[ 6 ], code[ 7 ] );
		const int64_t perLabel = std::max( format.framesPerLabel, 1 );
		s.Set( "LabelBase", static_cast< int >( floorMod( floorDiv( first, perLabel ), 10000 ) ) );
		s.Set( "LabelRem", static_cast< int >( floorMod( first, perLabel ) ) );
		s.Set( "HashBase", static_cast< int >( floorMod( first, int64_t( 1 ) << 24 ) ) );
		s.Set( "StillBase", static_cast< int >( floorMod( first, 37 ) ) );

		s.Set( "BaseColour", look.base[ 0 ], look.base[ 1 ], look.base[ 2 ] );
		s.Set( "InkColour", look.ink[ 0 ], look.ink[ 1 ], look.ink[ 2 ] );
		s.Set( "TrackDense", look.dense[ 0 ], look.dense[ 1 ], look.dense[ 2 ] );
		s.Set( "TrackClear", look.clear[ 0 ], look.clear[ 1 ], look.clear[ 2 ] );
		s.Set( "BlackColour", look.black[ 0 ], look.black[ 1 ], look.black[ 2 ] );
		s.Set( "Mono", look.mono ? 1 : 0 );

		const int holes = option( K_HOLES, controls::kHolesCount );
		if( holes == controls::kHolesClear && !( perturb & kPerturbHolesOpaque ) )
			s.Set( "HoleColour", 0.0f, 0.0f, 0.0f, 0.0f );
		else if( holes == controls::kHolesLight )
			s.Set( "HoleColour", 1.0f, 1.0f, 1.0f, 1.0f );
		else
			s.Set( "HoleColour", 0.0f, 0.0f, 0.0f, 1.0f );
		s.Set( "SurroundKind", option( K_SURROUND, isEffect ? controls::kSurroundCount : controls::kSurroundCount - 1 ) );

		s.Set( "FrameCount", count );
		const FFGLTexCoords maxCoords = isEffect ? GetMaxGLTexCoords( *input ) : FFGLTexCoords{ 1.0f, 1.0f };
		s.Set( "MaxUV", maxCoords.s, maxCoords.t );
		s.Set( "Window", window[ 0 ], window[ 1 ], window[ 2 ], window[ 3 ] );
		s.Set( "InputSize", static_cast< float >( width ), static_cast< float >( height ) );

		s.Set( "LiveRow", liveRow );
		s.Set( "StepRow", stepRow );
		s.Set( "FilmRate", static_cast< float >( rate ) );
		s.Set( "TrackLevel", controls::TrackLevel( values[ K_TRACK_LEVEL ] ) );
		s.Set( "BinHz", static_cast< float >( kBinHz ) );
		s.Set( "CountFrames", ( perturb & kPerturbLeaderCount ) ? Leader::kCount + 1 : Leader::kCount );
		s.Set( "PopFlash", option( K_POP_FRAME, controls::kPopFrameCount ) == controls::kPopFlash ? 1 : 0 );
		s.Set( "DigitHeight", Leader::kDigitHeight );
		s.Set( "DigitBold", Leader::kDigitBold );

		s.Set( "IsEffect", isEffect ? 1 : 0 );
		s.Set( "MixAmount", isEffect ? std::clamp( values[ K_MIX ], 0.0f, 1.0f ) : 1.0f );
		s.Set( "Perturb", perturb );
		quad.Draw();
	}

	started = true;
	return FF_SUCCESS;
}

//---------------------------------------------------------------------------
FFResult GaugePlugin::SetFloatParameter( unsigned int index, float v )
{
	if( index >= ParamCount() )
		return FF_FAIL;
	if( index >= AboutFirst() )
		return stoatworks::about::handleParam( index - AboutFirst(), v ) ? FF_SUCCESS : FF_FAIL;

	const Kind kind = order[ index ];
	//Events act on the rising edge, once.
	if( kind == K_ROLL_LEADER || kind == K_FLASH )
	{
		const bool down = v >= 0.5f;
		if( down && !pressed[ kind ] )
			++( kind == K_ROLL_LEADER ? rollPresses : flashPresses );
		pressed[ kind ] = down;
	}
	values[ kind ] = v;
	return FF_SUCCESS;
}

float GaugePlugin::GetFloatParameter( unsigned int index )
{
	if( index >= AboutFirst() )
		return 0.0f;
	return values[ order[ index ] ];
}

char* GaugePlugin::GetTextParameter( unsigned int index )
{
	if( index == AboutFirst() )
	{
		aboutText = stoatworks::about::textParam( 0 );
		return const_cast< char* >( aboutText.c_str() );
	}
	return CFFGLPlugin::GetTextParameter( index );
}

FFResult GaugePlugin::SetTextParameter( unsigned int index, const char* v )
{
	if( index == AboutFirst() )
		return FF_SUCCESS;
	return CFFGLPlugin::SetTextParameter( index, v );
}

//---------------------------------------------------------------------------
void GaugePlugin::SetClockScaleForTest( double scale )
{
	clockScale = scale;
}

void GaugePlugin::SetTransportForTest( double bpm, double barPhase )
{
	transport.SetBeatInfo( bpm, barPhase );
}

void GaugePlugin::SetPerturbForTest( int bits )
{
	perturb = bits;
}

void GaugePlugin::SetAudioForTest( const float* bins, int n )
{
	audioForTest = true;
	audio.fill( 0.0f );
	for( int i = 0; i < std::min( n, kAudioBins ); ++i )
		audio[ static_cast< size_t >( i ) ] = bins[ i ];
}

} // namespace gauge
