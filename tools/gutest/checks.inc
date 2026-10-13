//---------------------------------------------------------------------------
// The checks, the negative controls and the bench. Included into main.cpp's
// anonymous namespace, after the session and the parameter plumbing.
//---------------------------------------------------------------------------
#include "geometry.inc"

const std::vector< std::string >& checkNames()
{
	static const std::vector< std::string > names = { "--perfs", "--aperture", "--squeeze", "--negative" };
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
	if( check == "--negative" )
		return runNegative( width, height );
	return 1;
}

int runBench( const std::vector< std::string >&, bool, int, double )
{
	return 0;
}
