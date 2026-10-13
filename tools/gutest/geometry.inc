//---------------------------------------------------------------------------
// Shared tools for the checks, and the geometry checks: --perfs, --aperture,
// --squeeze. Included into main.cpp's anonymous namespace.
//
// Every check measures out of the picture. Where it needs to know where to
// look -- which pixel is the middle of the frame in the gate -- it asks the
// plugin's camera, which is placement, never the claim under test.
//---------------------------------------------------------------------------

/// A finished render: the last frame as floats, and the plugin's state.
struct Run
{
	Image image;
	int width  = 0;
	int height = 0;
	GaugePlugin::State state;
};

/// Render `frames` frames through a fresh instance and read the last back.
/// `perFrame` runs before each frame (presses, audio).
bool render( bool effect, int width, int height, const Settings& settings, int perturb, int frames,
             const std::function< Image( int ) >& source, Run& out, double fps = 60.0, double bpm = 120.0,
             const std::function< void( Session&, int ) >& perFrame = {} )
{
	Session session( effect );
	session.fps = fps;
	session.bpm = bpm;
	if( !applyAll( *session.plugin, settings ) )
		return false;
	session.plugin->SetPerturbForTest( perturb );
	if( !session.begin( width, height ) )
		return false;
	for( int frame = 0; frame < frames; ++frame )
	{
		if( perFrame )
			perFrame( session, frame );
		if( !session.render( frame, source( frame ) ) )
		{
			session.end();
			return false;
		}
	}
	out.image  = session.readBackFloat();
	out.width  = width;
	out.height = height;
	out.state  = session.plugin->StateForTest();
	session.end();
	return true;
}

/// The pixel coordinate whose film position is ( x, y ), y relative to the
/// camera's base: the affine camera, inverted.
void screenOf( const camera::Camera& c, double x, double y, double& px, double& py )
{
	const double det = c.axisXx * c.axisYy - c.axisYx * c.axisXy;
	const double dx = x - c.originX, dy = y - c.originY;
	px = ( dx * c.axisYy - c.axisYx * dy ) / det;
	py = ( c.axisXx * dy - dx * c.axisXy ) / det;
}

/// One interval along a profile of box-filtered coverage (pixel i spans
/// [ i, i + 1 ]). Its LENGTH is the sum of the coverage, exactly. Its CENTRE is
/// not the coverage-weighted mean of the pixel centres: a partly covered end
/// pixel's ink sits against its covered side, not at its centre (that mistake
/// put every 35 mm hole 0.007 px toward the film's middle at 320x180). The ends
/// are exact instead -- the first covered pixel i0 starts at i0 + 1 - c( i0 ),
/// the last i1 ends at i1 + c( i1 ) -- provided everything between them is
/// fully covered, which `solid` reports. Coverage under 1e-6 is float noise,
/// and ignoring it moves an end by less than that.
struct Extent
{
	double length = 0.0;
	double centre = 0.0;
	bool solid    = false;
	double Lo() const
	{
		return centre - 0.5 * length;
	}
	double Hi() const
	{
		return centre + 0.5 * length;
	}
};

Extent extentOf( const std::vector< double >& c, int from = 0, int to = -1 )
{
	if( to < 0 )
		to = static_cast< int >( c.size() );
	Extent e;
	int i0 = -1, i1 = -1;
	for( int i = from; i < to; ++i )
	{
		const double v = c[ static_cast< size_t >( i ) ];
		e.length += v;
		//Float noise is not coverage: a flat field box-filtered over three taps
		//comes back an ULP off its level, which reads as 2e-9 of a pixel.
		if( v > 1e-6 )
		{
			if( i0 < 0 )
				i0 = i;
			i1 = i;
		}
	}
	if( i0 < 0 )
		return e;
	if( i0 == i1 )
	{
		e.centre = i0 + 0.5;
		return e;
	}
	e.solid = true;
	for( int i = i0 + 1; i < i1; ++i )
		e.solid = e.solid && c[ static_cast< size_t >( i ) ] >= 1.0 - 1e-6;
	const double e0 = i0 + 1.0 - c[ static_cast< size_t >( i0 ) ];
	const double e1 = i1 + c[ static_cast< size_t >( i1 ) ];
	e.centre        = 0.5 * ( e0 + e1 );
	return e;
}

/// A connected blob of pixels above a threshold: its box and centroid.
struct Blob
{
	int x0, y0, x1, y1;//inclusive
	double cx = 0.0, cy = 0.0;
	bool touchesBorder = false;
};

