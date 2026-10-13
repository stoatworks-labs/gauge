#include "Shaders.h"

namespace gauge::shaders
{
namespace
{
const char* const kVersion = "#version 410 core\n";

//---------------------------------------------------------------------------
// The vertex shader every pass shares.
//---------------------------------------------------------------------------
const char* const kVertexBody = R"(
layout( location = 0 ) in vec4 vPosition;
layout( location = 1 ) in vec2 vUV;

out vec2 uv;

void main()
{
	gl_Position = vPosition;
	uv = vUV;
}
)";

//---------------------------------------------------------------------------
// Capture: the clip into one held slot, as the aperture exposes it.
//
// The slot is the picture area upright: u across the picture left to right,
// v down it. Window is the part of the clip the picture shows (Fit Crop,
// Letterbox or Stretch, after the squeeze), top-down in 0..1 of the clip; a
// window that runs outside the clip is letterboxed, and letterbox bars print
// black. The clip is premultiplied (Resolume's DXV clips with alpha are), so
// its rgb IS the picture over black: a transparent clip prints as black film.
//
// A box filter over each slot texel's footprint, up to 8 x 8 taps: a strip
// cell can be a tenth of the clip's size, and it is captured once and kept,
// so aliasing here would ride along the strip for good.
//---------------------------------------------------------------------------
const char* const kCaptureBody = R"(
uniform sampler2D InputTexture;
uniform vec2 MaxUV;
uniform vec4 Window;    //x, y, w, h of the clip, top-down
uniform vec2 InputSize; //the clip's raster
uniform ivec2 CellSize; //the slot's raster

in vec2 uv;
out vec4 fragColor;

void main()
{
	vec2 img = vec2( uv.x, 1.0 - uv.y );
	vec2 src = Window.xy + img * Window.zw;
	if( src.x < 0.0 || src.y < 0.0 || src.x > 1.0 || src.y > 1.0 )
	{
		fragColor = vec4( 0.0, 0.0, 0.0, 1.0 );
		return;
	}

	vec2 texels = Window.zw * InputSize / vec2( CellSize );
	ivec2 n     = clamp( ivec2( ceil( texels ) ), ivec2( 1 ), ivec2( 8 ) );
	vec2 stride = Window.zw / vec2( CellSize ) / vec2( n );
	vec2 corner = src - 0.5 * Window.zw / vec2( CellSize );
	vec2 halfTexel = 0.5 / InputSize;
	vec3 sum = vec3( 0.0 );
	for( int j = 0; j < n.y; ++j )
		for( int i = 0; i < n.x; ++i )
		{
			vec2 s = clamp( corner + ( vec2( i, j ) + 0.5 ) * stride, halfTexel, vec2( 1.0 ) - halfTexel );
			sum += texture( InputTexture, vec2( s.x, 1.0 - s.y ) * MaxUV ).rgb;
		}
	fragColor = vec4( sum / float( n.x * n.y ), 1.0 );
}
)";

//---------------------------------------------------------------------------
// Copy: one layer of the old held array, across the viewport.
//---------------------------------------------------------------------------
const char* const kCopyBody = R"(
uniform sampler2DArray Old;
uniform int Layer;

in vec2 uv;
out vec4 fragColor;

void main()
{
	fragColor = texture( Old, vec3( uv, float( Layer ) ) );
}
)";

//---------------------------------------------------------------------------
// Composite, part 1: what the pass is told, and the small tools.
//
// Film coordinates are millimetres: x across from the reference edge, y along
// toward the tail from the top of frame `base` (the CPU's), so they stay small.
// Pixel ( 0, 0 ) is the output's top-left corner whatever GL's origin is.
//
// Coverage is a box filter over the pixel's footprint, written from the
// DISTANCES to each edge (fiche's trap: min( p + fp, hi ) - max( p - fp, lo )
// on absolute coordinates cancels in float32 when zoomed in). For a straight
// edge the ramp's integral is exact, which is what lets the harness measure a
// hole's width by summing coverage.
//---------------------------------------------------------------------------
const char* const kCompositeHead = R"(
uniform ivec2 Size;
uniform vec2 Origin;        //film ( x, y ) at pixel coordinate ( 0, 0 )
uniform vec2 AxisX;         //film mm per pixel step in x: ( across, along )
uniform vec2 AxisY;
uniform float MmPerPixel;
uniform int Taps;           //motion blur: positions the shutter averages
uniform float TapStep;      //mm the film moves between them

