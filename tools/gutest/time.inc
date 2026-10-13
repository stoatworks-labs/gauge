//---------------------------------------------------------------------------
// The checks in time: --held, --leader, --flash, --resize. Included after
// geometry.inc.
//---------------------------------------------------------------------------

/// The held-frames checks feed the effect a flat field whose level names the
/// host frame: frame f is 0.1 + 0.8 ( f mod 97 ) / 97. A picture kept in the
/// RGBA8 ring comes back as that level rounded to 1/255; a live one exactly.
float levelOf( int frame )
{
	return 0.1f + 0.8f * static_cast< float >( frame % 97 ) / 97.0f;
}

/// Which host frame a level read back names, or -1. Neighbouring levels are
/// 0.8 / 97 = 2.1 / 255 apart and RGBA8 rounds by at most half of 1 / 255.
int frameOfLevel( double level, int frames )
{
	int best       = -1;
	double bestErr = 1.0;
	for( int f = 0; f < std::min( frames, 97 ); ++f )
	{
		const double e = std::fabs( level - levelOf( f ) );
		if( e < bestErr )
		{
			bestErr = e;
			best    = f;
		}
	}
	return bestErr <= 0.5 / 255.0 + 1e-6 ? best : -1;
}

/// The level at the middle of film frame `index`'s picture, or -1 if that is
/// off the output.
double centreLevel( const Image& image, int width, int height, const GaugePlugin::State& st, const strip::Format& f,
                    int64_t index )
{
	double px = 0.0, py = 0.0;
	const double y = static_cast< double >( index - st.camera.base ) * f.pitch + 0.5 * ( f.ImageY0() + f.ImageY1() );
	screenOf( st.camera, 0.5 * ( f.imageX0 + f.imageX1 ), y, px, py );
	const int ix = static_cast< int >( std::floor( px ) ), iy = static_cast< int >( std::floor( py ) );
	if( ix < 1 || iy < 1 || ix >= width - 1 || iy >= height - 1 )
		return -1.0;
	return pixelAt( image, width, ix, iy )[ 0 ];
}

/// The left edge of film frame `index`'s picture on the middle row, in pixels,
/// from the coverage of the pixel it falls in: that pixel is base ( 1 - c ) + L c
/// where L is the picture's own level two pixels in, so c and the edge are
/// exact whatever L is. -1 if it is not where the camera says.
double leftEdge( const Image& image, int width, int height, const GaugePlugin::State& st, const strip::Format& f, int64_t index )
{
	double px = 0.0, py = 0.0;
	screenOf( st.camera, 0.5 * ( f.imageX0 + f.imageX1 ), static_cast< double >( index - st.camera.base ) * f.pitch + f.ImageY0(),
	          px, py );
	const int row = height / 2;
	for( int x = std::max( 1, static_cast< int >( px ) - 2 ); x <= std::min( width - 4, static_cast< int >( px ) + 2 ); ++x )
	{
		const double v0 = pixelAt( image, width, x - 1, row )[ 0 ];
		const double v1 = pixelAt( image, width, x, row )[ 0 ];
		const double L  = pixelAt( image, width, x + 2, row )[ 0 ];
		if( std::fabs( v0 - kBaseRed ) < 1e-6 && v1 > kBaseRed + 1e-6 )
			return x + 1.0 - ( v1 - kBaseRed ) / ( L - kBaseRed );
	}
	return -1.0;
}

