//---------------------------------------------------------------------------
// The checks, the negative controls and the bench. Included into main.cpp's
// anonymous namespace, after the session and the parameter plumbing.
//---------------------------------------------------------------------------
#include "geometry.inc"
#include "time.inc"
#include "signal.inc"

const std::vector< std::string >& checkNames()
{
	static const std::vector< std::string > names = { "--perfs", "--aperture", "--squeeze", "--held",
		                                                   "--leader", "--flash", "--resize", "--track",
		                                                   "--alpha", "--negative" };
	return names;
}

bool isCheck( const std::string& name )
{
	const auto& names = checkNames();
	return std::find( names.begin(), names.end(), name ) != names.end();
}

int runNegative( int width, int height )
{
	struct Control
	{
		const char* name;
		int failuresSeen;
	};
	const Control controls[] = {
		{ "perfs with a hole turned through 90 degrees        ", runPerfs( width, height, kPerturbHoleSwap, true ) },
		{ "perfs with the 35 mm frame line through a hole     ", runPerfs( width, height, kPerturbFrameLine, true ) },
		{ "aperture with the sound prints' picture centred    ", runAperture( width, height, kPerturbImageCentred, true ) },
		{ "squeeze ignored                                    ", runSqueeze( width, height, kPerturbSqueeze, true ) },
		{ "held with every frame re-exposed every frame       ", runHeld( width, height, kPerturbLiveHeld, true ) },
		{ "leader with 25 frames to a count                   ", runLeader( width, height, kPerturbLeaderCount, true ) },
		{ "flash two frames long                              ", runFlash( width, height, kPerturbFlashLong, true ) },
		{ "resize that forgets the held frames                ", runResize( width, height, kPerturbResizeForget, true ) },
		{ "track drawn at 25 fps whatever the film's rate     ", runTrack( width, height, kPerturbTrackSpeed, true ) },
		{ "alpha with clear holes drawn opaque                ", runAlpha( width, height, kPerturbHolesOpaque, true ) },
	};
	int failures = 0;
	for( const Control& c : controls )
	{
		const bool ok = c.failuresSeen > 0;
		std::printf( "negative %s: %s  %s\n", c.name, ok ? "it failed" : "it PASSED", verdict( ok ) );
		if( !ok )
			++failures;
	}
	std::printf( "%s\n", failures == 0 ? "negative: every perturbed model is caught" : "negative: FAILURES -- a check cannot fail" );
	return failures;
}

int runCheck( const std::string& check, int width, int height, int perturb )
{
	if( check == "--perfs" )
		return runPerfs( width, height, perturb );
	if( check == "--aperture" )
		return runAperture( width, height, perturb );
	if( check == "--squeeze" )
		return runSqueeze( width, height, perturb );
	if( check == "--held" )
		return runHeld( width, height, perturb );
	if( check == "--leader" )
		return runLeader( width, height, perturb );
	if( check == "--flash" )
		return runFlash( width, height, perturb );
	if( check == "--resize" )
		return runResize( width, height, perturb );
	if( check == "--track" )
		return runTrack( width, height, perturb );
	if( check == "--alpha" )
		return runAlpha( width, height, perturb );
	if( check == "--negative" )
		return runNegative( width, height );
	return 1;
}

//---------------------------------------------------------------------------
// --bench: ProcessOpenGL at 720p through 4K, best of three runs, glFinish
// both sides, on a GPU shared with whatever else is running. Two cases: the
// defaults (a Cell, the clip live in the gate), and the costliest a strip
// gets -- 24 frames a second with the shutter open all the way, sixteen taps
// of motion blur over every pixel.
//---------------------------------------------------------------------------
double benchAt( const std::vector< std::string >& settings, const Settings& extra, bool effect, int width, int height,
                int frames, double fps )
{
	Session session( effect );
	session.floatOutput = false;
	session.fps         = fps;
	for( const std::string& s : settings )
	{
		std::string error;
		applySetting( *session.plugin, s, error );
	}
	applyAll( *session.plugin, extra );
	if( !session.begin( width, height ) )
		return -1.0;
	const Image card = buildCard( width, height, 0 );
	session.upload( card );
	const int warmup = 20;
	for( int frame = 0; frame < warmup; ++frame )
		session.renderAt( frame );
	glFinish();
	double best = 1e9;
	for( int run = 0; run < 3; ++run )
	{
		const auto start = std::chrono::steady_clock::now();
		for( int frame = 0; frame < frames; ++frame )
			session.renderAt( warmup + run * frames + frame );
		glFinish();
		const double seconds = std::chrono::duration< double >( std::chrono::steady_clock::now() - start ).count();
		best                 = std::min( best, seconds * 1000.0 / frames );
	}
	session.end();
	return best;
}

int runBench( const std::vector< std::string >& settings, bool effect, int frames, double fps )
{
	struct Size
	{
		const char* name;
		int width, height;
	};
	const Size sizes[] = { { "1280x720 ", 1280, 720 }, { "1920x1080", 1920, 1080 }, { "2560x1440", 2560, 1440 }, { "3840x2160", 3840, 2160 } };
	char travel[ 32 ];
	std::snprintf( travel, sizeof( travel ), "%.9g", controls::TravelParam( 24.0 ) );
	const Settings strip = { { "Mode", "Strip" }, { "Travel", travel }, { "Shutter", "1" } };

	std::printf( "%d frames each, best of three runs after a 20-frame warm-up, glFinish both sides.\n\n", frames );
	std::printf( "resolution   defaults ms   %%60fps   strip, 24 fps, 360 deg ms   %%60fps\n" );
	for( const Size& size : sizes )
	{
		const double a = benchAt( settings, {}, effect, size.width, size.height, frames, fps );
		const double b = benchAt( settings, strip, effect, size.width, size.height, frames, fps );
		std::printf( "%s     %7.3f     %5.1f%%          %7.3f            %5.1f%%\n", size.name, a, a / 16.667 * 100.0, b,
		             b / 16.667 * 100.0 );
	}
	std::printf( "\nWhatever --set says is what was measured.\n" );
	return 0;
}