uniform float FilmWidth;
uniform float Pitch;
uniform vec3 HoleSize;      //across, along, corner radius
uniform vec3 HoleGrid;      //pitch, centre of a hole within a frame, edge to hole
uniform ivec2 HoleRows;     //near row, far row
uniform vec4 Image;         //x0, x1 across; y0, y1 along within a frame
uniform int ImageAcross;    //the still format's picture runs along the film
uniform float DisplayAspect;//the picture's aspect as it is meant to be seen

uniform int Soundtracks;
uniform vec2 Analogue;
uniform int Channels;
uniform vec2 Dts;
uniform int Dolby;
uniform vec2 Sdds;

uniform int EdgePrint;
uniform vec2 PrintFar;
uniform vec2 PrintNear;
uniform int FramesPerLabel;
uniform int GlyphUp;        //+1: a glyph's top toward the far edge
uniform int GlyphAdvance;   //+1: text runs toward the tail
uniform ivec4 Code0;        //the stock's edge code, ASCII, 0 ends it
uniform ivec4 Code1;
uniform int LabelBase;      //the label number at or before frame base
uniform int LabelRem;       //base mod FramesPerLabel
uniform int HashBase;       //base, for the data tracks' bits
uniform int StillBase;      //base mod 37, the still roll's frame numbers

uniform vec3 BaseColour;    //the film where nothing is printed
uniform vec3 InkColour;     //the edge print
uniform vec3 TrackDense;
uniform vec3 TrackClear;
uniform vec3 BlackColour;   //a black frame
uniform int Mono;           //black-and-white stock

uniform vec4 HoleColour;    //premultiplied
uniform int SurroundKind;   //0 clear, 1 black, 2 light, 3 the clip

uniform int FrameCount;
uniform isampler2D ContentTable;//per frame from base: kind, value, spectrum row

uniform sampler2D InputTexture;
uniform vec2 MaxUV;
uniform vec4 Window;
uniform vec2 InputSize;
uniform sampler2DArray Held;

uniform sampler2D Spectrum; //rows of 64 magnitudes then 64 phases
uniform int LiveRow;
uniform int StepRow;        //each bin's phase step per film frame
uniform float FilmRate;
uniform float TrackLevel;
uniform float BinHz;

uniform sampler2D Font;     //5x7, glyph c at column 5c
uniform sampler2D Glyphs;   //the leader's distance-field atlas
uniform sampler2D Metrics;  //44 x 2: originX, originY, advance, inkX0 / inkX1
uniform int CountFrames;    //frames to a leader count
uniform int PopFlash;
uniform float DigitHeight;  //the numeral's ink height, in the picture's height
uniform float DigitBold;    //and its extra weight, in its own height

uniform int IsEffect;
uniform float MixAmount;
uniform int Perturb;

in vec2 uv;
out vec4 fragColor;

const float PI = 3.14159265358979;

uint pcg( uint v )
{
	uint state = v * 747796405u + 2891336453u;
	uint word  = ( ( state >> ( ( state >> 28u ) + 4u ) ) ^ state ) * 277803737u;
	return ( word >> 22u ) ^ word;
}

//The fraction of the pixel's footprint past an edge d mm behind it.
float ramp( float d )
{
	return clamp( d / MmPerPixel + 0.5, 0.0, 1.0 );
}

//The fraction of the footprint centred at c inside [ lo, hi ].
float span( float c, float lo, float hi )
{
	return ramp( c - lo ) - ramp( c - hi );
}

//A rounded rectangle centred at the origin, from its signed distance.
float roundedRect( vec2 q, vec2 size, float r )
{
	vec2 d    = abs( q ) - 0.5 * size + vec2( r );
	float sdf = length( max( d, vec2( 0.0 ) ) ) + min( max( d.x, d.y ), 0.0 ) - r;
	return clamp( 0.5 - sdf / MmPerPixel, 0.0, 1.0 );
}

ivec4 contentOf( int d )
{
	if( d < 0 || d >= FrameCount )
		return ivec4( 4, 0, -1, 0 );
	return texelFetch( ContentTable, ivec2( d, 0 ), 0 );
}
)";