//---------------------------------------------------------------------------
// --held
//
// Strip: Academy at 60 Hz, running at 6 frames a second with a closed
// shutter, the input changing every host frame. Every frame in view must hold
// the level of the host frame on which it first came into view (the harness
// records, frame by frame, which film frames the plugin had in view) however
// far it has since travelled. And the strip moves at Travel: one picture's
// left edge, measured from its coverage at host frames 40 and 50, moves
// 6 x 1/6 s x 19 mm toward the head.
//
// Cell: racked half a frame, so the frame line sits across the middle of the
// output and the bottom of the frame above the gate is in view. That frame
// must hold the input of the last host frame before the gate's pull-down.
//---------------------------------------------------------------------------
int runHeld( int width, int height, int perturb = 0, bool quiet = false )
{
	int failures           = 0;
	const strip::Format& f = strip::FormatAt( strip::kAcademy );
	char travel[ 32 ];
	std::snprintf( travel, sizeof( travel ), "%.9g", controls::TravelParam( 6.0 ) );
	const Settings stripSettings = { { "Mode", "Strip" }, { "Travel", travel },  { "Shutter", "0" },    { "Frames", "Held" },
		                             { "Size", "1" },     { "Soundtracks", "0" }, { "Edge Print", "0" }, { "Holes", "Black" } };

	const int frames = 90;
	std::map< int64_t, int > firstSeen;
	double edge40 = -1.0, edge50 = -1.0;
	int64_t tracked = INT64_MIN;
	Image last;
	GaugePlugin::State lastState;
	{
		Session session( true );
		applyAll( *session.plugin, stripSettings );
		session.plugin->SetPerturbForTest( perturb );
		if( !session.begin( width, height ) )
			return 1;
		for( int frame = 0; frame < frames; ++frame )
		{
			session.render( frame, buildFlat( width, height, levelOf( frame ) ) );
			const GaugePlugin::State& st = session.plugin->StateForTest();
			for( int64_t i = st.visibleLo; i <= st.visibleHi; ++i )
				if( !firstSeen.count( i ) )
					firstSeen[ i ] = frame;
			if( frame == 40 || frame == 50 )
			{
				const Image img = session.readBackFloat();
				//The frame whose picture is just right of the middle at frame 40.
				if( frame == 40 )
					for( int64_t i = st.visibleLo; i <= st.visibleHi && tracked == INT64_MIN; ++i )
					{
						double px = 0.0, py = 0.0;
						screenOf( st.camera, 0.5 * ( f.imageX0 + f.imageX1 ), static_cast< double >( i - st.camera.base ) * f.pitch + f.ImageY0(), px, py );
						if( px > 0.5 * width )
							tracked = i;
					}
				( frame == 40 ? edge40 : edge50 ) = leftEdge( img, width, height, st, f, tracked );
			}
		}
		last      = session.readBackFloat();
		lastState = session.plugin->StateForTest();
		session.end();
	}

	int checked = 0, wrong = 0;
	for( int64_t i = lastState.visibleLo; i <= lastState.visibleHi; ++i )
	{
		const double level = centreLevel( last, width, height, lastState, f, i );
		if( level < 0.0 || !firstSeen.count( i ) )
			continue;
		++checked;
		if( frameOfLevel( level, frames ) != firstSeen[ i ] )
			++wrong;
	}
	const double moved    = edge50 - edge40;
	const double expected = -6.0 * ( 10.0 / 60.0 ) * f.pitch / lastState.camera.mmPerPixel;
	const double moveTol  = 2.0 * kEdgeTol;
	const bool stripOk    = checked >= 3 && wrong == 0 && edge40 >= 0.0 && edge50 >= 0.0 && within( moved, expected, moveTol );
	if( !quiet )
		std::printf( "held strip: %d frames in view, each holding the input of the host frame it came into view on (%d wrong); "
		             "a picture moved %.4f px in 1/6 s against %.4f  %s\n",
		             checked, wrong, moved, expected, verdict( stripOk ) );
	failures += stripOk ? 0 : 1;

	//Cell, racked to the frame line.
	const Settings cell = { { "Mode", "Cell" }, { "Rack", "0" }, { "Frames", "Held" }, { "Framing", "0.7" },
		                    { "Soundtracks", "0" }, { "Edge Print", "0" } };
	Run c;
	if( !render( true, width, height, cell, perturb, 61, [ & ]( int fr ) { return buildFlat( width, height, levelOf( fr ) ); }, c ) )
		return failures + 1;
	//The film frame above the gate was in the gate on every host frame f with
	//floor( 24 f / 60 ) = gate - 1; the last of them is ceil( 2.5 gate ) - 1.
	const int64_t gate = c.state.gate;
	const int lastIn   = static_cast< int >( std::ceil( 2.5 * static_cast< double >( gate ) - 1e-9 ) ) - 1;
	double px = 0.0, py = 0.0;
	screenOf( c.state.camera, 0.5 * ( f.imageX0 + f.imageX1 ),
	          static_cast< double >( gate - 1 - c.state.camera.base ) * f.pitch + f.ImageY1() - 1.0, px, py );
	const int ix = static_cast< int >( px ), iy = static_cast< int >( py );
	const double above = ( ix >= 0 && iy >= 0 && ix < width && iy < height ) ? pixelAt( c.image, width, ix, iy )[ 0 ] : -1.0;
	const int named    = frameOfLevel( above, 61 );
	const bool cellOk  = named == lastIn;
	if( !quiet )
		std::printf( "held cell: the frame above the gate (film frame %lld) holds host frame %d's input; the last before its "
		             "pull-down was %d  %s\n",
		             static_cast< long long >( gate - 1 ), named, lastIn, verdict( cellOk ) );
	failures += cellOk ? 0 : 1;

	if( !quiet )
		std::printf( "%s\n", failures == 0 ? "held: every frame keeps the picture it was exposed with" : "held: FAILURES" );
	return failures;
}

