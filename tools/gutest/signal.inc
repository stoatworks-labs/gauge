//---------------------------------------------------------------------------
// The track and the alpha: --track, --alpha. Included after time.inc.
//---------------------------------------------------------------------------

/// One row's clear width across the analogue track, in mm, from the red of
/// the pixels wholly inside the track band: each is TrackDense + ( TrackClear -
/// TrackDense ) x lit, with lit the box coverage of the clear part. The clear
/// part never comes within 0.08 of the band's width of its edges, so the
/// pixels left out at the edges are dense.
struct TrackRows
{
	std::vector< double > y;    ///< film y of each row's centre, mm from the camera's base
	std::vector< double > width;///< clear width, mm
};

TrackRows readTrack( const Image& image, int width, int height, const camera::Camera& cam, const strip::Format& f )
{
	TrackRows rows;
	const double dense = 0.02, clear = 0.92;//Colour Print's track, red
	for( int py = 0; py < height; ++py )
	{
		double lit = 0.0;
		int inside = 0;
		for( int px = 0; px < width; ++px )
		{
			double fx = 0.0, fy = 0.0;
			cam.Film( px + 0.5, py + 0.5, fx, fy );
			//Wholly inside the band: the pixel's footprint is mmPerPixel wide.
			if( fx - 0.5 * cam.mmPerPixel < f.analogue.x0 || fx + 0.5 * cam.mmPerPixel > f.analogue.x1 )
				continue;
			++inside;
			lit += ( pixelAt( image, width, px, py )[ 0 ] - dense ) / ( clear - dense );
		}
		if( inside == 0 )
			continue;
		double ignore = 0.0, y = 0.0;
		cam.Film( 0.5, py + 0.5, ignore, y );
		rows.y.push_back( y );
		rows.width.push_back( lit * cam.mmPerPixel );
	}
	return rows;
}

/// Least squares: s( y ) = a sin( 2 pi nu y ) + b cos( 2 pi nu y ) + c. Returns
/// the amplitude and the residual's RMS.
void fitSine( const std::vector< double >& y, const std::vector< double >& s, double nu, double& amplitude, double& rms )
{
	double m[ 3 ][ 4 ] = {};
	for( size_t i = 0; i < y.size(); ++i )
	{
		const double basis[ 3 ] = { std::sin( 2.0 * kPi * nu * y[ i ] ), std::cos( 2.0 * kPi * nu * y[ i ] ), 1.0 };
		for( int r = 0; r < 3; ++r )
		{
			for( int c = 0; c < 3; ++c )
				m[ r ][ c ] += basis[ r ] * basis[ c ];
			m[ r ][ 3 ] += basis[ r ] * s[ i ];
		}
	}
	for( int col = 0; col < 3; ++col )
	{
		int pivot = col;
		for( int r = col + 1; r < 3; ++r )
			if( std::fabs( m[ r ][ col ] ) > std::fabs( m[ pivot ][ col ] ) )
				pivot = r;
		for( int c = 0; c < 4; ++c )
			std::swap( m[ col ][ c ], m[ pivot ][ c ] );
		for( int r = 0; r < 3; ++r )
		{
			if( r == col || m[ col ][ col ] == 0.0 )
				continue;
			const double k = m[ r ][ col ] / m[ col ][ col ];
			for( int c = 0; c < 4; ++c )
				m[ r ][ c ] -= k * m[ col ][ c ];
		}
	}
	const double a = m[ 0 ][ 3 ] / m[ 0 ][ 0 ], b = m[ 1 ][ 3 ] / m[ 1 ][ 1 ], c = m[ 2 ][ 3 ] / m[ 2 ][ 2 ];
	amplitude      = std::sqrt( a * a + b * b );
	double sum     = 0.0;
	for( size_t i = 0; i < y.size(); ++i )
	{
		const double model = a * std::sin( 2.0 * kPi * nu * y[ i ] ) + b * std::cos( 2.0 * kPi * nu * y[ i ] ) + c;
		sum += ( s[ i ] - model ) * ( s[ i ] - model );
	}
	rms = std::sqrt( sum / std::max< size_t >( y.size(), 1 ) );
}