std::vector< Blob > blobs( const std::vector< double >& field, int width, int height, double threshold )
{
	std::vector< int > label( field.size(), -1 );
	std::vector< Blob > out;
	std::vector< int > stack;
	for( int y = 0; y < height; ++y )
		for( int x = 0; x < width; ++x )
		{
			const size_t i = static_cast< size_t >( y ) * width + x;
			if( field[ i ] <= threshold || label[ i ] >= 0 )
				continue;
			Blob b{ x, y, x, y };
			const int id = static_cast< int >( out.size() );
			stack.assign( 1, static_cast< int >( i ) );
			label[ i ] = id;
			while( !stack.empty() )
			{
				const int p = stack.back();
				stack.pop_back();
				const int px = p % width, py = p / width;
				b.x0 = std::min( b.x0, px );
				b.x1 = std::max( b.x1, px );
				b.y0 = std::min( b.y0, py );
				b.y1 = std::max( b.y1, py );
				const int nx[ 4 ] = { px - 1, px + 1, px, px };
				const int ny[ 4 ] = { py, py, py - 1, py + 1 };
				for( int k = 0; k < 4; ++k )
				{
					if( nx[ k ] < 0 || ny[ k ] < 0 || nx[ k ] >= width || ny[ k ] >= height )
						continue;
					const size_t j = static_cast< size_t >( ny[ k ] ) * width + nx[ k ];
					if( field[ j ] > threshold && label[ j ] < 0 )
					{
						label[ j ] = id;
						stack.push_back( static_cast< int >( j ) );
					}
				}
			}
			b.touchesBorder = b.x0 <= 1 || b.y0 <= 1 || b.x1 >= width - 2 || b.y1 >= height - 2;
			out.push_back( b );
		}
	return out;
}

bool within( double measured, double expected, double tol )
{
	return std::fabs( measured - expected ) <= tol;
}

//---------------------------------------------------------------------------
// The standards, as published -- a second table on purpose. Strip.cpp is the
// model; this is what it was transcribed from, in the standards' own units, so
// a transcription error (a hole turned through 90 degrees, an inch read as the
// wrong number) fails here instead of being measured back faithfully.
//
//   35 mm:   SMPTE 139 / ISO 491. Width 1.377 in (34.975 mm); KS hole 0.110 in
//            across, 0.078 in along; long pitch 0.1870 in; 0.079 in from the
//            edge; four holes to the frame, the frame line midway between two.
//            Sound-print picture: 0.825 in projection width, centred 0.738 in
//            from the guided edge; Academy camera height 0.630 in, anamorphic
//            0.732 in. Silent full aperture 0.980 x 0.735 in, centred.
//   135:     the same film and holes; 36 x 24 mm across eight holes.
//   16 mm:   ISO 69 / SMPTE 109. 16.00 mm; hole 0.0720 x 0.0500 in; pitch
//            0.3000 in; 0.0354 in from the edge; one hole per frame, the frame
//            line through it. Camera aperture 0.404 x 0.295 in, 7.98 mm from
//            the reference edge. Super 16 12.52 x 7.41 mm, centred 9.00 mm.
//   Super 8: SMPTE 149 / 157. 7.976 mm; hole 0.914 x 1.143 mm; pitch 4.234 mm;
//            0.51 mm from the edge; camera aperture 5.69 x 4.22 mm, 1.47 mm
//            from the perforated edge, the hole beside its middle.
//---------------------------------------------------------------------------
struct Published
{
	int format;
	double width;
	double holeAcross, holeAlong, holePitch, holeEdge;
	int holesPerFrame;
	double lineOffset;///< a hole centre's distance past a frame line, in hole pitches
	double picAcross, picAlong, picCentre;
	bool holesFar;
};

constexpr double kIn = 25.4;

