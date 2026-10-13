#include "Strip.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace gauge::strip
{
namespace
{
//---------------------------------------------------------------------------
// 35 mm, common to the four 35 mm formats. SMPTE 139 / ISO 491: the film is
// 34.975 mm wide; the KS ("Kodak Standard", print) hole is 0.110 x 0.078 in
// with 0.020 in corners -- 0.110 ACROSS the film, 0.078 along it -- on the
// long pitch of 0.1870 in, 0.079 in from each edge. Four holes to a 0.748 in
// frame, and the frame line midway between two holes.
//---------------------------------------------------------------------------
constexpr double k35Width     = 34.975;
constexpr double k35Pitch     = 4.0 * 4.75;
constexpr double k35HolePitch = 4.75;
constexpr Hole k35Hole        = { 0.110 * kInch, 0.078 * kInch, 0.020 * kInch };
constexpr double k35HoleEdge  = 2.01;

/// The far edge's margin, outside the holes, for an edge print 1 mm tall.
constexpr Band k35PrintFar  = { k35Width - 1.5, k35Width - 0.5 };
constexpr Band k35PrintNear = { 0.5, 1.5 };

/// The analogue track: SMPTE 40's 0.100 in area centred 0.243 in from the
/// guided edge, between the near holes and the picture.
constexpr Band k35Analogue = { 0.243 * kInch - 0.050 * kInch, 0.243 * kInch + 0.050 * kInch };

/// The sound prints' picture: the projection aperture's 0.825 in width
/// centred 0.738 in from the guided edge. Narrower than the camera
/// aperture's 0.866, which is what leaves room for DTS beside the track.
constexpr double k35PrintCentre = 0.738 * kInch;
constexpr double k35PrintWidth  = 0.825 * kInch;

/// Placed by eye: DTS time code in the gap between the analogue track and
/// the picture; SDDS in the margin outside the holes. Neither is from a
/// published drawing.
constexpr Band k35Dts  = { 7.55, 8.10 };
constexpr Band k35Sdds = { 0.20, 1.75 };

Format make35( const char* name )
{
	Format f;
	f.name       = name;
	f.width      = k35Width;
	f.pitch      = k35Pitch;
	f.hole       = k35Hole;
	f.holePitch  = k35HolePitch;
	f.holeOffset = 0.5 * k35HolePitch;
	f.holeEdge   = k35HoleEdge;
	f.holesNear  = true;
	f.holesFar   = true;
	return f;
}

//---------------------------------------------------------------------------
// 16 mm. ISO 69 / SMPTE 109: 16.00 mm wide, the hole 0.0720 x 0.0500 in
// (1.829 x 1.270 mm) with 0.25 mm corners, 0.300 in (7.62 mm) long pitch,
// 0.90 mm from the reference edge; one hole per frame, and the frame line
// through the middle of the hole. Camera aperture 0.404 x 0.295 in, centred
// 7.98 mm from the reference edge; Super 16's 12.52 x 7.41 mm, centred
// 9.00 mm from it and running into the old track area.
//---------------------------------------------------------------------------
constexpr double k16Width   = 16.00;
constexpr double k16Pitch   = 7.62;
constexpr Hole k16Hole      = { 1.829, 1.270, 0.25 };
constexpr double k16Edge    = 0.90;
constexpr Band k16PrintFar  = { 16.00 - 0.78, 16.00 - 0.12 };
constexpr Band k16PrintNear = { 0.12, 0.78 };

Format make16( const char* name )
{
	Format f;
	f.name           = name;
	f.width          = k16Width;
	f.pitch          = k16Pitch;
	f.hole           = k16Hole;
	f.holePitch      = k16Pitch;
	f.holeOffset     = 0.0;
	f.holeEdge       = k16Edge;
	f.holesNear      = true;
	f.imageX0        = 7.98 - 0.5 * 0.404 * kInch;
	f.imageX1        = 7.98 + 0.5 * 0.404 * kInch;
	f.imageHeight    = 0.295 * kInch;
	f.framesPerLabel = 20;//key numbers every half foot: 40 frames to the foot
	return f;
}

std::array< Format, kFormatCount > makeFormats()
{
	std::array< Format, kFormatCount > all{};

	{
		Format f         = make35( "35 mm Academy" );
		f.imageX0        = k35PrintCentre - 0.5 * k35PrintWidth;
		f.imageX1        = k35PrintCentre + 0.5 * k35PrintWidth;
		f.imageHeight    = 0.630 * kInch;//the camera aperture's height: Academy's thick frame line
		f.analogue       = k35Analogue;
		f.channels       = 1;
		f.dts            = k35Dts;
		f.print          = k35PrintFar;
		f.framesPerLabel = 16;//one foot
		all[ kAcademy ]  = f;
	}
	{
		Format f      = make35( "35 mm Scope" );
		f.imageX0     = k35PrintCentre - 0.5 * k35PrintWidth;
		f.imageX1     = k35PrintCentre + 0.5 * k35PrintWidth;
		f.imageHeight = 0.732 * kInch;//the anamorphic camera aperture's height: a hairline
		f.squeeze     = 2.0;
		f.analogue    = k35Analogue;
		f.channels    = 2;
		f.dts         = k35Dts;
		f.dolby       = true;
		f.sdds        = k35Sdds;
		//SDDS fills both margins, so there is nowhere left for an edge print.
		all[ kScope ] = f;
	}
	{
		Format f         = make35( "35 mm Full" );
		f.imageX0        = 0.5 * k35Width - 0.5 * 0.980 * kInch;
		f.imageX1        = 0.5 * k35Width + 0.5 * 0.980 * kInch;
		f.imageHeight    = 0.735 * kInch;
		f.print          = k35PrintFar;
		f.framesPerLabel = 16;
		all[ kFull ]     = f;
	}
	{
		//135: the same film and the same holes, run sideways through a still
		//camera. Eight holes to a 38 mm frame; a 36 x 24 picture, its long
		//side along the film. A code on the far margin, frame numbers on the
		//near one, both every half frame.
		Format f         = make35( "35 mm Still" );
		f.pitch          = 8.0 * k35HolePitch;
		f.imageX0        = 0.5 * k35Width - 12.0;
		f.imageX1        = 0.5 * k35Width + 12.0;
		f.imageHeight    = 36.0;
		f.imageAcross    = true;
		f.print          = k35PrintFar;
		f.printNear      = k35PrintNear;
		f.framesPerLabel = 1;
		f.glyphUp        = -1;
		all[ kStill ]    = f;
	}
	{
		Format f   = make16( "16 mm" );
		f.holesFar = true;//double perf: a hole either side of every frame line
		f.print    = k16PrintFar;
		all[ k16 ] = f;
	}
	{
		//Single perf, the track on the far edge. Placed by eye in the area the
		//single-perf layout leaves free: I found no published centreline.
		Format f        = make16( "16 mm Sound" );
		f.analogue      = { 13.45, 15.25 };
		f.channels      = 1;
		f.print         = k16PrintNear;
		f.glyphUp       = -1;
		f.glyphAdvance  = -1;
		all[ k16Sound ] = f;
	}
	{
		Format f        = make16( "Super 16" );
		f.imageX0       = 9.00 - 0.5 * 12.52;
		f.imageX1       = 9.00 + 0.5 * 12.52;
		f.imageHeight   = 7.41;
		f.print         = k16PrintNear;
		f.glyphUp       = -1;
		f.glyphAdvance  = -1;
		all[ kSuper16 ] = f;
	}
	{
		//Super 8, SMPTE 149 / 157: 7.976 mm wide, a 0.914 x 1.143 mm hole with
		//square corners 0.51 mm from the edge on a 4.234 mm pitch; the camera
		//aperture 5.69 x 4.22 mm starting 1.47 mm from the perforated edge.
		//SMPTE places the picture by the hole two pitches on, which puts each
		//hole beside the middle of a frame; I put it exactly there.
		Format f        = {};
		f.name          = "Super 8";
		f.width         = 7.976;
		f.pitch         = 4.234;
		f.hole          = { 0.914, 1.143, 0.013 };
		f.holePitch     = 4.234;
		f.holeOffset    = 0.5 * 4.234;
		f.holeEdge      = 0.51;
		f.holesNear     = true;
		f.imageX0       = 1.47;
		f.imageX1       = 1.47 + 5.69;
		f.imageHeight   = 4.22;
		all[ kSuper8 ]  = f;
	}
	return all;
}
} // namespace

int Format::HolesPerFrame() const
{
	return holePitch > 0.0 ? static_cast< int >( std::lround( pitch / holePitch ) ) : 0;
}

const Format& FormatAt( int id )
{
	static const std::array< Format, kFormatCount > formats = makeFormats();
	return formats[ static_cast< size_t >( std::clamp( id, 0, kFormatCount - 1 ) ) ];
}

const char* FormatName( int id )
{
	return FormatAt( id ).name;
}

} // namespace gauge::strip