//---------------------------------------------------------------------------
// The leader, read frame by frame out of the gate's picture.
//
// Display units: the picture's height is 1, its centre is 0. The sweep is read
// in the annulus 0.39..0.47 -- outside the rings, inside the frame -- with the
// crosshair's two bands left out: the fraction of it darker than halfway
// between the leader's two greys. The rings are read at r = 0.36. The number is
// read by template: the leader's ink against each candidate digit drawn from
// the face's own atlas, inside the inner ring, the crosshair and the hand left
// out of both.
//---------------------------------------------------------------------------
struct LeaderReading
{
	double mean   = 0.0;///< the whole picture's red
	double ring   = 0.0;///< mean at r = 0.36
	double swept  = 0.0;///< fraction of the annulus that is dark
	double sweptWant = 0.0;///< the fraction of the same pixels whose centres lie behind the hand
	double sweptTol = 0.0;
	int digit     = -1;
	double digitScore = 0.0, runnerUp = 0.0;
};

LeaderReading readLeader( const Image& image, int width, int height, const GaugePlugin::State& st, const strip::Format& f,
                          double displayAspect, int expectK )
{
	LeaderReading out;
	const camera::Camera& cam = st.camera;
	const double picW = f.ImageWidth(), picH = f.imageHeight;
	const double fw   = cam.mmPerPixel / picH;//a pixel, in display units
	const double y0   = static_cast< double >( st.gate - cam.base ) * f.pitch + f.ImageY0();
	const lettering::Atlas& atlas = lettering::BuiltinAtlas();

	double sum = 0.0, ringSum = 0.0, annulus = 0.0, dark = 0.0, behind = 0.0;
	int n = 0, ringN = 0;
	struct Px
	{
		double px, py, ink;
	};
	std::vector< Px > inner;
	const double A = 2.0 * kPi * expectK / Leader::kCount;
	for( int y = 0; y < height; ++y )
		for( int x = 0; x < width; ++x )
		{
			double fx = 0.0, fy = 0.0;
			cam.Film( x + 0.5, y + 0.5, fx, fy );
			const double u = ( fx - f.imageX0 ) / picW, v = ( fy - y0 ) / picH;
			//A pixel's width in from the picture's edge, so no pixel read is
			//partly rebate.
			const double mu = cam.mmPerPixel / picW, mv = cam.mmPerPixel / picH;
			if( u < mu || u > 1.0 - mu || v < mv || v > 1.0 - mv )
				continue;
			const double level = pixelAt( image, width, x, y )[ 0 ];
			sum += level;
			++n;
			const double pxd = ( u - 0.5 ) * displayAspect, pyd = v - 0.5;
			const double r   = std::sqrt( pxd * pxd + pyd * pyd );
			if( std::fabs( r - 0.36 ) < 0.004 )
			{
				ringSum += level;
				++ringN;
			}
			const bool offCross = std::fabs( pxd ) > 0.03 && std::fabs( pyd ) > 0.03;
			double a = std::atan2( pxd, -pyd );
			if( a < 0.0 )
				a += 2.0 * kPi;
			if( r > 0.39 && r < 0.47 && offCross )
			{
				annulus += 1.0;
				dark += level < 0.5 * ( 0.62 + 0.30 ) ? 1.0 : 0.0;
				behind += expectK > 0 && a < A ? 1.0 : 0.0;
			}
			if( r < 0.28 && std::fabs( pxd ) > 0.004 + fw && std::fabs( pyd ) > 0.004 + fw )
			{
				//Leave out the hand and the swept wedge: the number is read on
				//the light ground, ink = 1 - level / 0.62.
				if( expectK > 0 && a < A + ( 0.004 + 2.0 * fw ) / std::max( r, 1e-3 ) )
					continue;
				inner.push_back( { pxd, pyd, std::clamp( 1.0 - level / 0.62, 0.0, 1.0 ) } );
			}
		}
	out.mean  = n > 0 ? sum / n : 0.0;
	out.ring  = ringN > 0 ? ringSum / ringN : 0.0;
	out.swept     = annulus > 0.0 ? dark / annulus : 0.0;
	out.sweptWant = annulus > 0.0 ? behind / annulus : 0.0;
	//The hand's black line across the annulus, and half a pixel either side of
	//each of the sweep's two radial edges, misread at worst.
	out.sweptTol = 0.08 * ( 2.0 * 0.004 + 2.0 * fw ) / ( annulus * fw * fw * 1.0 ) + 1e-9;

	//The digit, by template.
	const float H = Leader::kDigitHeight, bold = Leader::kDigitBold;
	double best = -1.0, second = -1.0;
	for( int g = 2; g <= 8; ++g )
	{
		const auto& glyph = atlas.glyph[ g ];
		const double x0   = -0.5 * ( glyph.inkX0 + glyph.inkX1 ) * H;
		const double y1   = 0.5 * H;
		double agree = 0.0;
		for( const Px& p : inner )
		{
			const double lx = ( p.px - x0 ) / H, ly = ( y1 - p.py ) / H;
			const double d  = atlas.Distance( g, static_cast< float >( lx ), static_cast< float >( ly ) ) + bold;
			const double t  = std::clamp( 0.5 + d * H / fw, 0.0, 1.0 );
			agree += ( t > 0.5 ) == ( p.ink > 0.5 ) ? 1.0 : 0.0;
		}
		agree /= std::max< size_t >( inner.size(), 1 );
		if( agree > best )
		{
			second    = best;
			best      = agree;
			out.digit = g;
		}
		else if( agree > second )
			second = agree;
	}
	out.digitScore = best;
	out.runnerUp   = second;
	return out;
}