const std::vector< Published >& published()
{
	static const std::vector< Published > table = {
		{ strip::kAcademy, 34.975, 0.110 * kIn, 0.078 * kIn, 0.1870 * kIn, 0.079 * kIn, 4, 0.5, 0.825 * kIn, 0.630 * kIn, 0.738 * kIn, true },
		{ strip::kScope, 34.975, 0.110 * kIn, 0.078 * kIn, 0.1870 * kIn, 0.079 * kIn, 4, 0.5, 0.825 * kIn, 0.732 * kIn, 0.738 * kIn, true },
		{ strip::kFull, 34.975, 0.110 * kIn, 0.078 * kIn, 0.1870 * kIn, 0.079 * kIn, 4, 0.5, 0.980 * kIn, 0.735 * kIn, 34.975 / 2.0, true },
		{ strip::kStill, 34.975, 0.110 * kIn, 0.078 * kIn, 0.1870 * kIn, 0.079 * kIn, 8, 0.5, 24.0, 36.0, 34.975 / 2.0, true },
		{ strip::k16, 16.00, 0.0720 * kIn, 0.0500 * kIn, 0.3000 * kIn, 0.0354 * kIn, 1, 0.0, 0.404 * kIn, 0.295 * kIn, 7.98, true },
		{ strip::k16Sound, 16.00, 0.0720 * kIn, 0.0500 * kIn, 0.3000 * kIn, 0.0354 * kIn, 1, 0.0, 0.404 * kIn, 0.295 * kIn, 7.98, false },
		{ strip::kSuper16, 16.00, 0.0720 * kIn, 0.0500 * kIn, 0.3000 * kIn, 0.0354 * kIn, 1, 0.0, 12.52, 7.41, 9.00, false },
		{ strip::kSuper8, 7.976, 0.914, 1.143, 4.234, 0.51, 1, 0.5, 5.69, 4.22, 1.47 + 5.69 / 2.0, false },
	};
	return table;
}

/// How far the model may sit from the published nominal: it rounds some inch
/// figures to a hundredth of a millimetre (0.079 in is 2.0066 mm; the model
/// says 2.01). Each standard's own tolerance on these dimensions is wider.
constexpr double kPublishedTol = 0.005;

/// How far an edge found by summing box-filtered coverage may be from the
/// truth, in pixels. The coverage is an analytic ramp, not a sample, so its
/// sum across an edge is exact and what is left is float: a film coordinate
/// near 40 mm carries an ULP of 4e-6 mm, about 3e-5 px at the finest raster
/// used here; each coverage value is within 2 ULP of exact; an edge's sum
/// collects two or three of them. 1e-3 px an edge is thirty times that.
constexpr double kEdgeTol = 1e-3;

/// A Colour Print rebate's red: the input level that makes a picture
/// indistinguishable from the film around it, in red.
constexpr float kBaseRed = 0.018f;

/// Strip mode at Angle 0 shows this many frames of any format at this size.
double sizeForFrames( const strip::Format& f, int width, int height, double frames )
{
	const double aspect = static_cast< double >( width ) / height;
	return std::min( 1.0, aspect * f.width / ( frames * f.pitch ) );
}

/// One format's strip, measured from two renders of exactly the same moving
/// strip at the same instant -- same settings, same synthetic clock -- running
/// at 6 frames a second with a closed shutter, so nothing is blurred.
///
///   A: the clip at the rebate's red, holes Light. Only the holes differ from
///      the film around them, so each hole's coverage h is exact.
///   B: the same, holes Black, and two frames two apart flashed white (Flash,
///      pressed so each lands on the next frame to come into view). Only those
///      two pictures differ from the film around them -- so a picture is
///      measured whole even where its neighbours touch it (Super 8's frame
///      line is 0.014 mm). With r2 = base ( 1 - p - h ) + p, each picture's
///      coverage is p = ( r2 - base ( 1 - h ) ) / ( 1 - base ).
struct Geometry
{
	bool ok = false;
	double mmPerPx = 0.0;
	std::vector< Extent > holeAcross, holeAlong;///< px, through each hole's middle
	std::vector< Extent > picAcross, picAlong;  ///< the two flashed pictures, in order along
	Extent film;                                ///< across, px
	int flashGap = 0;                           ///< frames between the two pictures
};

/// Film x of an across position (a pixel y) at Strip Angle 0: a motion
/// picture's far edge faces up, the still format's reference edge does.
double filmX( const strip::Format& f, const Geometry& g, double y )
{
	return f.imageAcross ? ( y - g.film.Lo() ) * g.mmPerPx : ( g.film.Hi() - y ) * g.mmPerPx;
}