//---------------------------------------------------------------------------
// --track
//
// The analogue track is variable area: its clear width is 0.84 x ( 1 + s ) / 2
// of the band, s being the sound. One FFT bin, k = 1, at magnitude 0.25 and
// Track Level 1, on Academy's mono track at 24 fps with every frame live, so
// the track runs on unbroken from frame to frame. Every row's clear width is
// read and s recovered, then fitted with a sine at the frequency the film's
// speed puts that bin at: ( k + 0.5 ) x 22050 / 64 Hz over 24 x 19 mm a second,
// 1.133 cycles a millimetre. The fit must find amplitude 0.25 and leave
// nothing else: a track drawn at the wrong speed is a sine at another
// frequency, which a sine at this one cannot fit.
//
// With no sound the track is a clear band of constant width (s = 0). And the
// leader's 2 carries the pop: 1 kHz, amplitude 0.8, on that one frame.
//---------------------------------------------------------------------------
int runTrack( int width, int height, int perturb = 0, bool quiet = false )
{
	int failures           = 0;
	const strip::Format& f = strip::FormatAt( strip::kAcademy );
	const double band      = f.analogue.x1 - f.analogue.x0;
	auto signalOf          = [ & ]( double clearWidth ) { return 2.0 * clearWidth / ( 0.84 * band ) - 1.0; };
	const Settings settings = { { "Mode", "Cell" },        { "Framing", "1" },       { "Frames", "Live" },
		                        { "Soundtracks", "1" },    { "Edge Print", "0" },    { "Rate", "24 fps" },
		                        { "Track Level", "0.5" } };

	//One bin.
	{
		std::vector< float > bins( GaugePlugin::kAudioBins, 0.0f );
		bins[ 1 ] = 0.25f;
		Run r;
		if( !render( true, width, height, settings, perturb, 5, [ & ]( int ) { return buildFlat( width, height, 0.5f ); }, r, 60.0,
		             120.0, [ & ]( Session& s, int ) { s.plugin->SetAudioForTest( bins.data(), static_cast< int >( bins.size() ) ); } ) )
			return 1;
		const TrackRows rows = readTrack( r.image, width, height, r.state.camera, f );
		std::vector< double > s;
		for( double w : rows.width )
			s.push_back( signalOf( w ) );
		const double nu = 1.5 * 22050.0 / 64.0 / ( 24.0 * f.pitch );
		double amplitude = 0.0, rms = 0.0;
		fitSine( rows.y, s, nu, amplitude, rms );
		//GLSL promises nothing for sin's accuracy; 1e-3 of full modulation is
		//far above what any conforming GPU's sin is off by over a few hundred
		//radians, and far below what a wrong frequency leaves (~0.18 here).
		const bool ok = rows.y.size() > 40 && std::fabs( amplitude - 0.25 ) < 1e-3 && rms < 1e-3;
		if( !quiet )
			std::printf( "track: bin 1 at 0.25 is a sine of %.5f at %.4f cycles/mm on the film (residual %.1e over %zu rows)  %s\n",
			             amplitude, nu, rms, rows.y.size(), verdict( ok ) );
		failures += ok ? 0 : 1;
	}

	//Silence.
	{
		Run r;
		if( !render( true, width, height, settings, perturb, 5, [ & ]( int ) { return buildFlat( width, height, 0.5f ); }, r ) )
			return failures + 1;
		const TrackRows rows = readTrack( r.image, width, height, r.state.camera, f );
		double worst         = 0.0;
		for( double w : rows.width )
			worst = std::max( worst, std::fabs( signalOf( w ) ) );
		const bool ok = !rows.y.empty() && worst < 1e-4;
		if( !quiet )
			std::printf( "track: silence is a clear band of constant width (largest |s| %.1e)  %s\n", worst, verdict( ok ) );
		failures += ok ? 0 : 1;
	}

	//The pop, on the source's own leader: Loop on from frame 0 at 24 fps, the
	//host at 24 Hz, so host frame 144 has the 2 in the gate.
	{
		const Settings leader = { { "Mode", "Cell" }, { "Framing", "1" }, { "Soundtracks", "1" }, { "Edge Print", "0" },
			                      { "Rate", "24 fps" } };
		Run r;
		if( !render( false, width, height, leader, perturb, Leader::kTwo + 1, [ & ]( int ) { return Image(); }, r, 24.0 ) )
			return failures + 1;
		const TrackRows all = readTrack( r.image, width, height, r.state.camera, f );
		const double y0     = static_cast< double >( r.state.gate - r.state.camera.base ) * f.pitch;
		std::vector< double > y, s;
		double quiet2 = 0.0;
		for( size_t i = 0; i < all.y.size(); ++i )
		{
			if( all.y[ i ] > y0 && all.y[ i ] < y0 + f.pitch )
			{
				y.push_back( all.y[ i ] - y0 );
				s.push_back( signalOf( all.width[ i ] ) );
			}
			else
				quiet2 = std::max( quiet2, std::fabs( signalOf( all.width[ i ] ) ) );
		}
		double amplitude = 0.0, rms = 0.0;
		fitSine( y, s, 1000.0 / ( 24.0 * f.pitch ), amplitude, rms );
		const bool ok = y.size() > 20 && std::fabs( amplitude - 0.8 ) < 1e-3 && rms < 1e-3 && quiet2 < 1e-4;
		if( !quiet )
			std::printf( "track: the 2 carries the pop, 1 kHz at %.5f (residual %.1e), and the frames either side are silent "
			             "(largest |s| %.1e)  %s\n",
			             amplitude, rms, quiet2, verdict( ok ) );
		failures += ok ? 0 : 1;
	}

	if( !quiet )
		std::printf( "%s\n", failures == 0 ? "track: the optical track is the sound, drawn at the film's speed" : "track: FAILURES" );
	return failures;
}

