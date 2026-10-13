/**
	gutest -- render Gauge offline, and read the strip back out of it.

	Where the holes are, how thick the frame line is, what a 2x squeeze does
	to a disc, which picture each frame of a moving strip holds, and on which
	frame the leader's picture starts are facts with one right answer each.
	Every check renders through the REAL plugin class in a headless GL context
	on a synthetic clock, reads the pixels back as floats, and measures.

		gutest --out /tmp/frame.png     a picture (the effect on the test card)
		gutest --plugin source ...      the leader source instead
		gutest --list                   every parameter, its kind and default
		gutest --perfs                  hole size, pitch, edge distance, count
		gutest --aperture               picture size, place and frame line
		gutest --squeeze                a disc through 2x reads back 1/2 wide
		gutest --held                   a moving strip keeps its pictures
		gutest --leader                 the countdown, frame by frame, on time
		gutest --flash                  a flash is one film frame
		gutest --track                  the optical track's spatial frequency
		gutest --alpha                  clear holes and surround, opaque film
		gutest --resize                 held frames survive a resize
		gutest --negative               every check above can FAIL
		gutest --bench                  the render cost
		gutest --dump-shaders DIR       the exact GLSL the plugin compiles
		gutest --pipe                   raw frames in, raw frames out

	Every check takes --size; run each at two rasters at least, the one you
	develop at and 320x180, which is what CI uses. AGENTS.md has one line per
	check on where each tolerance comes from.
*/

#include "Camera.h"
#include "Controls.h"
#include "Gauge.h"
#include "Lettering.h"
#include "Shaders.h"
#include "Strip.h"

#include <OpenGL/OpenGL.h>
#include <OpenGL/gl3.h>
#include <zlib.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <unistd.h>
#include <utility>
#include <vector>

using namespace gauge;

namespace
{
constexpr double kPi = 3.14159265358979323846;

//---------------------------------------------------------------------------
// A PNG writer. zlib ships with the OS.
//---------------------------------------------------------------------------
void putU32( std::vector< unsigned char >& out, uint32_t v )
{
	out.push_back( static_cast< unsigned char >( v >> 24 ) );
	out.push_back( static_cast< unsigned char >( v >> 16 ) );
	out.push_back( static_cast< unsigned char >( v >> 8 ) );
	out.push_back( static_cast< unsigned char >( v ) );
}

void putChunk( std::vector< unsigned char >& out, const char* type, const std::vector< unsigned char >& data )
{
	putU32( out, static_cast< uint32_t >( data.size() ) );
	const size_t start = out.size();
	out.insert( out.end(), type, type + 4 );
	out.insert( out.end(), data.begin(), data.end() );
	uLong crc = crc32( 0L, Z_NULL, 0 );
	crc       = crc32( crc, out.data() + start, static_cast< uInt >( 4 + data.size() ) );
	putU32( out, static_cast< uint32_t >( crc ) );
}

bool writePng( const std::string& path, int width, int height, const std::vector< unsigned char >& rgba )
{
	std::vector< unsigned char > raw;
	raw.reserve( static_cast< size_t >( height ) * ( 1 + static_cast< size_t >( width ) * 4 ) );
	for( int y = 0; y < height; ++y )
	{
		raw.push_back( 0 );
		const unsigned char* row = rgba.data() + static_cast< size_t >( y ) * width * 4;
		raw.insert( raw.end(), row, row + static_cast< size_t >( width ) * 4 );
	}
	uLongf size = compressBound( static_cast< uLong >( raw.size() ) );
	std::vector< unsigned char > packedData( size );
	if( compress2( packedData.data(), &size, raw.data(), static_cast< uLong >( raw.size() ), 6 ) != Z_OK )
		return false;
	packedData.resize( size );

	std::vector< unsigned char > png = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };
	std::vector< unsigned char > ihdr;
	putU32( ihdr, static_cast< uint32_t >( width ) );
	putU32( ihdr, static_cast< uint32_t >( height ) );
	ihdr.push_back( 8 );
	ihdr.push_back( 6 );
	ihdr.push_back( 0 );
	ihdr.push_back( 0 );
	ihdr.push_back( 0 );
	putChunk( png, "IHDR", ihdr );
	putChunk( png, "IDAT", packedData );
	putChunk( png, "IEND", {} );

	FILE* file = fopen( path.c_str(), "wb" );
	if( file == nullptr )
		return false;
	const size_t written = fwrite( png.data(), 1, png.size(), file );
	fclose( file );
	return written == png.size();
}

//---------------------------------------------------------------------------
// Sources. Rows are top-first, and flipped on the way into GL; the checks
// read back through the same flip, so row 0 is the top everywhere here.
//---------------------------------------------------------------------------
using Image = std::vector< float >;//RGBA, top row first