bool measureGeometry( int format, int width, int height, int perturb, Geometry& g )
{
	const strip::Format& f = strip::FormatAt( format );
	char size[ 32 ], travel[ 32 ];
	std::snprintf( size, sizeof( size ), "%.9g", controls::SizeParam( sizeForFrames( f, width, height, 4.5 ) ) );
	std::snprintf( travel, sizeof( travel ), "%.9g", controls::TravelParam( 6.0 ) );
	const Settings common = {
		{ "Format", f.name },   { "Mode", "Strip" },        { "Size", size },        { "Angle", "0.5" },
		{ "Position", "0.5" },  { "Travel", travel },       { "Shutter", "0" },      { "Frames", "Live" },
		{ "Surround", "Clear" }, { "Soundtracks", "0" },    { "Edge Print", "0" },   { "Stock", "Colour Print" },
		{ "Squeeze", "None" },  { "Start On", "Press" },
	};
	const Image dark = buildFlat( width, height, kBaseRed );

	//B first: it decides how long both run. Flash the next frame to come in,
	//then the one two after it, then run until both are wholly in view.
	Settings picturesWhite = common;
	picturesWhite.push_back( { "Holes", "Black" } );
	Session sb( true );
	if( !applyAll( *sb.plugin, picturesWhite ) )
		return false;
	sb.plugin->SetPerturbForTest( perturb );
	if( !sb.begin( width, height ) )
		return false;
	const int flashIndex = sb.plugin->IndexOf( K_FLASH );
	int64_t first = INT64_MIN, second = INT64_MIN;
	int frames    = 0;
	for( int frame = 0; frame < 2000; ++frame )
	{
		const GaugePlugin::State& before = sb.plugin->StateForTest();
		bool press = false;
		if( frame == 2 )
		{
			first = before.visibleHi + 1;
			press = true;
		}
		else if( first != INT64_MIN && second == INT64_MIN && before.visibleHi + 1 == first + 2 )
		{
			second = first + 2;
			press  = true;
		}
		sb.plugin->SetFloatParameter( static_cast< unsigned int >( flashIndex ), press ? 1.0f : 0.0f );
		sb.render( frame, dark );
		frames = frame + 1;
		if( second == INT64_MIN )
			continue;
		//Both pictures wholly on the output, three pixels in?
		const GaugePlugin::State& st = sb.plugin->StateForTest();
		bool inside                  = true;
		for( int64_t i : { first, second } )
			for( double y : { f.ImageY0(), f.ImageY1() } )
				for( double x : { f.imageX0, f.imageX1 } )
				{
					double px = 0.0, py = 0.0;
					screenOf( st.camera, x, ( static_cast< double >( i - st.camera.base ) ) * f.pitch + y, px, py );
					inside = inside && px > 3.0 && px < width - 3.0 && py > 3.0 && py < height - 3.0;
				}
		if( inside )
			break;
	}
	if( second == INT64_MIN )
	{
		if( std::getenv( "GU_DEBUG" ) )
			std::printf( "  %s: the second flash never came (first %lld)\n", f.name, static_cast< long long >( first ) );
		sb.end();
		return false;
	}
	const Image b = sb.readBackFloat();
	int kind = 0, v = 0;
	const bool flashed = sb.plugin->ContentForTest( first, kind, v ) && kind == C_FLASH && sb.plugin->ContentForTest( second, kind, v )
	                     && kind == C_FLASH && sb.plugin->ContentForTest( first + 1, kind, v ) && kind != C_FLASH;
	sb.end();
	if( !flashed )
		return false;
	g.flashGap = static_cast< int >( second - first );

	Settings holesLight = common;
	holesLight.push_back( { "Holes", "Light" } );
	Run a;
	if( !render( true, width, height, holesLight, perturb, frames, [ & ]( int ) { return dark; }, a ) )
		return false;

	const size_t n = static_cast< size_t >( width ) * height;
	std::vector< double > hole( n ), picture( n );
	for( size_t i = 0; i < n; ++i )
	{
		const double r1 = a.image[ i * 4 ];
		const double r2 = b[ i * 4 ];
		hole[ i ]       = std::clamp( ( r1 - kBaseRed ) / ( 1.0 - kBaseRed ), 0.0, 1.0 );
		picture[ i ]    = std::clamp( ( r2 - kBaseRed * ( 1.0 - hole[ i ] ) ) / ( 1.0 - kBaseRed ), 0.0, 1.0 );
	}

	//The film's width: alpha down the middle column. The holes are Light, so
	//the film is opaque from edge to edge and the surround is clear.
	std::vector< double > alpha( static_cast< size_t >( height ) );
	for( int y = 0; y < height; ++y )
		alpha[ static_cast< size_t >( y ) ] = pixelAt( a.image, width, width / 2, y )[ 3 ];
	g.film    = extentOf( alpha );
	g.mmPerPx = f.width / g.film.length;

	//A row and a column through each blob's middle (the middle of its box),
	//over the box and two pixels more: across is the output's y here, along
	//its x.
	auto measure = [ & ]( const std::vector< double >& field, std::vector< Extent >& across, std::vector< Extent >& along ) {
		std::vector< Blob > found = blobs( field, width, height, 0.5 );
		std::sort( found.begin(), found.end(), []( const Blob& p, const Blob& q ) { return p.x0 < q.x0; } );
		for( const Blob& bl : found )
		{
			if( bl.touchesBorder )
				continue;
			const int x0 = std::max( 0, bl.x0 - 2 ), x1 = std::min( width - 1, bl.x1 + 2 );
			const int y0 = std::max( 0, bl.y0 - 2 ), y1 = std::min( height - 1, bl.y1 + 2 );
			const int cx = ( bl.x0 + bl.x1 ) / 2, cy = ( bl.y0 + bl.y1 ) / 2;
			std::vector< double > col( static_cast< size_t >( height ), 0.0 ), row( static_cast< size_t >( width ), 0.0 );
			for( int y = y0; y <= y1; ++y )
				col[ static_cast< size_t >( y ) ] = field[ static_cast< size_t >( y ) * width + cx ];
			for( int x = x0; x <= x1; ++x )
				row[ static_cast< size_t >( x ) ] = field[ static_cast< size_t >( cy ) * width + x ];
			across.push_back( extentOf( col, y0, y1 + 1 ) );
			along.push_back( extentOf( row, x0, x1 + 1 ) );
		}
	};
	measure( hole, g.holeAcross, g.holeAlong );
	measure( picture, g.picAcross, g.picAlong );

	bool solid = g.film.solid;
	int which  = 0;
	for( const auto* list : { &g.holeAcross, &g.holeAlong, &g.picAcross, &g.picAlong } )
	{
		for( const Extent& e : *list )
		{
			if( !e.solid && std::getenv( "GU_DEBUG" ) )
				std::printf( "  not solid: list %d, length %.4f centre %.4f\n", which, e.length, e.centre );
			solid = solid && e.solid;
		}
		++which;
	}
	g.ok = solid && g.holeAcross.size() >= 2 && g.picAcross.size() == 2;
	if( std::getenv( "GU_DEBUG" ) )
		std::printf( "  %s: %d frames, flashes %lld and %lld, film solid %d, %zu holes, %zu pictures, all solid %d\n", f.name, frames,
		             static_cast< long long >( first ), static_cast< long long >( second ), g.film.solid ? 1 : 0,
		             g.holeAcross.size(), g.picAcross.size(), solid ? 1 : 0 );
	return true;
}

