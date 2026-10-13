#include "Controls.h"

#include <algorithm>
#include <cmath>

namespace gauge::controls
{
namespace
{
float clamp01( float v )
{
	return std::clamp( v, 0.0f, 1.0f );
}

template< size_t N >
const char* pick( const char* const ( &names )[ N ], int index )
{
	return names[ std::clamp( index, 0, static_cast< int >( N ) - 1 ) ];
}

/// A signed cube about the middle of the slider: v = scale ( 2p - 1 )^3.
double signedCube( float value, double scale )
{
	const double c = 2.0 * static_cast< double >( clamp01( value ) ) - 1.0;
	return scale * c * c * c;
}

float signedCubeParam( double v, double scale )
{
	const double c = std::cbrt( std::clamp( v / scale, -1.0, 1.0 ) );
	return clamp01( static_cast< float >( 0.5 * ( c + 1.0 ) ) );
}
} // namespace

const char* StockName( int index )
{
	static const char* const names[] = { "Colour Print", "B&W Print", "Clear Base" };
	return pick( names, index );
}

const char* SqueezeName( int index )
{
	static const char* const names[] = { "Format", "None", "1.33x", "1.5x", "2x" };
	return pick( names, index );
}

double SqueezeFactor( int index, double formatSqueeze )
{
	switch( index )
	{
	case kSqueezeNone: return 1.0;
	case kSqueeze133: return 4.0 / 3.0;
	case kSqueeze150: return 1.5;
	case kSqueeze200: return 2.0;
	default: return formatSqueeze;
	}
}

const char* FitName( int index )
{
	static const char* const names[] = { "Crop", "Letterbox", "Stretch" };
	return pick( names, index );
}

const char* ModeName( int index )
{
	static const char* const names[] = { "Cell", "Strip" };
	return pick( names, index );
}

const char* RateName( int index )
{
	static const char* const names[] = { "24 fps", "25 fps", "18 fps", "16 fps", "Beat" };
	return pick( names, index );
}

double RateFps( int index )
{
	static const double rates[] = { 24.0, 25.0, 18.0, 16.0, 0.0 };
	return rates[ std::clamp( index, 0, kRateCount - 1 ) ];
}

const char* FramesName( int index )
{
	static const char* const names[] = { "Held", "Live" };
	return pick( names, index );
}

const char* StartOnName( int index )
{
	static const char* const names[] = { "Press", "Beat", "Bar" };
	return pick( names, index );
}

const char* PopFrameName( int index )
{
	static const char* const names[] = { "Number", "Flash" };
	return pick( names, index );
}

const char* AutoFlashName( int index )
{
	static const char* const names[] = { "Off", "Beat", "Bar" };
	return pick( names, index );
}

const char* HolesName( int index )
{
	static const char* const names[] = { "Light", "Clear", "Black" };
	return pick( names, index );
}

const char* SurroundName( int index )
{
	static const char* const names[] = { "Clear", "Black", "Light", "Clip" };
	return pick( names, index );
}

const char* AfterName( int index )
{
	static const char* const names[] = { "Black", "Clear" };
	return pick( names, index );
}

int OptionIndex( float value, int count )
{
	return std::clamp( static_cast< int >( std::lround( value ) ), 0, count - 1 );
}

float Framing( float value )
{
	return clamp01( value );
}

double RackFrames( float value )
{
	return static_cast< double >( clamp01( value ) ) - 0.5;
}

float RackParam( double frames )
{
	return clamp01( static_cast< float >( frames + 0.5 ) );
}

double DriftFps( float value )
{
	return signedCube( value, 4.0 );
}

float DriftParam( double fps )
{
	return signedCubeParam( fps, 4.0 );
}

double SizeFraction( float value )
{
	return 0.05 * std::pow( 20.0, static_cast< double >( clamp01( value ) ) );
}

float SizeParam( double fraction )
{
	return clamp01( static_cast< float >( std::log( std::max( fraction, 0.05 ) / 0.05 ) / std::log( 20.0 ) ) );
}

double AngleDegrees( float value )
{
	return 360.0 * static_cast< double >( clamp01( value ) ) - 180.0;
}

float AngleParam( double degrees )
{
	return clamp01( static_cast< float >( ( degrees + 180.0 ) / 360.0 ) );
}

double PositionFraction( float value )
{
	return 2.0 * static_cast< double >( clamp01( value ) ) - 1.0;
}

float PositionParam( double fraction )
{
	return clamp01( static_cast< float >( 0.5 * ( fraction + 1.0 ) ) );
}

double TravelFps( float value )
{
	return signedCube( value, 24.0 );
}

float TravelParam( double fps )
{
	return signedCubeParam( fps, 24.0 );
}

double ShutterDegrees( float value )
{
	return 360.0 * static_cast< double >( clamp01( value ) );
}

float ShutterParam( double degrees )
{
	return clamp01( static_cast< float >( degrees / 360.0 ) );
}

float TrackLevel( float value )
{
	return 2.0f * clamp01( value );
}

float TrackLevelParam( float level )
{
	return clamp01( 0.5f * level );
}

} // namespace gauge::controls