//---------------------------------------------------------------------------
// Composite, part 2: the countdown leader, after the SMPTE Universal Leader.
//
// The geometry is designed in the picture's DISPLAY units (height 1, width the
// display aspect), so its circles are round as projected -- and squeezed on a
// scope print, which is where it is drawn. Leader frame j: 0 PICTURE START;
// 1..143 the counts 8 to 3, CountFrames each, the sweep a fraction
// ( j mod CountFrames ) / CountFrames round; 144 the 2 for one frame; 145 on
// black. The greys are mine; the standard does not set them.
//---------------------------------------------------------------------------
const char* const kCompositeLeader = R"(
const float LIGHT = 0.62;
const float DARK  = 0.30;
const int TWO     = 144;
const int PICTURE[ 7 ] = int[ 7 ]( 25, 18, 12, 29, 30, 27, 14 );//P I C T U R E in the atlas
const int START[ 5 ]   = int[ 5 ]( 28, 29, 10, 27, 29 );        //S T A R T
const int GLYPH_C = 12;
const int GLYPH_F = 15;

//Signed distance in H units, positive inside, of glyph g at local ( x, y up ).
float glyphDistance( int g, vec2 local )
{
	vec4 m  = texelFetch( Metrics, ivec2( g, 0 ), 0 );
	vec2 a  = m.xy + local * 88.0;
	vec2 lo = m.xy - vec2( 20.0 );
	if( a.x < lo.x + 0.5 || a.y < lo.y + 0.5 || a.x > lo.x + 127.5 || a.y > lo.y + 127.5 )
		return -1.0;
	float b = textureLod( Glyphs, a / vec2( 1024.0, 768.0 ), 0.0 ).r * 255.0;
	return ( b - 128.0 ) / ( 128.0 / 12.0 ) / 88.0;
}

//Glyph g with its pen at x0 and its ink between baseline y1 and cap y1 - H,
//display units with y down. Bold thickens the stroke, in H units.
float glyphCover( vec2 p, int g, float x0, float y1, float H, float bold, float fw )
{
	vec2 local = vec2( ( p.x - x0 ) / H, ( y1 - p.y ) / H );
	float dist = glyphDistance( g, local ) + bold;
	return clamp( 0.5 + dist * H / fw, 0.0, 1.0 );
}

float inkX0( int g )
{
	return texelFetch( Metrics, ivec2( g, 0 ), 0 ).w;
}
float inkX1( int g )
{
	return texelFetch( Metrics, ivec2( g, 1 ), 0 ).x;
}
float advanceOf( int g )
{
	return texelFetch( Metrics, ivec2( g, 0 ), 0 ).z;
}

//One glyph, its ink centred on c.
float charCover( vec2 p, int g, vec2 c, float H, float bold, float fw )
{
	float x0 = c.x - 0.5 * ( inkX0( g ) + inkX1( g ) ) * H;
	return glyphCover( p, g, x0, c.y + 0.5 * H, H, bold, fw );
}

//PICTURE (word 0) or START (word 1), its ink centred on c.
float wordCover( vec2 p, int word, vec2 c, float H, float fw )
{
	int n = word == 0 ? 7 : 5;
	float pen = 0.0;
	int first = word == 0 ? PICTURE[ 0 ] : START[ 0 ];
	int last  = word == 0 ? PICTURE[ 6 ] : START[ 4 ];
	float width = -inkX0( first );
	for( int i = 0; i < n - 1; ++i )
		width += advanceOf( word == 0 ? PICTURE[ i ] : START[ i ] );
	width += inkX1( last );

	float start = c.x - 0.5 * width * H - inkX0( first ) * H;
	float ink   = 0.0;
	for( int i = 0; i < n; ++i )
	{
		int g = word == 0 ? PICTURE[ i ] : START[ i ];
		ink   = max( ink, glyphCover( p, g, start + pen * H, c.y + 0.5 * H, H, 0.02, fw ) );
		pen += advanceOf( g );
	}
	return ink;
}

float lineCover( float distance, float halfWidth, float fw )
{
	return clamp( 0.5 + ( halfWidth - distance ) / fw, 0.0, 1.0 );
}

float crosshair( vec2 p, float fw )
{
	return max( lineCover( abs( p.x ), 0.004, fw ), lineCover( abs( p.y ), 0.004, fw ) );
}