/// The test card: sky over ground, a row of greys, colour patches, and a disc
/// on a slow Lissajous path so consecutive frames differ.
Image buildCard( int width, int height, int frame )
{
	Image card( static_cast< size_t >( width ) * height * 4 );
	const float w = static_cast< float >( width ), h = static_cast< float >( height ), t = static_cast< float >( frame );
	const float discX = 0.5f * w + 0.30f * w * std::sin( t * 0.05f );
	const float discY = 0.42f * h + 0.12f * h * std::sin( t * 0.037f + 1.1f );
	const float discR = 0.07f * h;
	for( int y = 0; y < height; ++y )
		for( int x = 0; x < width; ++x )
		{
			const float u = ( x + 0.5f ) / w, v = ( y + 0.5f ) / h;
			float r, g, b;
			if( v < 0.55f )
			{
				const float k = v / 0.55f;
				r = 0.25f + 0.45f * k;
				g = 0.45f + 0.35f * k;
				b = 0.85f - 0.05f * k;
			}
			else
			{
				r = 0.42f;
				g = 0.33f;
				b = 0.22f;
			}
			if( v > 0.84f )
			{
				const int step = std::min( 10, static_cast< int >( u * 11.0f ) );
				r = g = b = static_cast< float >( step ) / 10.0f;
			}
			else if( v > 0.66f && v < 0.80f )
			{
				static const float patches[ 8 ][ 3 ] = { { 0.75f, 0.12f, 0.10f }, { 0.20f, 0.60f, 0.20f }, { 0.12f, 0.20f, 0.75f },
					                                     { 0.10f, 0.65f, 0.70f }, { 0.70f, 0.15f, 0.60f }, { 0.90f, 0.80f, 0.15f },
					                                     { 0.87f, 0.64f, 0.52f }, { 0.03f, 0.03f, 0.03f } };
				const int p = std::min( 7, static_cast< int >( u * 8.0f ) );
				r = patches[ p ][ 0 ];
				g = patches[ p ][ 1 ];
				b = patches[ p ][ 2 ];
			}
			const float dx = x + 0.5f - discX, dy = y + 0.5f - discY;
			const float dist = std::sqrt( dx * dx + dy * dy );
			if( dist < discR )
			{
				const float e = std::min( 1.0f, ( discR - dist ) / ( discR * 0.2f ) );
				r += ( 0.95f - r ) * e;
				g += ( 0.55f - g ) * e;
				b += ( 0.15f - b ) * e;
			}
			float* p = card.data() + ( static_cast< size_t >( y ) * width + x ) * 4;
			p[ 0 ] = r;
			p[ 1 ] = g;
			p[ 2 ] = b;
			p[ 3 ] = 1.0f;
		}
	return card;
}

Image buildFlat( int width, int height, float level, float alpha = 1.0f )
{
	Image image( static_cast< size_t >( width ) * height * 4 );
	for( size_t i = 0; i < image.size(); i += 4 )
	{
		image[ i + 0 ] = image[ i + 1 ] = image[ i + 2 ] = level * alpha;
		image[ i + 3 ] = alpha;
	}
	return image;
}

/// The exact area of a disc ( centre cx, cy, radius R ) inside the box
/// [ x0, x1 ] x [ y0, y1 ]: the chord's span inside the box, integrated across
/// it by composite Simpson's rule over 64 panels -- the chord is smooth inside a
/// pixel, so the error is far below float.
double discArea( double cx, double cy, double R, double x0, double x1, double y0, double y1 )
{
	auto chord = [ & ]( double x ) {
		const double d = R * R - ( x - cx ) * ( x - cx );
		if( d <= 0.0 )
			return 0.0;
		const double h = std::sqrt( d );
		return std::max( 0.0, std::min( y1, cy + h ) - std::max( y0, cy - h ) );
	};
	const int n    = 64;
	const double w = ( x1 - x0 ) / n;
	double sum     = chord( x0 ) + chord( x1 );
	for( int i = 1; i < n; ++i )
		sum += ( i % 2 ? 4.0 : 2.0 ) * chord( x0 + i * w );
	return sum * w / 3.0;
}

/// A white disc of radius `r` (of the height) at the centre, on black. Each
/// pixel holds the exact fraction of it the disc covers, so the disc's
/// moments are a circle's to float, not to a supersampling grid's.
Image buildDisc( int width, int height, double r )
{
	Image image( static_cast< size_t >( width ) * height * 4, 0.0f );
	const double cx = 0.5 * width, cy = 0.5 * height, R = r * height;
	for( int y = 0; y < height; ++y )
		for( int x = 0; x < width; ++x )
		{
			//Wholly inside or wholly outside needs no integral.
			const double nx = std::clamp( cx, double( x ), double( x + 1 ) ) - cx;
			const double ny = std::clamp( cy, double( y ), double( y + 1 ) ) - cy;
			const double fx = std::max( std::fabs( x - cx ), std::fabs( x + 1 - cx ) );
			const double fy = std::max( std::fabs( y - cy ), std::fabs( y + 1 - cy ) );
			double cover;
			if( nx * nx + ny * ny >= R * R )
				cover = 0.0;
			else if( fx * fx + fy * fy <= R * R )
				cover = 1.0;
			else
				cover = discArea( cx, cy, R, x, x + 1, y, y + 1 );
			float* p = image.data() + ( static_cast< size_t >( y ) * width + x ) * 4;
			p[ 0 ] = p[ 1 ] = p[ 2 ] = static_cast< float >( cover );
			p[ 3 ] = 1.0f;
		}
	return image;
}