//---------------------------------------------------------------------------
// --leader
//
// The effect in Cell at 24 fps with the host at 24 Hz, so a host frame is a
// film frame. Roll Leader at host frame 10 splices PICTURE START onto the next
// frame, 11, and every host frame from there is checked against the leader's
// own definition: PICTURE START (no rings); counts 8 to 3 a second each (rings,
// the sweep's swept fraction ( j mod 24 ) / 24, the number by template); the 2
// for one frame; black to frame 191; the clip from frame 192, exactly eight
// seconds after PICTURE START.
//
// Then on the beat: Rate Beat at 125 bpm with the host at 50 Hz (one film
// frame a host frame), Start On Bar, Roll Leader pressed in the middle of the
// second bar. PICTURE START must wait for the third bar's downbeat and the
// clip must come back on the fifth's, two bars later.
//---------------------------------------------------------------------------
int runLeader( int width, int height, int perturb = 0, bool quiet = false )
{
	int failures           = 0;
	const strip::Format& f = strip::FormatAt( strip::kAcademy );
	const double aspect    = f.ImageWidth() / f.imageHeight;
	const float clip       = 0.7f;
	const Settings settings = { { "Mode", "Cell" },    { "Rate", "24 fps" },  { "Frames", "Held" },  { "Soundtracks", "0" },
		                        { "Edge Print", "0" }, { "Start On", "Press" }, { "Framing", "0.7" } };
	const int press = 10, start = press + 1, frames = start + Leader::kPicture + 6;

	Session session( true );
	session.fps = 24.0;
	applyAll( *session.plugin, settings );
	session.plugin->SetPerturbForTest( perturb );
	if( !session.begin( width, height ) )
		return 1;
	const unsigned int roll = static_cast< unsigned int >( session.plugin->IndexOf( K_ROLL_LEADER ) );
	int wrong = 0, read = 0;
	std::string firstWrong;
	double worstSwept = 0.0;
	for( int frame = 0; frame < frames; ++frame )
	{
		session.plugin->SetFloatParameter( roll, frame == press ? 1.0f : 0.0f );
		session.render( frame, buildFlat( width, height, clip ) );
		const Image img              = session.readBackFloat();
		const GaugePlugin::State& st = session.plugin->StateForTest();
		const int j                  = frame - start;
		const int k                  = j >= 1 && j < Leader::kTwo ? j % Leader::kCount : 0;
		const LeaderReading rd       = readLeader( img, width, height, st, f, aspect, k );
		bool ok;
		char what[ 160 ];
		if( j < 0 || j >= Leader::kPicture )
		{
			ok = std::fabs( rd.mean - clip ) < 1e-6;
			std::snprintf( what, sizeof( what ), "clip, mean %.6f", rd.mean );
		}
		else if( j == 0 )
		{
			ok = rd.ring < 0.85;
			std::snprintf( what, sizeof( what ), "PICTURE START, ring %.3f", rd.ring );
		}
		else if( j <= Leader::kTwo )
		{
			const int number = Leader::NumberAt( j );
			//The swept fraction of the annulus's pixels is k / 24 of the circle
			//less the crosshair's bands, which do not fall evenly: the pixels'
			//own centres say what it should be.
			const double want = rd.sweptWant;
			worstSwept = std::max( worstSwept, std::fabs( rd.swept - want ) );
			ok = rd.ring > 0.85 && std::fabs( rd.swept - want ) <= rd.sweptTol;
			//The number, read once a count: on its first frame (the 8's second,
			//PICTURE START having its first) and on the 2.
			if( k == 0 || ( number == 8 && j == 1 ) )
				ok = ok && rd.digit == number && rd.digitScore > 0.97 && rd.runnerUp < rd.digitScore - 0.05;
			std::snprintf( what, sizeof( what ), "%d, swept %.4f of %.4f, ring %.3f, read %d (%.3f, next %.3f)", number, rd.swept,
			               want, rd.ring, rd.digit, rd.digitScore, rd.runnerUp );
		}
		else
		{
			ok = std::fabs( rd.mean - 0.012 ) < 1e-6;
			std::snprintf( what, sizeof( what ), "black, mean %.6f", rd.mean );
		}
		++read;
		if( !ok )
		{
			++wrong;
			if( firstWrong.empty() )
				firstWrong = "host frame " + std::to_string( frame ) + " (leader " + std::to_string( j ) + "): " + what;
		}
	}
	session.end();
	const bool timed = wrong == 0;
	if( !quiet )
	{
		std::printf( "leader: %d host frames at 24 Hz, %d wrong; the sweep within %.4f of ( j mod 24 ) / 24 of the circle everywhere  %s\n", read, wrong,
		             worstSwept, verdict( timed ) );
		if( !timed )
			std::printf( "  first wrong: %s\n", firstWrong.c_str() );
	}
	failures += timed ? 0 : 1;

	//On the beat.
	const Settings beat = { { "Mode", "Cell" },    { "Rate", "Beat" },  { "Frames", "Live" }, { "Soundtracks", "0" },
		                    { "Edge Print", "0" }, { "Start On", "Bar" } };
	Session b( true );
	b.fps = 50.0;
	b.bpm = 125.0;
	applyAll( *b.plugin, beat );
	b.plugin->SetPerturbForTest( perturb );
	if( !b.begin( width, height ) )
		return failures + 1;
	const int barFrames = 96;//four beats of 0.48 s at 50 Hz
	const int pressAt   = barFrames + 34;
	int pictureStart = -1, clipBack = -1;
	for( int frame = 0; frame < 5 * barFrames + 10; ++frame )
	{
		b.plugin->SetFloatParameter( roll, frame == pressAt ? 1.0f : 0.0f );
		b.render( frame, buildFlat( width, height, clip ) );
		const Image img              = b.readBackFloat();
		const GaugePlugin::State& st = b.plugin->StateForTest();
		const LeaderReading rd       = readLeader( img, width, height, st, f, aspect, 0 );
		const bool isClip            = std::fabs( rd.mean - clip ) < 1e-6;
		if( frame >= pressAt && !isClip && pictureStart < 0 )
			pictureStart = frame;
		if( pictureStart >= 0 && isClip && clipBack < 0 )
			clipBack = frame;
	}
	b.end();
	const bool onBar = pictureStart == 2 * barFrames && clipBack == 4 * barFrames;
	if( !quiet )
		std::printf( "leader on the beat: pressed at host frame %d, PICTURE START at %d (the third bar, %d), the clip back at %d "
		             "(the fifth, %d)  %s\n",
		             pressAt, pictureStart, 2 * barFrames, clipBack, 4 * barFrames, verdict( onBar ) );
	failures += onBar ? 0 : 1;

	if( !quiet )
		std::printf( "%s\n", failures == 0 ? "leader: PICTURE START, eight to two, black, and the picture at eight seconds or eight beats"
		                                    : "leader: FAILURES" );
	return failures;
}