//Leader frame j at picture coordinates uv (0..1, v down). fw is a pixel's
//footprint in display units.
vec3 leaderFrame( vec2 uvPic, int j, float fw )
{
	if( j > TWO || j < 0 )
		return BlackColour;
	if( j == TWO && PopFlash != 0 )
		return vec3( 1.0 );

	vec2 p  = vec2( ( uvPic.x - 0.5 ) * DisplayAspect, uvPic.y - 0.5 );
	float r = length( p );

	if( j == 0 )
	{
		vec3 c    = mix( vec3( LIGHT ), vec3( 1.0 ), crosshair( p, fw ) );
		float ink = max( wordCover( p, 0, vec2( 0.0, -0.09 ), 0.11, fw ), wordCover( p, 1, vec2( 0.0, 0.09 ), 0.11, fw ) );
		return mix( c, vec3( 0.0 ), ink );
	}

	int count = j == TWO ? 2 : 8 - j / CountFrames;
	int k     = j == TWO ? 0 : j - ( j / CountFrames ) * CountFrames;
	float A   = 2.0 * PI * float( k ) / float( CountFrames );

	//The sweep: the frame behind the hand goes dark, clockwise from twelve.
	float swept = 0.0;
	if( k > 0 )
	{
		float a = atan( p.x, -p.y );
		if( a < 0.0 )
			a += 2.0 * PI;
		swept = clamp( 0.5 + min( a, A - a ) * r / fw, 0.0, 1.0 );
	}
	vec3 c = mix( vec3( LIGHT ), vec3( DARK ), swept );

	if( j != TWO )
	{
		vec2 dir    = vec2( sin( A ), -cos( A ) );
		float along = dot( p, dir );
		float off   = along > 0.0 ? abs( p.x * dir.y - p.y * dir.x ) : r;
		c = mix( c, vec3( 0.0 ), lineCover( off, 0.004, fw ) );
	}

	float rings = max( lineCover( abs( r - 0.36 ), 0.008, fw ), lineCover( abs( r - 0.30 ), 0.008, fw ) );
	c = mix( c, vec3( 1.0 ), rings );
	c = mix( c, vec3( 1.0 ), crosshair( p, fw ) );
	c = mix( c, vec3( 0.0 ), charCover( p, count, vec2( 0.0 ), DigitHeight, DigitBold, fw ) );

	//The control frames: C C F F round the count of four.
	if( count == 4 )
	{
		float x  = 0.5 * DisplayAspect - 0.10;
		float cf = max( max( charCover( p, GLYPH_C, vec2( -x, -0.40 ), 0.08, 0.0, fw ),
		                     charCover( p, GLYPH_C, vec2( x, -0.40 ), 0.08, 0.0, fw ) ),
		                max( charCover( p, GLYPH_F, vec2( -x, 0.40 ), 0.08, 0.0, fw ),
		                     charCover( p, GLYPH_F, vec2( x, 0.40 ), 0.08, 0.0, fw ) ) );
		c = mix( c, vec3( 0.0 ), cf );
	}
	return c;
}
)";

//---------------------------------------------------------------------------
// Composite, part 3: what a print carries outside the picture.
//
// The analogue track is variable area: clear where the signal reaches, so its
// clear width is ( 1 + s ) / 2 of the scanned width, bilateral about each
// channel's centre. s is the host's spectrum drawn at the film's speed:
// bin k is a sine at ( k + 0.5 ) BinHz, which lies along the film at
// f / ( FilmRate Pitch ) cycles a millimetre. A bin finer than half a pixel
// is left out rather than aliased. Leader frames are silent except the 2,
// which carries the pop: 1 kHz for one frame.
//
// DTS, Dolby Digital and SDDS are data, so they are drawn as data: hashed
// bits, averaged to their mean where a bit is smaller than a pixel.
//---------------------------------------------------------------------------
const char* const kCompositeTracks = R"(
float trackSignal( int d, float yf )
{
	ivec4 c = contentOf( d );
	if( c.x == 2 )
	{
		if( c.y != TWO )
			return 0.0;
		float nu = 1000.0 / ( FilmRate * Pitch );
		return 0.8 * sin( 2.0 * PI * nu * yf );
	}
	if( c.z < 0 )
		return 0.0;

	float rate = ( Perturb & 64 ) != 0 ? 25.0 : FilmRate;
	float s    = 0.0;
	for( int k = 0; k < 64; ++k )
	{
		float mag = texelFetch( Spectrum, ivec2( k, c.z ), 0 ).r;
		if( mag <= 0.0 )
			continue;
		float nu  = ( float( k ) + 0.5 ) * BinHz / ( rate * Pitch );
		float aa  = clamp( 2.0 - 4.0 * nu * MmPerPixel, 0.0, 1.0 );
		if( aa <= 0.0 )
			break;
		float phase = texelFetch( Spectrum, ivec2( 64 + k, c.z ), 0 ).r;
		if( c.z == LiveRow )
			phase += float( d ) * texelFetch( Spectrum, ivec2( k, StepRow ), 0 ).r;
		s += mag * aa * sin( 2.0 * PI * nu * yf + phase );
	}
	return TrackLevel * s;
}