//---------------------------------------------------------------------------
// GL plumbing.
//---------------------------------------------------------------------------
CGLContextObj createContext( bool software )
{
	const CGLPixelFormatAttribute accelerated[] = {
		kCGLPFAOpenGLProfile, static_cast< CGLPixelFormatAttribute >( kCGLOGLPVersion_GL4_Core ), kCGLPFAAccelerated,
		kCGLPFAColorSize,     static_cast< CGLPixelFormatAttribute >( 24 ), kCGLPFAAlphaSize,
		static_cast< CGLPixelFormatAttribute >( 8 ), static_cast< CGLPixelFormatAttribute >( 0 )
	};
	const CGLPixelFormatAttribute softwareOnly[] = {
		kCGLPFAOpenGLProfile, static_cast< CGLPixelFormatAttribute >( kCGLOGLPVersion_GL4_Core ), kCGLPFARendererID,
		static_cast< CGLPixelFormatAttribute >( kCGLRendererGenericFloatID ), kCGLPFAColorSize,
		static_cast< CGLPixelFormatAttribute >( 24 ), kCGLPFAAlphaSize, static_cast< CGLPixelFormatAttribute >( 8 ),
		static_cast< CGLPixelFormatAttribute >( 0 )
	};
	const CGLPixelFormatAttribute any[] = {
		kCGLPFAOpenGLProfile, static_cast< CGLPixelFormatAttribute >( kCGLOGLPVersion_GL4_Core ), kCGLPFAColorSize,
		static_cast< CGLPixelFormatAttribute >( 24 ), kCGLPFAAlphaSize, static_cast< CGLPixelFormatAttribute >( 8 ),
		static_cast< CGLPixelFormatAttribute >( 0 )
	};
	CGLPixelFormatObj format = nullptr;
	GLint n                  = 0;
	if( software )
		CGLChoosePixelFormat( softwareOnly, &format, &n );
	else if( CGLChoosePixelFormat( accelerated, &format, &n ) != kCGLNoError || format == nullptr )
		CGLChoosePixelFormat( any, &format, &n );
	if( format == nullptr )
		return nullptr;
	CGLContextObj context = nullptr;
	const CGLError error  = CGLCreateContext( format, nullptr, &context );
	CGLDestroyPixelFormat( format );
	if( error != kCGLNoError )
		return nullptr;
	CGLSetCurrentContext( context );
	return context;
}

GLuint makeTexture( int width, int height, GLint internalFormat, GLenum type, const void* pixels )
{
	GLuint texture = 0;
	glGenTextures( 1, &texture );
	glBindTexture( GL_TEXTURE_2D, texture );
	glTexImage2D( GL_TEXTURE_2D, 0, internalFormat, width, height, 0, GL_RGBA, type, pixels );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
	glBindTexture( GL_TEXTURE_2D, 0 );
	return texture;
}

template< typename T >
std::vector< T > flipRows( const std::vector< T >& image, int width, int height )
{
	std::vector< T > flipped( image.size() );
	const size_t stride = static_cast< size_t >( width ) * 4;
	for( int y = 0; y < height; ++y )
		std::copy( image.begin() + static_cast< long >( ( height - 1 - y ) * stride ),
		           image.begin() + static_cast< long >( ( height - y ) * stride ), flipped.begin() + static_cast< long >( y * stride ) );
	return flipped;
}

//---------------------------------------------------------------------------
// Parameters by display name, so the automation reads as English. An option
// may be named by its element ("Format=Super 8") or its index; anything that
// is neither, or not wholly a number, is refused (polyhedral's trap: strtof
// of a word is a silent 0).
//---------------------------------------------------------------------------
struct NamedParameter
{
	std::string name;
	unsigned int index;
	unsigned int type;
	float value;
	float low;
	float high;
	std::vector< std::string > elements;
};