//---------------------------------------------------------------------------
// --flash
//
// A sync flash is one film frame, fully exposed. Pressed at host frame 10 at
// 24 Hz, film frame 11 is white and frames 10 and 12 are the clip. At 60 Hz the
// flashed film frame is in the gate for every host frame with floor( 0.4 f )
// equal to it -- two or three of them -- and white on exactly those. Auto Flash
// Beat at 120 bpm flashes the frame each beat starts, every 12th film frame:
// over four seconds the white host frames are exactly those whose film frame is
// a multiple of 12.
//---------------------------------------------------------------------------
int runFlash( int width, int height, int perturb = 0, bool quiet = false )
{
	int failures           = 0;
	const strip::Format& f = strip::FormatAt( strip::kAcademy );
	const float clip       = 0.7f;
	const Settings settings = { { "Mode", "Cell" }, { "Rate", "24 fps" }, { "Frames", "Live" }, { "Soundtracks", "0" },
		                        { "Edge Print", "0" }, { "Start On", "Press" } };

	auto meanOf = [ & ]( Session& s ) {
		const Image img              = s.readBackFloat();
		const GaugePlugin::State& st = s.plugin->StateForTest();
		return centreLevel( img, width, height, st, f, st.gate );
	};

	//At 24 Hz.
	{
		Session s( true );
		s.fps = 24.0;
		applyAll( *s.plugin, settings );
		s.plugin->SetPerturbForTest( perturb );
		if( !s.begin( width, height ) )
			return 1;
		const unsigned int flash = static_cast< unsigned int >( s.plugin->IndexOf( K_FLASH ) );
		std::vector< int > white;
		for( int frame = 0; frame < 20; ++frame )
		{
			s.plugin->SetFloatParameter( flash, frame == 10 ? 1.0f : 0.0f );
			s.render( frame, buildFlat( width, height, clip ) );
			if( meanOf( s ) == 1.0 )
				white.push_back( frame );
		}
		s.end();
		const bool ok = white.size() == 1 && white[ 0 ] == 11;
		if( !quiet )
			std::printf( "flash at 24 Hz: pressed on host frame 10, white on %zu frame(s), the first %d  %s\n", white.size(),
			             white.empty() ? -1 : white[ 0 ], verdict( ok ) );
		failures += ok ? 0 : 1;
	}

	//At 60 Hz, pressed and on the beat.
	for( int pass = 0; pass < 2; ++pass )
	{
		Settings st = settings;
		if( pass == 1 )
			st.push_back( { "Auto Flash", "Beat" } );
		Session s( true );
		s.fps = 60.0;
		s.bpm = 120.0;
		applyAll( *s.plugin, st );
		s.plugin->SetPerturbForTest( perturb );
		if( !s.begin( width, height ) )
			return failures + 1;
		const unsigned int flash = static_cast< unsigned int >( s.plugin->IndexOf( K_FLASH ) );
		std::vector< int > white, want;
		const int frames = pass == 0 ? 30 : 240;
		int64_t flashed  = -1;
		for( int frame = 0; frame < frames; ++frame )
		{
			s.plugin->SetFloatParameter( flash, pass == 0 && frame == 10 ? 1.0f : 0.0f );
			s.render( frame, buildFlat( width, height, clip ) );
			if( pass == 0 && frame == 10 )
				flashed = s.plugin->StateForTest().gate + 1;
			if( meanOf( s ) == 1.0 )
				white.push_back( frame );
			const int64_t film = static_cast< int64_t >( std::floor( 0.4 * frame + 1e-9 ) );
			if( pass == 0 ? film == flashed : ( film > 0 && film % 12 == 0 ) )
				want.push_back( frame );
		}
		s.end();
		const bool ok = white == want && !want.empty();
		if( !quiet )
			std::printf( "flash at 60 Hz, %s: white on %zu host frames, predicted %zu, %s  %s\n",
			             pass == 0 ? "pressed" : "every beat at 120 bpm", white.size(), want.size(),
			             white == want ? "the same ones" : "NOT the same ones", verdict( ok ) );
		failures += ok ? 0 : 1;
	}

	if( !quiet )
		std::printf( "%s\n", failures == 0 ? "flash: a sync flash is exactly one film frame, wherever it lands" : "flash: FAILURES" );
	return failures;
}