//Hashed data bits of cell size `bit` mm, averaged where a bit is under a pixel.
float dataBits( vec2 q, float bit, uint salt )
{
	ivec2 b    = ivec2( floor( q / bit ) );
	uint h     = pcg( uint( b.x ) * 73856093u ^ uint( b.y ) * 19349663u ^ salt );
	float v    = float( h & 1u );
	float seen = clamp( bit / MmPerPixel - 0.5, 0.0, 1.0 );
	return mix( 0.5, v, seen );
}

vec3 soundtracks( vec3 col, vec2 f, float yf, int d )
{
	float fp = MmPerPixel;

	if( Channels > 0 && f.x > Analogue.x - fp && f.x < Analogue.y + fp )
	{
		float cov = span( f.x, Analogue.x, Analogue.y );
		float w   = ( Analogue.y - Analogue.x ) / float( Channels );
		int ch    = int( clamp( floor( ( f.x - Analogue.x ) / w ), 0.0, float( Channels - 1 ) ) );
		float xc  = Analogue.x + ( float( ch ) + 0.5 ) * w;
		float s   = trackSignal( d, yf );
		float hw  = 0.42 * w * clamp( 0.5 + 0.5 * s, 0.0, 1.0 );
		float lit = span( f.x, xc - hw, xc + hw );
		col = mix( col, mix( TrackDense, TrackClear, lit ), cov );
	}

	if( Dts.y > Dts.x && f.x > Dts.x - fp && f.x < Dts.y + fp )
	{
		//Time code: a dash per bit, 32 to a frame, the first two and the last
		//two a fixed sync pattern.
		float cell = Pitch / 32.0;
		float cf   = floor( yf / cell );
		int ci     = int( cf );
		float yc   = yf - cf * cell;
		uint bits  = pcg( uint( HashBase + d ) * 2654435761u );
		bool on    = ( ci < 2 || ci >= 30 ) ? ( ci % 2 == 0 ) : ( ( bits >> uint( ci - 2 ) ) & 1u ) != 0u;
		if( on )
		{
			float wide = Dts.y - Dts.x;
			float cov  = span( f.x, Dts.x + 0.1 * wide, Dts.y - 0.1 * wide ) * span( yc, 0.2 * cell, 0.8 * cell );
			col = mix( col, TrackClear, cov );
		}
	}

	if( Dolby != 0 )
	{
		//A block between every two near holes, its middle left clear.
		float jb = floor( ( f.y - HoleGrid.y ) / HoleGrid.x );
		vec2 q   = vec2( f.x - ( HoleGrid.z + 0.5 * HoleSize.x ), f.y - ( HoleGrid.y + ( jb + 0.5 ) * HoleGrid.x ) );
		float hs = 1.1;
		if( abs( q.x ) < hs + fp && abs( q.y ) < hs + fp )
		{
			float cov    = span( q.x, -hs, hs ) * span( q.y, -hs, hs );
			float level  = dataBits( q + vec2( hs ), 2.0 * hs / 64.0, uint( HashBase * 4 + int( jb ) + 64 ) );
			float middle = span( q.x, -0.35, 0.35 ) * span( q.y, -0.35, 0.35 );
			level        = mix( level, 1.0, middle );
			col = mix( col, mix( TrackDense, TrackClear, level ), cov );
		}
	}

	if( Sdds.y > Sdds.x )
	{
		float xn = f.x;
		float xf = FilmWidth - f.x;
		float x  = min( xn, xf );
		if( x > Sdds.x - fp && x < Sdds.y + fp )
		{
			//SDDS prints as a faint pattern, not a clear one.
			float cov   = span( x, Sdds.x, Sdds.y );
			float level = dataBits( vec2( x, f.y + float( HashBase % 1024 ) * Pitch ), 0.08, xn < xf ? 17u : 29u );
			col = mix( col, mix( TrackDense, TrackClear, 0.15 + 0.35 * level ), cov );
		}
	}
	return col;
}