const char* kindName( GaugePlugin& plugin, const NamedParameter& p )
{
	if( p.index >= plugin.AboutFirst() )
		return "about";
	switch( p.type )
	{
	case FF_TYPE_BOOLEAN: return "bool";
	case FF_TYPE_EVENT: return "event";
	case FF_TYPE_OPTION: return "option";
	case FF_TYPE_INTEGER: return "integer";
	case FF_TYPE_BUFFER: return "buffer";
	case FF_TYPE_TEXT: return "text";
	case FF_TYPE_STANDARD: return "standard";
	default: return "other";
	}
}

std::vector< NamedParameter > listParameters( GaugePlugin& plugin )
{
	std::vector< NamedParameter > list;
	for( unsigned int i = 0; i < plugin.ParamCount(); ++i )
	{
		const char* name = plugin.GetParamName( i );
		NamedParameter p;
		p.name  = name ? std::string( name, strnlen( name, 16 ) ) : "?";
		p.index = i;
		p.type  = plugin.GetParamType( i );
		p.value = plugin.GetFloatParameter( i );
		p.low   = 0.0f;
		p.high  = 1.0f;
		if( p.type == FF_TYPE_OPTION )
		{
			const unsigned int n = plugin.GetNumParamElements( i );
			p.high               = static_cast< float >( std::max( 1u, n ) - 1u );
			for( unsigned int e = 0; e < n; ++e )
			{
				const char* en = plugin.GetParamElementName( i, e );
				p.elements.push_back( en ? en : "" );
			}
		}
		list.push_back( p );
	}
	return list;
}

bool applySetting( GaugePlugin& plugin, const std::string& assignment, std::string& error )
{
	const size_t equals = assignment.find( '=' );
	if( equals == std::string::npos )
	{
		error = "expected Name=Value";
		return false;
	}
	const std::string name = assignment.substr( 0, equals );
	const std::string text = assignment.substr( equals + 1 );
	for( const NamedParameter& p : listParameters( plugin ) )
	{
		if( p.name != name )
			continue;
		for( size_t e = 0; e < p.elements.size(); ++e )
			if( p.elements[ e ] == text )
			{
				plugin.SetFloatParameter( p.index, static_cast< float >( e ) );
				return true;
			}
		char* end     = nullptr;
		const float v = std::strtof( text.c_str(), &end );
		if( text.empty() || end == nullptr || *end != '\0' )
		{
			error = "'" + text + "' is not a number" + ( p.elements.empty() ? "" : " or an option of " + name );
			return false;
		}
		plugin.SetFloatParameter( p.index, v );
		return true;
	}
	error = "no parameter called '" + name + "'";
	return false;
}

bool set( GaugePlugin& plugin, const std::string& name, float value )
{
	std::string error;
	char buffer[ 64 ];
	std::snprintf( buffer, sizeof( buffer ), "%.9g", value );
	if( applySetting( plugin, name + "=" + buffer, error ) )
		return true;
	std::fprintf( stderr, "%s\n", error.c_str() );
	return false;
}

bool setOption( GaugePlugin& plugin, const std::string& name, const std::string& element )
{
	std::string error;
	if( applySetting( plugin, name + "=" + element, error ) )
		return true;
	std::fprintf( stderr, "%s\n", error.c_str() );
	return false;
}

//---------------------------------------------------------------------------
// A session: one plugin, its input and output, and the clock that drives it.
//---------------------------------------------------------------------------
struct Session
{
	std::unique_ptr< GaugePlugin > plugin;
	int width        = 0;
	int height       = 0;
	double fps       = 60.0;
	double bpm       = 120.0;
	bool transport   = true;///< hand the plugin a bar phase every frame, as a host would
	bool floatOutput = true;

	GLuint sourceTexture = 0;
	GLuint outputTexture = 0;
	GLuint outputFBO     = 0;
	FFGLTextureStruct inputStruct  = {};
	FFGLTextureStruct* inputs[ 1 ] = { nullptr };
	ProcessOpenGLStruct process    = {};

	explicit Session( bool effect = true ) :
		plugin( new GaugePlugin( effect ) )
	{
	}

	void makeTargets()
	{
		sourceTexture = makeTexture( width, height, GL_RGBA32F, GL_FLOAT, nullptr );
		outputTexture = floatOutput ? makeTexture( width, height, GL_RGBA32F, GL_FLOAT, nullptr )
		                            : makeTexture( width, height, GL_RGBA8, GL_UNSIGNED_BYTE, nullptr );
		glGenFramebuffers( 1, &outputFBO );
		glBindFramebuffer( GL_FRAMEBUFFER, outputFBO );
		glFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, outputTexture, 0 );

