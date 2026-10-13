#pragma once

#include <cstdint>
#include <string>
#include <vector>

/**
	The leader's lettering: a stroked face, as a signed-distance atlas.

	jackpot's built-in face, copied with its distance-field builder and nothing
	else (no font files, no stb_truetype): jackpot draws it when the font a
	composition names is not installed, and its digits are polyhedral's. A
	countdown numeral is seen anywhere from a quarter of the output down to a
	frame a few dozen pixels high on a moving strip, squeezed 2x on a scope
	print, and a distance field stays a good distance at every one of those
	sizes. The same everywhere, because it is defined here.

	One 128-px cell per character, the characters' ink in 0..1 of a common
	height H, 88 px to H. The atlas is R8, row 0 at the bottom, 128 on the
	outline and 12 px of distance either side.
*/
namespace gauge::lettering
{

constexpr const char* kCharset = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ$'+-.:/!";
constexpr int kGlyphCount      = 44;

/// The cell of character `c`, or -1 (a space, or anything not in kCharset).
int GlyphIndex( char c );

/// Half the stroke's width, in H units.
constexpr float kHalfWidth = 0.075f;

struct Atlas
{
	static constexpr int kCell     = 128;
	static constexpr int kColumns  = 8;
	static constexpr int kRows     = 6;
	static constexpr int kWidth    = kCell * kColumns;
	static constexpr int kHeight   = kCell * kRows;
	static constexpr float kOnEdge = 128.0f;///< the byte value on the outline
	static constexpr float kSpread = 12.0f; ///< px of distance either side the bytes cover
	static constexpr float kPxPerH = 88.0f;

	struct Glyph
	{
		float originX = 0, originY = 0;   ///< atlas px (y up) of the glyph's local origin
		float advance = 0.7f;             ///< H units
		float inkX0 = 0.0f, inkX1 = 0.6f; ///< H units, from the pen
	};

	std::vector< uint8_t > pixels;///< kWidth x kHeight, row 0 at the bottom
	Glyph glyph[ kGlyphCount ];

	/// The width of `text` set solid, ink edge to ink edge, in H units.
	float Width( const std::string& text ) const;

	/// Signed distance in H units at local point ( x, y up ) of glyph g:
	/// positive inside. Bilinear, as the shader samples it; -kSpread outside the
	/// glyph's cell.
	float Distance( int g, float x, float y ) const;
};

/// The whole charset, built once.
const Atlas& BuiltinAtlas();

} // namespace gauge::lettering