//The 5x7 font, glyph pixel ( gx, gy ) of ASCII `code`, top row 0.
float fontBit( int code, int gx, int gy )
{
	if( gx < 0 || gx > 4 || gy < 0 || gy > 6 || code < 32 || code > 127 )
		return 0.0;
	return texelFetch( Font, ivec2( code * 5 + gx, gy ), 0 ).r;
}

int codeChar( int i )
{
	if( i < 4 )
		return Code0[ i ];
	if( i < 8 )
		return Code1[ i - 4 ];
	return 0;
}

int codeLength()
{
	int n = 0;
	for( int i = 0; i < 8; ++i )
		if( codeChar( i ) != 0 )
			n = i + 1;
	return n;
}

int digitOf( int number, int place )
{
	for( int i = 0; i < place; ++i )
		number /= 10;
	return 48 + number % 10;
}

//Character i of a label: the stock code, a space, the number in `digits`
//places (or, with `withCode` false, only the number and an optional A).
int labelChar( int i, int number, int digits, bool withCode, bool suffixA )
{
	int n = withCode ? codeLength() : 0;
	if( withCode )
	{
		if( i < n )
			return codeChar( i );
		if( i == n )
			return 32;
		i -= n + 1;
	}
	if( i < digits )
		return digitOf( number, digits - 1 - i );
	if( suffixA && i == digits )
		return 65;
	return 0;
}

//Ink of a label in a band, t mm along it from its start.
float labelInk( float x, vec2 band, float t, int number, int digits, bool withCode, bool suffixA )
{
	float gp = ( band.y - band.x ) / 7.0;
	if( t < 0.0 || x < band.x || x > band.y )
		return 0.0;
	int col = int( floor( t / gp ) );
	int ci  = col / 6;
	int gx  = col - ci * 6;
	int gy  = GlyphUp > 0 ? int( floor( ( band.y - x ) / gp ) ) : int( floor( ( x - band.x ) / gp ) );
	int code = labelChar( ci, number, digits, withCode, suffixA );
	if( code == 0 )
		return 0.0;
	float bit = fontBit( code, gx, gy );
	return mix( 0.25, bit, clamp( gp / MmPerPixel - 0.5, 0.0, 1.0 ) );
}

float labelLength( int digits, bool withCode, bool suffixA, vec2 band )
{
	int n = ( withCode ? codeLength() + 1 : 0 ) + digits + ( suffixA ? 1 : 0 );
	return float( n * 6 - 1 ) * ( band.y - band.x ) / 7.0;
}

float edgeInk( vec2 f )
{
	if( FramesPerLabel <= 0 )
		return 0.0;

	if( ImageAcross != 0 )
	{
		//The still roll: the code twice a frame on the far margin, the frame
		//number under each frame's middle and the number with an A between
		//frames on the near one.
		float ink = 0.0;
		float fd  = floor( f.y / Pitch );
		float yf  = f.y - fd * Pitch;
		int number = ( StillBase + int( fd ) ) % 37;
		float lenCode = labelLength( 0, true, false, PrintFar );
		for( int q = 0; q < 2; ++q )
		{
			float c = ( q == 0 ? 0.25 : 0.75 ) * Pitch;
			ink     = max( ink, labelInk( f.x, PrintFar, yf - ( c - 0.5 * lenCode ), 0, 0, true, false ) );
		}
		float lenN = labelLength( 2, false, false, PrintNear );
		ink        = max( ink, labelInk( f.x, PrintNear, yf - ( 0.5 * Pitch - 0.5 * lenN ), number, 2, false, false ) );
		float lenA = labelLength( 2, false, true, PrintNear );
		float yb   = yf < 0.5 * Pitch ? yf : yf - Pitch;//from the nearest frame line
		int before = yf < 0.5 * Pitch ? ( number + 36 ) % 37 : number;
		ink        = max( ink, labelInk( f.x, PrintNear, yb + 0.5 * lenA, before, 2, false, true ) );
		return ink;
	}

	//A key number every FramesPerLabel frames, from 1 mm into its frame
	//(toward the tail, or from 1 mm short of the frame's end toward the head).
	float period = float( FramesPerLabel ) * Pitch;
	float y      = f.y + float( LabelRem ) * Pitch;
	float len    = labelLength( 4, true, false, PrintFar );
	float t;
	float L;
	if( GlyphAdvance > 0 )
	{
		L = floor( ( y - 1.0 ) / period );
		t = ( y - 1.0 ) - L * period;
	}
	else
	{
		L = ceil( ( y - ( Pitch - 1.0 ) ) / period );
		t = L * period + Pitch - 1.0 - y;
	}
	if( t > len )
		return 0.0;
	int number = ( LabelBase + int( L ) + 10000 ) % 10000;
	return labelInk( f.x, PrintFar, t, number, 4, true, false );
}
)";