//---------------------------------------------------------------------------
// --alpha
//
// Film is opaque; what is not film is what the Holes and Surround controls
// say. On a strip with both Clear: alpha exactly 0 far off the film and in the
// middle of a hole, exactly 1 on the rebate and in a picture -- even of a clip
// that is half transparent, which prints as itself over black. With Surround
// Black, 1 off the film. And the source's frames After a leader that does not
// loop, set Clear, are cut out of the film: alpha 0 in the picture, 1 on the
// rebate round it.
//---------------------------------------------------------------------------
int runAlpha( int width, int height, int perturb = 0, bool quiet = false )
{
	int failures           = 0;
	const strip::Format& f = strip::FormatAt( strip::kAcademy );
	auto alphaAt           = [ & ]( const Run& r, double x, double y ) {
        double px = 0.0, py = 0.0;
        screenOf( r.state.camera, x, y, px, py );
        const int ix = static_cast< int >( px ), iy = static_cast< int >( py );
        if( ix < 0 || iy < 0 || ix >= width || iy >= height )
            return -1.0;
        return static_cast< double >( pixelAt( r.image, width, ix, iy )[ 3 ] );
	};

	for( const char* surround : { "Clear", "Black" } )
	{
		const Settings settings = { { "Mode", "Strip" },   { "Size", "0.6" },       { "Holes", "Clear" },
			                        { "Surround", surround }, { "Frames", "Live" }, { "Travel", "0.5" } };
		Run r;
		if( !render( true, width, height, settings, perturb, 3, [ & ]( int ) { return buildFlat( width, height, 0.5f, 0.5f ); }, r ) )
			return failures + 1;
		const double frame = static_cast< double >( r.state.gate - r.state.camera.base ) * f.pitch;
		const double off   = pixelAt( r.image, width, 0, 0 )[ 3 ];
		const double hole  = alphaAt( r, f.holeEdge + 0.5 * f.hole.across, frame + f.holeOffset );
		const double base  = alphaAt( r, 0.5 * ( f.imageX0 + f.imageX1 ), frame + 0.5 * f.ImageY0() );
		const double pic   = alphaAt( r, 0.5 * ( f.imageX0 + f.imageX1 ), frame + 0.5 * f.pitch );
		const double wantOff = std::string( surround ) == "Clear" ? 0.0 : 1.0;
		const bool ok = off == wantOff && hole == 0.0 && base == 1.0 && pic == 1.0;
		if( !quiet )
			std::printf( "alpha, Surround %s: off the film %.6f, in a hole %.6f, rebate %.6f, a half-clear clip's picture %.6f  %s\n",
			             surround, off, hole, base, pic, verdict( ok ) );
		failures += ok ? 0 : 1;
	}

	{
		const Settings settings = { { "Mode", "Cell" }, { "Loop", "0" }, { "After", "Clear" }, { "Rate", "24 fps" } };
		Run r;
		if( !render( false, width, height, settings, perturb, Leader::kLength + 3, [ & ]( int ) { return Image(); }, r, 24.0 ) )
			return failures + 1;
		const double frame = static_cast< double >( r.state.gate - r.state.camera.base ) * f.pitch;
		const double pic   = alphaAt( r, 0.5 * ( f.imageX0 + f.imageX1 ), frame + 0.5 * f.pitch );
		const double base  = alphaAt( r, 0.5 * ( f.imageX0 + f.imageX1 ), frame + 0.5 * f.ImageY0() );
		const bool ok      = pic == 0.0 && base == 1.0;
		if( !quiet )
			std::printf( "alpha, the source After Clear: the picture %.6f, the rebate round it %.6f  %s\n", pic, base, verdict( ok ) );
		failures += ok ? 0 : 1;
	}

	if( !quiet )
		std::printf( "%s\n", failures == 0 ? "alpha: film is opaque, and only what is not film is clear" : "alpha: FAILURES" );
	return failures;
}