//---------------------------------------------------------------------------
// --resize
//
// The photofinish trap: a moving strip's held frames must survive the host
// changing the raster. Forty frames of the --held strip at W x H, then the
// SAME instance handed 2W x 2H: every frame in view before and after must
// name the same host frame, and the ring must really have been reallocated
// (its cell size changes), or the check proves nothing.
//---------------------------------------------------------------------------
int runResize( int width, int height, int perturb = 0, bool quiet = false )
{
	const strip::Format& f = strip::FormatAt( strip::kAcademy );
	char travel[ 32 ];
	std::snprintf( travel, sizeof( travel ), "%.9g", controls::TravelParam( 6.0 ) );
	const Settings settings = { { "Mode", "Strip" }, { "Travel", travel },  { "Shutter", "0" },    { "Frames", "Held" },
		                        { "Size", "1" },     { "Soundtracks", "0" }, { "Edge Print", "0" }, { "Holes", "Black" } };
	Session s( true );
	applyAll( *s.plugin, settings );
	s.plugin->SetPerturbForTest( perturb );
	if( !s.begin( width, height ) )
		return 1;
	for( int frame = 0; frame < 40; ++frame )
		s.render( frame, buildFlat( width, height, levelOf( frame ) ) );
	const Image before                 = s.readBackFloat();
	const GaugePlugin::State stBefore  = s.plugin->StateForTest();
	s.resize( 2 * width, 2 * height );
	s.render( 40, buildFlat( 2 * width, 2 * height, levelOf( 40 ) ) );
	const Image after                 = s.readBackFloat();
	const GaugePlugin::State stAfter  = s.plugin->StateForTest();
	s.end();

	int compared = 0, differ = 0;
	for( int64_t i = std::max( stBefore.visibleLo, stAfter.visibleLo ); i <= std::min( stBefore.visibleHi, stAfter.visibleHi ); ++i )
	{
		const double a = centreLevel( before, width, height, stBefore, f, i );
		const double b = centreLevel( after, 2 * width, 2 * height, stAfter, f, i );
		if( a < 0.0 || b < 0.0 )
			continue;
		++compared;
		if( frameOfLevel( a, 41 ) != frameOfLevel( b, 41 ) || frameOfLevel( a, 41 ) < 0 )
			++differ;
	}
	const bool reallocated = stAfter.heldWidth != stBefore.heldWidth;
	const bool ok          = compared >= 2 && differ == 0 && reallocated;
	if( !quiet )
	{
		std::printf( "resize: held frames %dx%d -> %dx%d; %d frames compared across the resize, %d changed  %s\n", stBefore.heldWidth,
		             stBefore.heldHeight, stAfter.heldWidth, stAfter.heldHeight, compared, differ, verdict( ok ) );
		std::printf( "%s\n", ok ? "resize: a moving strip keeps its frames when the raster changes" : "resize: FAILURES" );
	}
	return ok ? 0 : 1;
}