//---------------------------------------------------------------------------
// Composite, part 4: the strip, and the output.
//---------------------------------------------------------------------------
const char* const kCompositeMain = R"(
//The clip at picture coordinates, through the window, boxed over the pixel's
//footprint (up to 4 x 4 taps).
vec3 clipAt( vec2 uvPic, vec2 footprint )
{
	vec2 src = Window.xy + uvPic * Window.zw;
	if( src.x < 0.0 || src.y < 0.0 || src.x > 1.0 || src.y > 1.0 )
		return vec3( 0.0 );
	vec2 texels = footprint * Window.zw * InputSize;
	ivec2 n     = clamp( ivec2( ceil( texels ) ), ivec2( 1 ), ivec2( 4 ) );
	vec2 stride = footprint * Window.zw / vec2( n );
	vec2 corner = src - 0.5 * footprint * Window.zw;
	vec2 halfTexel = 0.5 / InputSize;
	vec3 sum = vec3( 0.0 );
	for( int j = 0; j < n.y; ++j )
		for( int i = 0; i < n.x; ++i )
		{
			vec2 s = clamp( corner + ( vec2( i, j ) + 0.5 ) * stride, halfTexel, vec2( 1.0 ) - halfTexel );
			sum += textureLod( InputTexture, vec2( s.x, 1.0 - s.y ) * MaxUV, 0.0 ).rgb;
		}
	return sum / float( n.x * n.y );
}

float luma( vec3 c )
{
	return dot( c, vec3( 0.2126, 0.7152, 0.0722 ) );
}

//Frame d's picture where this pixel is inside it, ADDED to `sum` with its
//coverage added to `cover`. yf is the pixel's distance from the top of frame d
//(it may run past either end, for the neighbour across a frame line). A frame
//that is Clear adds its coverage to `clearPart` instead.
//
//Added, not mixed in turn: two pictures either side of a frame line never
//overlap, so a pixel across a hairline (Super 8's is 0.014 mm) is base where
//neither is, plus each picture by its own coverage. Mixing the second over
//the first would scale the first's share down by the second's coverage --
//`gutest --aperture` found Super 8's picture 0.12 px short that way.
void pictureInto( inout vec3 sum, inout float cover, inout float clearPart, float x, float yf, int d )
{
	float cov = span( x, Image.x, Image.y ) * span( yf, Image.z, Image.w );
	if( cov <= 0.0 )
		return;

	float w = Image.y - Image.x;
	float h = Image.w - Image.z;
	vec2 uvPic;
	vec2 footprint;
	float heightMm;
	if( ImageAcross != 0 )
	{
		uvPic     = vec2( ( yf - Image.z ) / h, ( x - Image.x ) / w );
		footprint = vec2( MmPerPixel / h, MmPerPixel / w );
		heightMm  = w;
	}
	else
	{
		uvPic     = vec2( ( x - Image.x ) / w, ( yf - Image.z ) / h );
		footprint = vec2( MmPerPixel / w, MmPerPixel / h );
		heightMm  = h;
	}
	uvPic = clamp( uvPic, 0.0, 1.0 );

	ivec4 c = contentOf( d );
	vec3 pic;
	if( c.x == 0 )
		pic = clipAt( uvPic, footprint );
	else if( c.x == 1 )
		pic = textureLod( Held, vec3( uvPic.x, 1.0 - uvPic.y, float( c.y ) ), 0.0 ).rgb;
	else if( c.x == 2 )
	{
		float fw = max( MmPerPixel / heightMm, footprint.x * DisplayAspect );
		pic      = leaderFrame( uvPic, c.y, fw );
	}
	else if( c.x == 3 )
		pic = vec3( 1.0 );
	else if( c.x == 5 )
	{
		clearPart += cov;
		cover += cov;
		return;
	}
	else
		pic = BlackColour;

	if( Mono != 0 )
		pic = vec3( luma( pic ) );
	sum += pic * cov;
	cover += cov;
}