		inputStruct.Width = inputStruct.HardwareWidth = static_cast< FFUInt32 >( width );
		inputStruct.Height = inputStruct.HardwareHeight = static_cast< FFUInt32 >( height );
		inputStruct.Handle                              = sourceTexture;
		inputs[ 0 ]                                     = &inputStruct;
		process.numInputTextures                        = plugin->IsEffect() ? 1 : 0;
		process.inputTextures                           = plugin->IsEffect() ? inputs : nullptr;
		process.HostFBO                                 = outputFBO;
	}

	void dropTargets()
	{
		if( outputFBO )
			glDeleteFramebuffers( 1, &outputFBO );
		if( outputTexture )
			glDeleteTextures( 1, &outputTexture );
		if( sourceTexture )
			glDeleteTextures( 1, &sourceTexture );
		outputFBO = outputTexture = sourceTexture = 0;
	}

	bool begin( int w, int h )
	{
		width  = w;
		height = h;
		FFGLViewportStruct viewport = {};
		viewport.width              = static_cast< FFUInt32 >( width );
		viewport.height             = static_cast< FFUInt32 >( height );
		if( plugin->InitGL( &viewport ) != FF_SUCCESS )
		{
			std::fprintf( stderr, "InitGL failed -- see the diagnostics log for which shader\n" );
			return false;
		}
		makeTargets();
		return true;
	}

	/// What a host does when the composition changes size: the SAME instance,
	/// a differently sized input. No DeInitGL.
	void resize( int w, int h )
	{
		dropTargets();
		width  = w;
		height = h;
		makeTargets();
	}

	/// Seconds at frame n of the synthetic clock.
	double timeAt( int frame ) const
	{
		return static_cast< double >( frame ) / fps;
	}

	bool renderAt( int frame )
	{
		//A synthetic clock, declared: left to the wall clock a hundred frames
		//take a few milliseconds and the film never moves.
		plugin->SetClockScaleForTest( 1.0 );
		plugin->SetTime( timeAt( frame ) );
		if( transport )
		{
			const double bars = timeAt( frame ) * bpm / 60.0 / 4.0;
			plugin->SetTransportForTest( bpm, bars - std::floor( bars ) );
		}
		glBindFramebuffer( GL_FRAMEBUFFER, outputFBO );
		glViewport( 0, 0, width, height );
		glClearColor( 0.0f, 0.0f, 0.0f, 0.0f );
		glClear( GL_COLOR_BUFFER_BIT );
		const bool ok = plugin->ProcessOpenGL( &process ) == FF_SUCCESS;
		if( !ok )
			std::fprintf( stderr, "ProcessOpenGL failed on frame %d\n", frame );
		return ok;
	}

	void upload( const Image& pixels )
	{
		const Image flipped = flipRows( pixels, width, height );
		glBindTexture( GL_TEXTURE_2D, sourceTexture );
		glTexSubImage2D( GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGBA, GL_FLOAT, flipped.data() );
		glBindTexture( GL_TEXTURE_2D, 0 );
	}

	/// Render one frame. The source has no input: an empty image uploads nothing.
	bool render( int frame, const Image& pixels )
	{
		if( !pixels.empty() )
			upload( pixels );
		return renderAt( frame );
	}

	std::vector< unsigned char > readBack()
	{
		std::vector< unsigned char > pixels( static_cast< size_t >( width ) * height * 4 );
		glBindFramebuffer( GL_FRAMEBUFFER, outputFBO );
		glPixelStorei( GL_PACK_ALIGNMENT, 1 );
		glReadPixels( 0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data() );
		return flipRows( pixels, width, height );
	}

	Image readBackFloat()
	{
		Image pixels( static_cast< size_t >( width ) * height * 4 );
		glBindFramebuffer( GL_FRAMEBUFFER, outputFBO );
		glPixelStorei( GL_PACK_ALIGNMENT, 1 );
		glReadPixels( 0, 0, width, height, GL_RGBA, GL_FLOAT, pixels.data() );
		return flipRows( pixels, width, height );
	}

	void end()
	{
		plugin->DeInitGL();
		dropTargets();
	}
};

const float* pixelAt( const Image& image, int width, int x, int y )
{
	return image.data() + ( static_cast< size_t >( y ) * width + x ) * 4;
}

const char* verdict( bool ok )
{
	return ok ? "ok" : "FAILED";
}

using Settings = std::vector< std::pair< std::string, std::string > >;

bool applyAll( GaugePlugin& plugin, const Settings& settings )
{
	for( const auto& s : settings )
	{
		std::string error;
		if( !applySetting( plugin, s.first + "=" + s.second, error ) )
		{
			std::fprintf( stderr, "%s=%s: %s\n", s.first.c_str(), s.second.c_str(), error.c_str() );
			return false;
		}
	}
	return true;
}

//---------------------------------------------------------------------------
// --pipe cue sheet: one 'frame Name value' per line, the fleet's format.
//---------------------------------------------------------------------------
using Track = std::vector< std::pair< int, float > >;

