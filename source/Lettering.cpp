#include "Lettering.h"

#include <algorithm>
#include <cmath>

/*
	jackpot's built-in face (source/Typeface.cpp, MIT, Stoatworks Labs), whose
	digits are polyhedral's: the strokes, the exact distance field and the cell
	layout, copied unchanged apart from the names. Everything to do with font
	files is left behind.
*/
namespace gauge::lettering
{
int GlyphIndex( char c )
{
	if( c >= 'a' && c <= 'z' )
		c = static_cast< char >( c - 'a' + 'A' );
	for( int i = 0; i < kGlyphCount; ++i )
		if( kCharset[ i ] == c )
			return i;
	return -1;
}

//---------------------------------------------------------------------------
// The built-in face.
//---------------------------------------------------------------------------
namespace
{
using Stroke = std::vector< std::pair< float, float > >;

const std::vector< std::vector< Stroke > >& Strokes();

/// An arc as a polyline, angles in degrees, counter-clockwise when a1 > a0.
void Arc( Stroke& into, float cx, float cy, float rx, float ry, float a0, float a1, int segments = 24 )
{
	for( int i = 0; i <= segments; ++i )
	{
		const float a = ( a0 + ( a1 - a0 ) * static_cast< float >( i ) / static_cast< float >( segments ) ) * 3.14159265f / 180.0f;
		into.push_back( { cx + rx * std::cos( a ), cy + ry * std::sin( a ) } );
	}
}

Stroke Line( std::initializer_list< std::pair< float, float > > points )
{
	return Stroke( points );
}

/// Ink centrelines in 0..0.62 x 0.075..0.925 (the stroke adds kHalfWidth,
/// so a capital's ink spans 0..1). The digits are polyhedral's.
std::vector< std::vector< Stroke > > MakeStrokes()
{
	std::vector< std::vector< Stroke > > g( kGlyphCount );
	auto at = [ & ]( char c ) -> std::vector< Stroke >& { return g[ static_cast< size_t >( GlyphIndex( c ) ) ]; };

	//-- digits ---------------------------------------------------------------
	{
		Stroke s;
		Arc( s, 0.3f, 0.5f, 0.24f, 0.425f, 0.0f, 360.0f, 48 );
		at( '0' ) = { s };
	}
	at( '1' ) = { Line( { { 0.13f, 0.80f }, { 0.36f, 0.925f }, { 0.36f, 0.075f } } ) };
	{
		Stroke s;
		Arc( s, 0.3f, 0.70f, 0.235f, 0.225f, 165.0f, -40.0f );
		s.push_back( { 0.06f, 0.075f } );
		s.push_back( { 0.56f, 0.075f } );
		at( '2' ) = { s };
	}
	{
		Stroke upper, lower;
		Arc( upper, 0.29f, 0.725f, 0.215f, 0.20f, 150.0f, -90.0f );
		Arc( lower, 0.29f, 0.29f, 0.245f, 0.235f, 90.0f, -150.0f );
		at( '3' ) = { upper, lower };
	}
	at( '4' ) = { Line( { { 0.44f, 0.075f }, { 0.44f, 0.925f }, { 0.04f, 0.30f }, { 0.58f, 0.30f } } ) };
	{
		Stroke s = { { 0.54f, 0.925f }, { 0.12f, 0.925f }, { 0.08f, 0.53f } };
		Arc( s, 0.30f, 0.315f, 0.25f, 0.245f, 120.0f, -150.0f );
		at( '5' ) = { s };
	}
	{
		Stroke bowl, stem;
		Arc( bowl, 0.30f, 0.31f, 0.245f, 0.235f, 0.0f, 360.0f, 40 );
		stem = { { 0.46f, 0.925f }, { 0.105f, 0.43f } };
		at( '6' ) = { bowl, stem };
	}
	at( '7' ) = { Line( { { 0.05f, 0.925f }, { 0.56f, 0.925f }, { 0.20f, 0.075f } } ) };
	{
		Stroke upper, lower;
		Arc( upper, 0.30f, 0.725f, 0.195f, 0.20f, 0.0f, 360.0f, 36 );
		Arc( lower, 0.30f, 0.285f, 0.245f, 0.21f, 0.0f, 360.0f, 40 );
		at( '8' ) = { upper, lower };
	}
	//Nine is six turned half round about the middle of its box.
	for( const Stroke& s : at( '6' ) )
	{
		Stroke turned;
		for( const auto& p : s )
			turned.push_back( { 0.6f - p.first, 1.0f - p.second } );
		at( '9' ).push_back( turned );
	}

	//-- capitals -------------------------------------------------------------
	at( 'A' ) = { Line( { { 0.04f, 0.075f }, { 0.30f, 0.925f }, { 0.56f, 0.075f } } ), Line( { { 0.13f, 0.36f }, { 0.47f, 0.36f } } ) };
	{
		Stroke s = { { 0.08f, 0.075f }, { 0.08f, 0.925f }, { 0.34f, 0.925f } };
		Arc( s, 0.34f, 0.71f, 0.215f, 0.215f, 90.0f, -90.0f, 16 );
		s.push_back( { 0.08f, 0.495f } );
		Stroke lower = { { 0.08f, 0.495f }, { 0.36f, 0.495f } };
		Arc( lower, 0.36f, 0.285f, 0.21f, 0.21f, 90.0f, -90.0f, 16 );
		lower.push_back( { 0.08f, 0.075f } );
		at( 'B' ) = { s, lower };
	}
	{
		Stroke s;
		Arc( s, 0.32f, 0.5f, 0.27f, 0.425f, 50.0f, 310.0f, 40 );
		at( 'C' ) = { s };
	}
	{
		Stroke s = { { 0.08f, 0.075f }, { 0.08f, 0.925f }, { 0.26f, 0.925f } };
		Arc( s, 0.26f, 0.5f, 0.30f, 0.425f, 90.0f, -90.0f, 28 );
		s.push_back( { 0.08f, 0.075f } );
		at( 'D' ) = { s };
	}
	at( 'E' ) = { Line( { { 0.54f, 0.925f }, { 0.08f, 0.925f }, { 0.08f, 0.075f }, { 0.54f, 0.075f } } ),
		          Line( { { 0.08f, 0.5f }, { 0.44f, 0.5f } } ) };
	at( 'F' ) = { Line( { { 0.54f, 0.925f }, { 0.08f, 0.925f }, { 0.08f, 0.075f } } ), Line( { { 0.08f, 0.5f }, { 0.42f, 0.5f } } ) };
	{
		Stroke s;
		Arc( s, 0.32f, 0.5f, 0.27f, 0.425f, 45.0f, 360.0f, 40 );
		s.push_back( { 0.36f, 0.5f } );
		at( 'G' ) = { s };
	}
	at( 'H' ) = { Line( { { 0.08f, 0.075f }, { 0.08f, 0.925f } } ), Line( { { 0.52f, 0.075f }, { 0.52f, 0.925f } } ),
		          Line( { { 0.08f, 0.5f }, { 0.52f, 0.5f } } ) };
	at( 'I' ) = { Line( { { 0.3f, 0.075f }, { 0.3f, 0.925f } } ), Line( { { 0.14f, 0.925f }, { 0.46f, 0.925f } } ),
		          Line( { { 0.14f, 0.075f }, { 0.46f, 0.075f } } ) };
	{
		Stroke s = { { 0.50f, 0.925f }, { 0.50f, 0.30f } };
		Arc( s, 0.29f, 0.30f, 0.21f, 0.225f, 0.0f, -180.0f, 20 );
		at( 'J' ) = { s };
	}
	at( 'K' ) = { Line( { { 0.08f, 0.075f }, { 0.08f, 0.925f } } ), Line( { { 0.54f, 0.925f }, { 0.08f, 0.36f } } ),
		          Line( { { 0.24f, 0.53f }, { 0.56f, 0.075f } } ) };
	at( 'L' ) = { Line( { { 0.08f, 0.925f }, { 0.08f, 0.075f }, { 0.54f, 0.075f } } ) };
	at( 'M' ) = { Line( { { 0.05f, 0.075f }, { 0.05f, 0.925f }, { 0.30f, 0.40f }, { 0.55f, 0.925f }, { 0.55f, 0.075f } } ) };
	at( 'N' ) = { Line( { { 0.08f, 0.075f }, { 0.08f, 0.925f }, { 0.52f, 0.075f }, { 0.52f, 0.925f } } ) };
	{
		Stroke s;
		Arc( s, 0.3f, 0.5f, 0.26f, 0.425f, 0.0f, 360.0f, 48 );
		at( 'O' ) = { s };
		at( 'Q' ) = { s, Line( { { 0.36f, 0.25f }, { 0.58f, 0.04f } } ) };
	}
	{
		Stroke s = { { 0.08f, 0.075f }, { 0.08f, 0.925f }, { 0.32f, 0.925f } };
		Arc( s, 0.32f, 0.70f, 0.225f, 0.225f, 90.0f, -90.0f, 16 );
		s.push_back( { 0.08f, 0.475f } );
		at( 'P' ) = { s };
		at( 'R' ) = { s, Line( { { 0.30f, 0.475f }, { 0.56f, 0.075f } } ) };
	}
	{
		Stroke s;
		Arc( s, 0.3f, 0.715f, 0.22f, 0.21f, 20.0f, 270.0f, 24 );
		Arc( s, 0.3f, 0.29f, 0.24f, 0.215f, 90.0f, -160.0f, 24 );
		at( 'S' ) = { s };
	}
	at( 'T' ) = { Line( { { 0.03f, 0.925f }, { 0.57f, 0.925f } } ), Line( { { 0.3f, 0.925f }, { 0.3f, 0.075f } } ) };
	{
		Stroke s = { { 0.08f, 0.925f }, { 0.08f, 0.30f } };
		Arc( s, 0.30f, 0.30f, 0.22f, 0.225f, 180.0f, 360.0f, 20 );
		s.push_back( { 0.52f, 0.925f } );
		at( 'U' ) = { s };
	}
	at( 'V' ) = { Line( { { 0.04f, 0.925f }, { 0.30f, 0.075f }, { 0.56f, 0.925f } } ) };
	at( 'W' ) = { Line( { { 0.02f, 0.925f }, { 0.16f, 0.075f }, { 0.30f, 0.60f }, { 0.44f, 0.075f }, { 0.58f, 0.925f } } ) };
	at( 'X' ) = { Line( { { 0.06f, 0.925f }, { 0.54f, 0.075f } } ), Line( { { 0.54f, 0.925f }, { 0.06f, 0.075f } } ) };
	at( 'Y' ) = { Line( { { 0.04f, 0.925f }, { 0.30f, 0.50f }, { 0.56f, 0.925f } } ), Line( { { 0.30f, 0.50f }, { 0.30f, 0.075f } } ) };
	at( 'Z' ) = { Line( { { 0.06f, 0.925f }, { 0.54f, 0.925f }, { 0.06f, 0.075f }, { 0.54f, 0.075f } } ) };

	//-- punctuation ----------------------------------------------------------
	{
		Stroke s;
		Arc( s, 0.3f, 0.68f, 0.19f, 0.17f, 20.0f, 270.0f, 20 );
		Arc( s, 0.3f, 0.34f, 0.20f, 0.17f, 90.0f, -160.0f, 20 );
		at( '$' ) = { s, Line( { { 0.3f, 0.02f }, { 0.3f, 0.98f } } ) };
	}
	at( '\'' ) = { Line( { { 0.30f, 0.925f }, { 0.27f, 0.70f } } ) };
	at( '+' )  = { Line( { { 0.3f, 0.25f }, { 0.3f, 0.75f } } ), Line( { { 0.05f, 0.5f }, { 0.55f, 0.5f } } ) };
	at( '-' )  = { Line( { { 0.1f, 0.5f }, { 0.5f, 0.5f } } ) };
	at( '.' )  = { Line( { { 0.3f, 0.075f }, { 0.3f, 0.09f } } ) };
	at( ':' )  = { Line( { { 0.3f, 0.25f }, { 0.3f, 0.26f } } ), Line( { { 0.3f, 0.70f }, { 0.3f, 0.71f } } ) };
	at( '/' )  = { Line( { { 0.08f, 0.075f }, { 0.52f, 0.925f } } ) };
	at( '!' )  = { Line( { { 0.3f, 0.925f }, { 0.3f, 0.32f } } ), Line( { { 0.3f, 0.075f }, { 0.3f, 0.09f } } ) };
	return g;
}

float SegmentDistance( float px, float py, float ax, float ay, float bx, float by )
{
	const float dx = bx - ax, dy = by - ay;
	const float l2 = dx * dx + dy * dy;
	float t        = l2 > 0.0f ? ( ( px - ax ) * dx + ( py - ay ) * dy ) / l2 : 0.0f;
	t              = std::clamp( t, 0.0f, 1.0f );
	const float x = ax + t * dx - px, y = ay + t * dy - py;
	return std::sqrt( x * x + y * y );
}

uint8_t Encode( float distancePx )
{
	const float v = Atlas::kOnEdge + distancePx * ( Atlas::kOnEdge / Atlas::kSpread );
	return static_cast< uint8_t >( std::clamp( std::lround( v ), 0L, 255L ) );
}

void CellOrigin( int g, int& x, int& y )
{
	x = ( g % Atlas::kColumns ) * Atlas::kCell;
	y = ( g / Atlas::kColumns ) * Atlas::kCell;
}

/// The built-in character into its cell, distance computed exactly from the strokes.
void DrawBuiltin( Atlas& atlas, int index, float pxPerUnit )
{
	int cx = 0, cy = 0;
	CellOrigin( index, cx, cy );
	//Local origin 20 px in from the cell's bottom-left: the ink (0..0.62 x 0..1,
	//plus the stroke) and the spread both fit in 128 px at 88 px per H.
	const float ox = static_cast< float >( cx ) + 20.0f;
	const float oy = static_cast< float >( cy ) + 20.0f;
	const auto& strokes = Strokes()[ static_cast< size_t >( index ) ];
	for( int y = 0; y < Atlas::kCell; ++y )
		for( int x = 0; x < Atlas::kCell; ++x )
		{
			const float lx = ( static_cast< float >( cx + x ) + 0.5f - ox ) / pxPerUnit;
			const float ly = ( static_cast< float >( cy + y ) + 0.5f - oy ) / pxPerUnit;
			float nearest  = 1e9f;
			for( const auto& stroke : strokes )
				for( size_t i = 0; i + 1 < stroke.size(); ++i )
					nearest = std::min( nearest, SegmentDistance( lx, ly, stroke[ i ].first, stroke[ i ].second,
					                                              stroke[ i + 1 ].first, stroke[ i + 1 ].second ) );
			const float inside = kHalfWidth - nearest;//H units, positive inside
			atlas.pixels[ static_cast< size_t >( cy + y ) * Atlas::kWidth + static_cast< size_t >( cx + x ) ] =
				Encode( inside * pxPerUnit );
		}
	Atlas::Glyph& g = atlas.glyph[ index ];
	g.originX            = ox;
	g.originY            = oy;
	float inkLo = 1e9f, inkHi = -1e9f;
	for( const auto& stroke : strokes )
		for( const auto& p : stroke )
		{
			inkLo = std::min( inkLo, p.first );
			inkHi = std::max( inkHi, p.first );
		}
	g.inkX0   = inkLo - kHalfWidth;
	g.inkX1   = inkHi + kHalfWidth;
	//Proportional: the ink plus a constant side bearing, so I and 1 do not
	//stand in a W's width of space.
	g.advance = std::max( 0.3f, g.inkX1 + 0.14f );
}
const std::vector< std::vector< Stroke > >& Strokes()
{
	static const std::vector< std::vector< Stroke > > glyphs = MakeStrokes();
	return glyphs;
}

Atlas Build()
{
	Atlas atlas;
	atlas.pixels.assign( static_cast< size_t >( Atlas::kWidth ) * Atlas::kHeight, 0 );
	for( int i = 0; i < kGlyphCount; ++i )
		DrawBuiltin( atlas, i, Atlas::kPxPerH );
	return atlas;
}
} // namespace

const Atlas& BuiltinAtlas()
{
	static const Atlas atlas = Build();
	return atlas;
}

float Atlas::Width( const std::string& text ) const
{
	float pen = 0.0f, lo = 1e9f, hi = -1e9f;
	for( char c : text )
	{
		const int g = GlyphIndex( c );
		if( g >= 0 )
		{
			lo = std::min( lo, pen + glyph[ g ].inkX0 );
			hi = std::max( hi, pen + glyph[ g ].inkX1 );
			pen += glyph[ g ].advance;
		}
		else
			pen += 0.35f;
	}
	return hi > lo ? hi - lo : 0.0f;
}

float Atlas::Distance( int index, float x, float y ) const
{
	const Glyph& g = glyph[ std::clamp( index, 0, kGlyphCount - 1 ) ];
	//Texel centres at +0.5, as GL samples them; the cell starts 20 px below and
	//left of the origin.
	const float ax = g.originX + x * kPxPerH - 0.5f;
	const float ay = g.originY + y * kPxPerH - 0.5f;
	const float x0 = g.originX - 20.0f, y0 = g.originY - 20.0f;
	if( ax < x0 || ay < y0 || ax > x0 + kCell - 1.0f || ay > y0 + kCell - 1.0f )
		return -kSpread / kPxPerH;
	const int ix = static_cast< int >( std::floor( ax ) ), iy = static_cast< int >( std::floor( ay ) );
	const float fx = ax - static_cast< float >( ix ), fy = ay - static_cast< float >( iy );
	auto at = [ & ]( int px, int py ) {
		px = std::clamp( px, 0, kWidth - 1 );
		py = std::clamp( py, 0, kHeight - 1 );
		return static_cast< float >( pixels[ static_cast< size_t >( py ) * kWidth + static_cast< size_t >( px ) ] );
	};
	const float v = ( at( ix, iy ) * ( 1 - fx ) + at( ix + 1, iy ) * fx ) * ( 1 - fy )
	              + ( at( ix, iy + 1 ) * ( 1 - fx ) + at( ix + 1, iy + 1 ) * fx ) * fy;
	return ( v - kOnEdge ) / ( kOnEdge / kSpread ) / kPxPerH;
}

} // namespace gauge::lettering