float holeCoverage( vec2 f )
{
	float jh  = floor( ( f.y - HoleGrid.y ) / HoleGrid.x + 0.5 );
	float cy  = HoleGrid.y + jh * HoleGrid.x;
	vec2 size = ( Perturb & 1 ) != 0 ? HoleSize.yx : HoleSize.xy;
	float cov = 0.0;
	if( HoleRows.x != 0 )
		cov = max( cov, roundedRect( f - vec2( HoleGrid.z + 0.5 * size.x, cy ), size, HoleSize.z ) );
	if( HoleRows.y != 0 )
		cov = max( cov, roundedRect( f - vec2( FilmWidth - HoleGrid.z - 0.5 * size.x, cy ), size, HoleSize.z ) );
	return cov;
}

vec4 clipPremultiplied( vec2 pix )
{
	vec2 s = vec2( pix.x / float( Size.x ), 1.0 - pix.y / float( Size.y ) );
	return texture( InputTexture, s * MaxUV );
}

vec4 surroundAt( vec2 pix )
{
	if( SurroundKind == 1 )
		return vec4( 0.0, 0.0, 0.0, 1.0 );
	if( SurroundKind == 2 )
		return vec4( 1.0 );
	if( SurroundKind == 3 && IsEffect != 0 )
		return clipPremultiplied( pix );
	return vec4( 0.0 );
}

vec4 shade( vec2 f, vec4 surround )
{
	float filmCov = span( f.x, 0.0, FilmWidth );
	if( filmCov <= 0.0 )
		return surround;

	float fd = floor( f.y / Pitch );
	int d    = int( fd );
	float yf = f.y - fd * Pitch;

	vec3 sum        = vec3( 0.0 );
	float cover     = 0.0;
	float clearPart = 0.0;
	pictureInto( sum, cover, clearPart, f.x, yf, d );
	if( yf < MmPerPixel )
		pictureInto( sum, cover, clearPart, f.x, yf + Pitch, d - 1 );
	if( yf > Pitch - MmPerPixel )
		pictureInto( sum, cover, clearPart, f.x, yf - Pitch, d + 1 );
	//A Clear frame's coverage counts as picture (no base under it) but adds no
	//colour: it is cut out of the film below.
	vec3 col = BaseColour * ( 1.0 - min( cover, 1.0 ) ) + sum;

	if( Soundtracks != 0 )
		col = soundtracks( col, f, yf, d );
	if( EdgePrint != 0 )
		col = mix( col, InkColour, edgeInk( f ) );

	//Premultiplied: col already leaves the Clear part out (no base under it,
	//no colour from it), so only the alpha loses it.
	vec4 film = vec4( col, 1.0 - clamp( clearPart, 0.0, 1.0 ) );
	film      = mix( film, HoleColour, holeCoverage( f ) );
	return mix( surround, film, filmCov );
}

void main()
{
	vec2 pix      = vec2( gl_FragCoord.x, float( Size.y ) - gl_FragCoord.y );
	vec2 f0       = Origin + pix.x * AxisX + pix.y * AxisY;
	vec4 surround = surroundAt( pix );

	vec4 sum = vec4( 0.0 );
	for( int k = 0; k < Taps; ++k )
		sum += shade( f0 - vec2( 0.0, ( float( k ) + 0.5 ) * TapStep ), surround );
	vec4 result = sum / float( max( Taps, 1 ) );

	if( IsEffect != 0 && MixAmount < 1.0 )
		result = mix( clipPremultiplied( pix ), result, MixAmount );
	fragColor = result;
}
)";

std::string assemble( std::initializer_list< const char* > pieces )
{
	std::string s = kVersion;
	for( const char* piece : pieces )
		s += piece;
	return s;
}
} // namespace

std::string Vertex()
{
	return assemble( { kVertexBody } );
}

std::string Capture()
{
	return assemble( { kCaptureBody } );
}

std::string Composite()
{
	return assemble( { kCompositeHead, kCompositeLeader, kCompositeTracks, kCompositeMain } );
}

std::string Copy()
{
	return assemble( { kCopyBody } );
}

} // namespace gauge::shaders