std::map< std::string, Track > loadScript( const std::string& path, std::string& error )
{
	std::map< std::string, Track > tracks;
	std::ifstream file( path );
	if( !file )
	{
		error = "cannot open " + path;
		return tracks;
	}
	std::string line;
	int lineNumber = 0;
	while( std::getline( file, line ) )
	{
		++lineNumber;
		const size_t hash = line.find( '#' );
		if( hash != std::string::npos )
			line.erase( hash );
		std::istringstream in( line );
		int frame = 0;
		if( !( in >> frame ) )
			continue;
		std::vector< std::string > words;
		std::string word;
		while( in >> word )
			words.push_back( word );
		if( words.size() < 2 )
		{
			error = path + ":" + std::to_string( lineNumber ) + ": expected `frame Parameter Name value`";
			return {};
		}
		char* end         = nullptr;
		const float value = std::strtof( words.back().c_str(), &end );
		if( end == nullptr || *end != '\0' )
		{
			error = path + ":" + std::to_string( lineNumber ) + ": '" + words.back() + "' is not a number";
			return {};
		}
		words.pop_back();
		std::string name = words.front();
		for( size_t i = 1; i < words.size(); ++i )
			name += " " + words[ i ];
		tracks[ name ].emplace_back( frame, value );
	}
	for( auto& entry : tracks )
		std::sort( entry.second.begin(), entry.second.end() );
	return tracks;
}

float valueAt( const Track& track, int frame )
{
	if( track.empty() )
		return 0.0f;
	if( frame <= track.front().first )
		return track.front().second;
	if( frame >= track.back().first )
		return track.back().second;
	for( size_t i = 1; i < track.size(); ++i )
		if( frame <= track[ i ].first )
		{
			const auto& a    = track[ i - 1 ];
			const auto& b    = track[ i ];
			const float span = static_cast< float >( b.first - a.first );
			const float t    = span > 0.0f ? static_cast< float >( frame - a.first ) / span : 1.0f;
			return a.second + ( b.second - a.second ) * t;
		}
	return track.back().second;
}

#include "checks.inc"

//---------------------------------------------------------------------------
int dumpShaders( const std::string& dir )
{
	const std::pair< const char*, std::string > files[] = {
		{ "vertex.vert", shaders::Vertex() },
		{ "capture.frag", shaders::Capture() },
		{ "composite.frag", shaders::Composite() },
		{ "copy.frag", shaders::Copy() },
	};
	for( const auto& f : files )
	{
		std::ofstream out( dir + "/" + f.first );
		if( !out )
		{
			std::fprintf( stderr, "cannot write %s/%s\n", dir.c_str(), f.first );
			return 1;
		}
		out << f.second;
	}
	std::printf( "wrote %zu shaders to %s\n", sizeof( files ) / sizeof( files[ 0 ] ), dir.c_str() );
	return 0;
}

void usage()
{
	std::printf(
		"gutest -- render and measure Gauge, the film strip\n"
		"\n"
		"  --plugin effect|source  which plugin (default effect: SW Gauge; source: SW Gauge Leader)\n"
		"  --out PATH          render through the plugin to a PNG (default /tmp/gauge.png)\n"
		"  --size WxH          raster (default 1280x720)\n"
		"  --frames N          frames to render before reading back (default 40)\n"
		"  --fps N             synthetic frame rate driving the clock (default 60)\n"
		"  --bpm N             the transport's tempo (default 120)\n"
		"  --source S          card (default), flat, disc or white\n"
		"  --level V           the flat source's level, 0..1 (default 0.5)\n"
		"  --set \"Name=V\"      set a parameter by its display name (an option by its element). Repeatable.\n"
		"  --press \"Name@F\"    press an event parameter at frame F. Repeatable.\n"
		"  --tone K=M          feed FFT bin K with magnitude M (repeatable)\n"
		"  --list              print every parameter, its kind, default and range, then exit\n"
		"  --perfs --aperture --squeeze --held --leader --flash --track --alpha --resize\n"
		"                      the checks; --negative runs every one against a perturbed model\n"
		"  --perturb BITS      run the checks against a perturbed model (Gauge.h), verbosely\n"
		"  --software          use Apple's software renderer\n"
		"  --bench             time ProcessOpenGL at 720p through 4K\n"
		"  --dump-shaders DIR  write the exact GLSL the plugin compiles\n"
		"  --pipe              raw RGBA frames on stdin, raw RGBA frames on stdout\n"
		"  --script PATH       parameter cues for --pipe: 'frame Name Value'\n"
		"  --help\n" );
}
} // namespace