//---------------------------------------------------------------------------
// --perfs
//
// Per format: a moving strip at Angle 0, measured (see measureGeometry).
// Every hole's extent across and along through its middle, its distance from
// its own edge, the pitch between neighbours in a row, the holes in one
// frame's length, and where a hole sits past the frame line -- in
// millimetres, the scale from the film's own width in the same picture. Each
// is held to the model (Strip.cpp) at the measurement's own tolerance, and to
// the published figure at kPublishedTol more.
//---------------------------------------------------------------------------
int runPerfs( int width, int height, int perturb = 0, bool quiet = false )
{
	int failures = 0;
	for( const Published& p : published() )
	{
		const strip::Format& f = strip::FormatAt( p.format );
		Geometry g;
		if( !measureGeometry( p.format, width, height, perturb, g ) || !g.ok )
		{
			if( !quiet )
				std::printf( "perfs %-14s: nothing to measure  FAILED\n", f.name );
			++failures;
			continue;
		}
		const double s    = g.mmPerPx;
		const double edge = kEdgeTol * s;//one edge's worth, in mm
		bool ok           = true;

		double across = 0.0, along = 0.0, toEdgeNear = 0.0;
		double worstPub = 0.0, worstModel = 0.0;
		std::vector< double > nearAlong;
		int far = 0;
		for( size_t i = 0; i < g.holeAcross.size(); ++i )
		{
			across              = g.holeAcross[ i ].length * s;
			along               = g.holeAlong[ i ].length * s;
			const double xc     = filmX( f, g, g.holeAcross[ i ].centre );
			const bool nearRow  = xc < 0.5 * f.width;
			const double toEdge = nearRow ? xc - 0.5 * across : f.width - ( xc + 0.5 * across );
			//A length is two edges; an edge distance is a hole's centre and
			//length against the film's centre and length: four.
			worstModel = std::max( { worstModel, std::fabs( across - f.hole.across ) / ( 2.0 * edge ),
			                         std::fabs( along - f.hole.along ) / ( 2.0 * edge ),
			                         std::fabs( toEdge - f.holeEdge ) / ( 4.0 * edge ) } );
			worstPub = std::max( { worstPub, std::fabs( across - p.holeAcross ) - 2.0 * edge, std::fabs( along - p.holeAlong ) - 2.0 * edge,
			                       std::fabs( toEdge - p.holeEdge ) - 4.0 * edge } );
			if( nearRow )
			{
				nearAlong.push_back( g.holeAlong[ i ].centre );
				toEdgeNear = toEdge;
			}
			else
				++far;
		}
		ok = ok && worstModel <= 1.0 && worstPub <= kPublishedTol && ( far > 0 ) == p.holesFar && nearAlong.size() >= 2;

		std::sort( nearAlong.begin(), nearAlong.end() );
		double pitchSum = 0.0;
		for( size_t i = 1; i < nearAlong.size(); ++i )
		{
			const double pitch = ( nearAlong[ i ] - nearAlong[ i - 1 ] ) * s;
			pitchSum += pitch;
			ok = ok && within( pitch, f.holePitch, 2.0 * edge ) && within( pitch, p.holePitch, kPublishedTol + 2.0 * edge );
		}
		const double holePitch = nearAlong.size() >= 2 ? pitchSum / static_cast< double >( nearAlong.size() - 1 ) : 1.0;

		//The frame: two flashed pictures flashGap frames apart; the frame line
		//half a frame past the first one's middle.
		const double c0         = g.picAlong[ 0 ].centre, c1 = g.picAlong[ 1 ].centre;
		const double framePitch = ( c1 - c0 ) * s / g.flashGap;
		const int perFrame      = static_cast< int >( std::lround( framePitch / holePitch ) );
		const double line       = c0 + 0.5 * ( c1 - c0 ) / g.flashGap;
		double offset           = 1e9;
		for( double h : nearAlong )
		{
			double o = ( h - line ) * s / holePitch;
			o -= std::floor( o );
			offset = std::min( { offset, std::fabs( o - p.lineOffset ), std::fabs( o - p.lineOffset - 1.0 ),
			                     std::fabs( o - p.lineOffset + 1.0 ) } );
		}
		//Three centres (two pictures and a hole), each two edges, over a pitch.
		const double offsetTol = 6.0 * edge / holePitch;
		ok = ok && perFrame == p.holesPerFrame && within( framePitch, f.pitch, 4.0 * edge / g.flashGap ) && offset <= offsetTol;

		if( !quiet )
			std::printf( "perfs %-14s: %2zu holes %.4f across x %.4f along, %.4f from the edge, %.4f apart, %d to a %.4f mm frame, "
			             "%.1e of a pitch from where the frame line puts them; %.1e mm past the standard  %s\n",
			             f.name, g.holeAcross.size(), across, along, toEdgeNear, holePitch, perFrame, framePitch, offset,
			             std::max( worstPub, 0.0 ), verdict( ok ) );
		if( !ok )
			++failures;
	}
	if( !quiet )
		std::printf( "%s\n", failures == 0 ? "perfs: every format's holes are where the standards put them" : "perfs: FAILURES" );
	return failures;
}

