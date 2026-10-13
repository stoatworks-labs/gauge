#pragma once

/**
	The strip: every gauge as numbers, in film millimetres.

	Film coordinates. `x` runs ACROSS the film from the reference edge (the
	guided edge: the soundtrack side of a 35 mm print, the perforated side of a
	single-perf 16 mm or Super 8 film). `y` runs ALONG it toward the tail, so a
	later frame has a larger y. Frame k occupies y in [ k P, ( k + 1 ) P ), and
	y = k P is the middle of the frame line above it. Everything the composite
	pass draws -- holes, frame lines, picture, soundtracks, edge print -- is a
	function of ( x, y ) and these numbers, never of a screen position.

	Where a number comes from a published standard it says so. Where I could
	not find one (the DTS, Dolby Digital and SDDS placements, the 16 mm optical
	track) it is placed by eye inside the area a standard leaves for it, and
	says that instead.
*/
namespace gauge::strip
{

enum FormatId
{
	kAcademy = 0,///< 35 mm 4-perf release print, Academy aperture, analogue track and DTS
	kScope,      ///< 35 mm 4-perf release print, 2x anamorphic, all four soundtracks
	kFull,       ///< 35 mm 4-perf silent full aperture
	kStill,      ///< 35 mm still (135): 36 x 24 across eight holes, the picture across the film
	k16,         ///< 16 mm double perf, silent
	k16Sound,    ///< 16 mm single perf, optical track on the far edge
	kSuper16,    ///< Super 16: single perf, the picture into the old track area
	kSuper8,     ///< Super 8: one small hole beside the middle of each frame
	kFormatCount
};

/// A perforation: a rounded rectangle.
struct Hole
{
	double across = 0.0;///< mm, the transverse dimension (a 35 mm KS hole's 2.794)
	double along  = 0.0;///< mm, in the direction of travel (a KS hole's 1.981)
	double radius = 0.0;///< corner radius, mm
};

/// A band across the film: x from x0 to x1.
struct Band
{
	double x0 = 0.0;
	double x1 = 0.0;
	bool Present() const
	{
		return x1 > x0;
	}
};

struct Format
{
	const char* name = "";

	double width = 0.0;///< mm across
	double pitch = 0.0;///< frame pitch, mm along

	Hole hole;
	double holePitch  = 0.0;///< mm between hole centres along the film
	double holeOffset = 0.0;///< y of a hole centre within a frame (so every hole is at holeOffset + j holePitch)
	double holeEdge   = 0.0;///< reference edge to the near side of a hole, mm
	bool holesNear    = false;///< a row on the reference edge
	bool holesFar     = false;///< a row on the far edge (at width - holeEdge - hole.across)

	/// The picture: x0..x1 across, centred in the frame along with this height.
	double imageX0     = 0.0;
	double imageX1     = 0.0;
	double imageHeight = 0.0;///< mm along the film

	/// The still format turns the picture: its up is toward the reference edge
	/// and its right is toward the tail. Every motion picture format has its up
	/// toward the head and its right toward the far edge.
	bool imageAcross = false;

	double squeeze = 1.0;///< the format's own anamorphic factor

	/// Soundtracks. An analogue variable-area area, its number of channels
	/// side by side, and the digital records of a late release print.
	Band analogue;
	int channels = 0;
	Band dts;
	bool dolby = false;///< blocks between the near holes
	Band sdds;         ///< near band; the far one is mirrored at the far edge

	/// Edge print: the band across the film, the frames between labels, and
	/// which way a glyph's top points (+1 toward the far edge, -1 toward the
	/// reference edge).
	Band print;
	int framesPerLabel = 0;
	int glyphUp        = 1;
	/// +1: text runs toward the tail. A motion picture print with its edge
	/// print on the near side runs it toward the head, or it would read
	/// mirrored.
	int glyphAdvance   = 1;
	/// The still format carries two lines: a code on the far edge and frame
	/// numbers on the near one.
	Band printNear;

	/// Picture rectangle in film millimetres for frame 0.
	double ImageY0() const
	{
		return 0.5 * ( pitch - imageHeight );
	}
	double ImageY1() const
	{
		return 0.5 * ( pitch + imageHeight );
	}
	double ImageWidth() const
	{
		return imageX1 - imageX0;
	}
	/// The picture's own aspect on the film, width over height as the picture
	/// stands upright (so the still format's is 36 / 24).
	double ApertureAspect() const
	{
		return imageAcross ? imageHeight / ImageWidth() : ImageWidth() / imageHeight;
	}
	/// Holes in one frame's length on one side: 4 for 35 mm, 8 for the still
	/// format, 1 for 16 mm and Super 8.
	int HolesPerFrame() const;
};

const Format& FormatAt( int id );

const char* FormatName( int id );

/// Inches, for the numbers that are published in inches.
constexpr double kInch = 25.4;

} // namespace gauge::strip