int main( int argc, char** argv )
{
	std::string outPath = "/tmp/gauge.png";
	std::string scriptPath;
	std::string sourceName = "card";
	std::string dumpDir;
	std::string pluginName = "effect";
	int width       = 1280;
	int height      = 720;
	int frames      = 40;
	float level     = 0.5f;
	int perturb     = 0;
	double fps      = 60.0;
	double bpm      = 120.0;
	bool wantList   = false;
	bool wantBench  = false;
	bool wantPipe   = false;
	bool software   = false;
	std::vector< std::string > settings;
	std::vector< std::pair< std::string, int > > presses;
	std::vector< float > tone( GaugePlugin::kAudioBins, 0.0f );
	bool toneSet = false;
	std::vector< std::string > checks;

	for( int i = 1; i < argc; ++i )
	{
		const std::string a = argv[ i ];
		const bool next     = i + 1 < argc;
		if( a == "--help" )
		{
			usage();
			return 0;
		}
		else if( a == "--out" && next )
			outPath = argv[ ++i ];
		else if( a == "--plugin" && next )
			pluginName = argv[ ++i ];
		else if( a == "--script" && next )
			scriptPath = argv[ ++i ];
		else if( a == "--source" && next )
			sourceName = argv[ ++i ];
		else if( a == "--level" && next )
			level = std::strtof( argv[ ++i ], nullptr );
		else if( a == "--perturb" && next )
			perturb = std::atoi( argv[ ++i ] );
		else if( a == "--dump-shaders" && next )
			dumpDir = argv[ ++i ];
		else if( a == "--size" && next )
		{
			const std::string s = argv[ ++i ];
			const size_t x      = s.find( 'x' );
			if( x == std::string::npos )
			{
				std::fprintf( stderr, "--size wants WxH\n" );
				return 2;
			}
			width  = std::atoi( s.substr( 0, x ).c_str() );
			height = std::atoi( s.substr( x + 1 ).c_str() );
		}
		else if( a == "--frames" && next )
			frames = std::atoi( argv[ ++i ] );
		else if( a == "--fps" && next )
			fps = std::strtod( argv[ ++i ], nullptr );
		else if( a == "--bpm" && next )
			bpm = std::strtod( argv[ ++i ], nullptr );
		else if( a == "--set" && next )
			settings.push_back( argv[ ++i ] );
		else if( a == "--press" && next )
		{
			const std::string s = argv[ ++i ];
			const size_t at     = s.rfind( '@' );
			if( at == std::string::npos )
			{
				std::fprintf( stderr, "--press wants Name@frame\n" );
				return 2;
			}
			presses.emplace_back( s.substr( 0, at ), std::atoi( s.substr( at + 1 ).c_str() ) );
		}
		else if( a == "--tone" && next )
		{
			const std::string s = argv[ ++i ];
			const size_t eq     = s.find( '=' );
			const int k         = std::atoi( s.substr( 0, eq ).c_str() );
			if( eq == std::string::npos || k < 0 || k >= GaugePlugin::kAudioBins )
			{
				std::fprintf( stderr, "--tone wants K=M with 0 <= K < 64\n" );
				return 2;
			}
			tone[ static_cast< size_t >( k ) ] = std::strtof( s.substr( eq + 1 ).c_str(), nullptr );
			toneSet                            = true;
		}
		else if( a == "--list" )
			wantList = true;
		else if( a == "--bench" )
			wantBench = true;
		else if( a == "--pipe" )
			wantPipe = true;
		else if( a == "--software" )
			software = true;
		else if( isCheck( a ) )
			checks.push_back( a );
		else
		{
			std::fprintf( stderr, "unknown argument: %s\n", a.c_str() );
			usage();
			return 2;
		}
	}
	if( width <= 0 || height <= 0 || frames <= 0 || fps <= 0.0 || bpm <= 0.0 )
	{
		std::fprintf( stderr, "width, height, frames, fps and bpm must all be positive\n" );
		return 2;
	}
	if( pluginName != "effect" && pluginName != "source" )
	{
		std::fprintf( stderr, "--plugin is effect or source\n" );
		return 2;
	}
	const bool effect = pluginName == "effect";

	if( !dumpDir.empty() )
		return dumpShaders( dumpDir );

	if( wantList )
	{
		//No GL needed, so it works where a context cannot be made, and in CI.
		GaugePlugin plugin( effect );
		std::printf( "%3s  %-16s  %-9s  %-8s  %s\n", "id", "name", "kind", "default", "range" );
		for( const NamedParameter& p : listParameters( plugin ) )
		{
			std::printf( "%3u  %-16s  %-9s  %.4f    [%g..%g]", p.index, p.name.c_str(), kindName( plugin, p ), p.value, p.low, p.high );
			if( !p.elements.empty() )
			{
				std::printf( "  " );
				for( size_t e = 0; e < p.elements.size(); ++e )
					std::printf( "%s%s", e ? " | " : "", p.elements[ e ].c_str() );
			}
			std::printf( "\n" );
		}
		return 0;
	}

	CGLContextObj context = createContext( software );
	if( context == nullptr )
	{
		std::fprintf( stderr, "could not create an OpenGL context\n" );
		return 1;
	}
	auto finish = [ & ]( int result ) {
		CGLSetCurrentContext( nullptr );
		CGLDestroyContext( context );
		return result;
	};

	if( !checks.empty() )
	{
		int failures = 0;
		for( const std::string& check : checks )
		{
			failures += runCheck( check, width, height, perturb );
			std::printf( "\n" );
		}
		return finish( failures == 0 ? 0 : 1 );
	}

	Session session( effect );
	session.fps         = fps;
	session.bpm         = bpm;
	session.floatOutput = false;
	for( const std::string& s : settings )
	{
		std::string error;
		if( !applySetting( *session.plugin, s, error ) )
		{
			std::fprintf( stderr, "--set %s: %s\n", s.c_str(), error.c_str() );
			return finish( 2 );
		}
	}
	for( const auto& p : presses )
	{
		bool found = false;
		for( const NamedParameter& np : listParameters( *session.plugin ) )
			found = found || np.name == p.first;
		if( !found )
		{
			std::fprintf( stderr, "--press: no parameter called '%s'\n", p.first.c_str() );
			return finish( 2 );
		}
	}
	if( toneSet )
		session.plugin->SetAudioForTest( tone.data(), static_cast< int >( tone.size() ) );
	session.plugin->SetPerturbForTest( perturb );

	if( wantBench )
		return finish( runBench( settings, effect, frames, fps ) );

	if( !session.begin( width, height ) )
		return finish( 1 );

	auto pressAt = [ & ]( int frame ) {
		for( const auto& p : presses )
		{
			if( p.second == frame )
				set( *session.plugin, p.first, 1.0f );
			else if( p.second + 1 == frame )
				set( *session.plugin, p.first, 0.0f );
		}
	};

	if( wantPipe )
	{
		std::signal( SIGPIPE, SIG_IGN );
		std::map< unsigned int, Track > automation;
		if( !scriptPath.empty() )
		{
			std::string error;
			const std::map< std::string, Track > tracks = loadScript( scriptPath, error );
			if( !error.empty() )
			{
				std::fprintf( stderr, "%s\n", error.c_str() );
				return finish( 2 );
			}
			for( const auto& entry : tracks )
			{
				int index = -1;
				for( const NamedParameter& p : listParameters( *session.plugin ) )
					if( p.name == entry.first )
						index = static_cast< int >( p.index );
				if( index < 0 )
				{
					std::fprintf( stderr, "script names '%s', which is not a parameter (try --list)\n", entry.first.c_str() );
					return finish( 2 );
				}
				automation[ static_cast< unsigned int >( index ) ] = entry.second;
			}
		}
		std::vector< unsigned char > frame( static_cast< size_t >( width ) * height * 4 );
		Image pixels( frame.size() );
		int status = 0;
		for( int index = 0;; ++index )
		{
			size_t got = 0;
			if( effect )
			{
				while( got < frame.size() )
				{
					const ssize_t n = read( STDIN_FILENO, frame.data() + got, frame.size() - got );
					if( n <= 0 )
						break;
					got += static_cast< size_t >( n );
				}
				if( got < frame.size() )
					break;
				for( size_t i = 0; i < frame.size(); ++i )
					pixels[ i ] = frame[ i ] / 255.0f;
			}
			else if( index >= frames )
				break;
			for( const auto& track : automation )
				session.plugin->SetFloatParameter( track.first, valueAt( track.second, index ) );
			pressAt( index );
			if( !session.render( index, pixels ) )
			{
				status = 1;
				break;
			}
			const std::vector< unsigned char > out = session.readBack();
			size_t written                         = 0;
			while( written < out.size() )
			{
				const ssize_t put = write( STDOUT_FILENO, out.data() + written, out.size() - written );
				if( put <= 0 )
					break;
				written += static_cast< size_t >( put );
			}
			if( written < out.size() )
			{
				std::fprintf( stderr, "stdout closed at frame %d\n", index );
				status = 1;
				break;
			}
		}
		session.end();
		return finish( status );
	}

	for( int frame = 0; frame < frames; ++frame )
	{
		Image pixels;
		if( sourceName == "flat" )
			pixels = buildFlat( width, height, level );
		else if( sourceName == "white" )
			pixels = buildFlat( width, height, 1.0f );
		else if( sourceName == "disc" )
			pixels = buildDisc( width, height, 0.3 );
		else
			pixels = buildCard( width, height, frame );
		pressAt( frame );
		if( !session.render( frame, pixels ) )
			return finish( 1 );
	}
	const std::vector< unsigned char > image = session.readBack();
	session.end();
	if( !writePng( outPath, width, height, image ) )
	{
		std::fprintf( stderr, "could not write %s\n", outPath.c_str() );
		return finish( 1 );
	}
	std::printf( "wrote %s (%dx%d, %d frames)\n", outPath.c_str(), width, height, frames );
	return finish( 0 );
}