//---------------------------------------------------------------------------
// --aperture
//
// The same renders: each flashed picture's width across, height along and
// its centre's distance from the reference edge, against the model and the
// published apertures; and the frame line -- the frame pitch less the
// picture's height, so Academy's thick bar and Scope's hairline are the same
// subtraction.
//---------------------------------------------------------------------------
int runAperture( int width, int height, int perturb = 0, bool quiet = false )
{
	int failures = 0;
	for( const Published& p : published() )
	{
		const strip::Format& f = strip::FormatAt( p.format );
		Geometry g;
		if( !measureGeometry( p.format, width, height, perturb, g ) || !g.ok )
		{
			if( !quiet )
				std::printf( "aperture %-14s: nothing to measure  FAILED\n", f.name );
			++failures;
			continue;
		}
		const double s    = g.mmPerPx;
		const double edge = kEdgeTol * s;
		bool ok           = true;
		double across = 0.0, along = 0.0, centre = 0.0, worstPub = 0.0;
		for( size_t i = 0; i < g.picAcross.size(); ++i )
		{
			across = g.picAcross[ i ].length * s;
			along  = g.picAlong[ i ].length * s;
			centre = filmX( f, g, g.picAcross[ i ].centre );
			ok = ok && within( across, f.ImageWidth(), 2.0 * edge ) && within( along, f.imageHeight, 2.0 * edge )
			     && within( centre, 0.5 * ( f.imageX0 + f.imageX1 ), 4.0 * edge );
			worstPub = std::max( { worstPub, std::fabs( across - p.picAcross ) - 2.0 * edge, std::fabs( along - p.picAlong ) - 2.0 * edge,
			                       std::fabs( centre - p.picCentre ) - 4.0 * edge } );
		}
		ok = ok && worstPub <= kPublishedTol;
		if( !quiet )
			std::printf( "aperture %-14s: %.4f across x %.4f along, centred %.4f from the reference edge, frame line %.4f mm; "
			             "%.1e mm past the standard  %s\n",
			             f.name, across, along, centre, f.pitch - along, std::max( worstPub, 0.0 ), verdict( ok ) );
		if( !ok )
			++failures;
	}
	if( !quiet )
		std::printf( "%s\n", failures == 0 ? "aperture: every picture is the size and in the place the standards give"
		                                    : "aperture: FAILURES" );
	return failures;
}

//---------------------------------------------------------------------------
// --squeeze
//
// A round disc, 0.3 of the clip's height in radius, on a field at the
// rebate's red, Fit Crop, in a Cell framed wide enough to hold the whole
// picture. Its second moments on the output: a uniform ellipse with semi-axes
// a and b has sigma_x^2 = a^2 / 4 and sigma_y^2 = b^2 / 4, so sigma_x /
// sigma_y is its width over its height as it lies on the film -- 1 / squeeze
// for a disc that is round as it is meant to be seen. Blur adds the same
// variance to each axis, at most Bx and By (see below), so 1 / squeeze must
// lie in [ sqrt( ( sx^2 - Bx ) / sy^2 ), sqrt( sx^2 / ( sy^2 - By ) ) ].
//---------------------------------------------------------------------------
int runSqueeze( int width, int height, int perturb = 0, bool quiet = false )
{
	struct Case
	{
		int format;
		const char* squeeze;
		double factor;
	};
	const Case cases[] = {
		{ strip::kScope, "Format", 2.0 }, { strip::kAcademy, "Format", 1.0 }, { strip::kAcademy, "1.33x", 4.0 / 3.0 },
		{ strip::kAcademy, "1.5x", 1.5 }, { strip::kSuper16, "2x", 2.0 },     { strip::kStill, "2x", 2.0 },
		{ strip::kSuper8, "1.5x", 1.5 },
	};
	int failures = 0;
	for( const Case& c : cases )
	{
		const strip::Format& f  = strip::FormatAt( c.format );
		const Settings settings = { { "Format", f.name },   { "Squeeze", c.squeeze }, { "Mode", "Cell" },
			                        { "Framing", "1" },     { "Frames", "Live" },     { "Holes", "Black" },
			                        { "Soundtracks", "0" }, { "Edge Print", "0" },    { "Fit", "Crop" } };

		//The window of the clip the picture shows (Fit Crop), so the disc can be
		//made to fit inside it: 0.35 of its shorter side in radius.
		const double picW     = f.imageAcross ? f.imageHeight : f.ImageWidth();
		const double picH     = f.imageAcross ? f.ImageWidth() : f.imageHeight;
		const double display  = picW / picH * c.factor;
		const double inAspect = static_cast< double >( width ) / height;
		const double winW     = inAspect > display ? display / inAspect : 1.0;
		const double winH     = inAspect > display ? 1.0 : inAspect / display;
		const double radius   = 0.35 * std::min( winW * width, winH * height ) / height;

		Image disc = buildDisc( width, height, radius );
		for( size_t i = 0; i < disc.size(); i += 4 )
			for( size_t k = 0; k < 3; ++k )
				disc[ i + k ] = kBaseRed + ( 1.0f - kBaseRed ) * disc[ i + k ];
		Run r;
		if( !render( true, width, height, settings, perturb, 3, [ & ]( int ) { return disc; }, r ) )
			return failures + 1;

		//Only the frame in the gate: its neighbours, live too, show the disc as
		//well. Its picture's box on the output, from the camera (placement).
		double bx0 = 1e9, by0 = 1e9, bx1 = -1e9, by1 = -1e9;
		const double y0 = static_cast< double >( r.state.gate - r.state.camera.base ) * f.pitch;
		for( double y : { y0 + f.ImageY0(), y0 + f.ImageY1() } )
			for( double x : { f.imageX0, f.imageX1 } )
			{
				double px = 0.0, py = 0.0;
				screenOf( r.state.camera, x, y, px, py );
				bx0 = std::min( bx0, px );
				bx1 = std::max( bx1, px );
				by0 = std::min( by0, py );
				by1 = std::max( by1, py );
			}
		auto weight = [ & ]( int x, int y ) {
			if( x + 0.5 < bx0 || x + 0.5 > bx1 || y + 0.5 < by0 || y + 0.5 > by1 )
				return 0.0;
			return std::clamp( ( pixelAt( r.image, width, x, y )[ 0 ] - kBaseRed ) / ( 1.0 - kBaseRed ), 0.0, 1.0 );
		};
		double sum = 0.0, sx = 0.0, sy = 0.0;
		for( int y = 0; y < height; ++y )
			for( int x = 0; x < width; ++x )
			{
				const double v = weight( x, y );
				sum += v;
				sx += v * ( x + 0.5 );
				sy += v * ( y + 0.5 );
			}
		const double mx = sx / sum, my = sy / sum;
		double vx = 0.0, vy = 0.0;
		for( int y = 0; y < height; ++y )
			for( int x = 0; x < width; ++x )
			{
				const double v = weight( x, y );
				vx += v * ( x + 0.5 - mx ) * ( x + 0.5 - mx );
				vy += v * ( y + 0.5 - my ) * ( y + 0.5 - my );
			}
		vx /= sum;
		vy /= sum;

		//Blur, per axis, in output pixels squared: a box of an output pixel
		//where the clip is sampled over the pixel's footprint, and another for
		//the pixel itself (1/12 each); and, in input texels (one is tx output
		//pixels across and ty down), the disc's own area-weighted edge (a box,
		//1/12) and the bilinear tap (a tent two texels wide, 1/6).
		const double s  = r.state.camera.mmPerPixel;
		const double tx = picW / s / ( winW * width );
		const double ty = picH / s / ( winH * height );
		//And the GPU: its bilinear weights come in 1/256 steps (fiche's trap),
		//so every pixel on the disc's edge may be off by half a step, and at
		//worst all one way. Each such pixel moves a variance by at most its
		//error times ( r^2 - sigma^2 ) over the total, with r^2 <= 4 sigma^2
		//on the edge: E <= perimeter x 1/512 x 3 sigma^2 / sum, on either axis.
		const double a  = 2.0 * std::sqrt( vx ), b = 2.0 * std::sqrt( vy );
		const double perimeter = kPi * ( 3.0 * ( a + b ) - std::sqrt( ( 3.0 * a + b ) * ( a + 3.0 * b ) ) );
		const double E  = perimeter / 512.0 * 3.0 * std::max( vx, vy ) / sum;
		const double Bx = 2.0 / 12.0 + 3.0 / 12.0 * tx * tx + E;
		const double By = 2.0 / 12.0 + 3.0 / 12.0 * ty * ty + E;
		const double lo = std::sqrt( ( vx - Bx ) / ( vy + E ) );
		const double hi = std::sqrt( ( vx + E ) / ( vy - By ) );
		const double want  = 1.0 / c.factor;
		const bool ok      = sum > 100.0 && want >= lo && want <= hi;
		if( std::getenv( "GU_DEBUG" ) )
			std::printf( "  vx %.4f vy %.4f Bx %.4f By %.4f tx %.4f ty %.4f sum %.2f centre (%.3f, %.3f) box [%.1f %.1f]x[%.1f %.1f]\n", vx, vy,
			             Bx, By, tx, ty, sum, mx, my, bx0, bx1, by0, by1 );
		if( !quiet )
			std::printf( "squeeze %-14s %-6s: width over height on the film %.5f, 1/squeeze %.5f, band [%.5f, %.5f]  %s\n", f.name,
			             c.squeeze, std::sqrt( vx / vy ), want, lo, hi, verdict( ok ) );
		if( !ok )
			++failures;
	}
	if( !quiet )
		std::printf( "%s\n", failures == 0 ? "squeeze: a round disc is 1/squeeze as wide as it is tall on the film"
		                                    : "squeeze: FAILURES" );
	return failures;
}
