#pragma once

/**
	What a 0..1 host slider means, with inverses, and the option lists.

	`SetParamInfo` clamps an FF_TYPE_STANDARD default into 0..1 before
	`SetParamRange` can widen it (SDK b1afaf9), so every ranged control is a
	plain 0..1 float and the units live here. Options are mapped by index: an
	option parameter's range reads back 0..1 whatever its element count.
*/
namespace gauge::controls
{

//--- options ---------------------------------------------------------------
enum Stock
{
	kColourPrint = 0,
	kBwPrint,
	kClearBase,
	kStockCount
};
const char* StockName( int index );

/// Squeeze: the format's own, or one of the anamorphic lenses people used.
enum Squeeze
{
	kSqueezeFormat = 0,
	kSqueezeNone,
	kSqueeze133,
	kSqueeze150,
	kSqueeze200,
	kSqueezeCount
};
const char* SqueezeName( int index );
double SqueezeFactor( int index, double formatSqueeze );

enum Fit
{
	kFitCrop = 0,
	kFitLetterbox,
	kFitStretch,
	kFitCount
};
const char* FitName( int index );

enum Mode
{
	kModeCell = 0,
	kModeStrip,
	kModeCount
};
const char* ModeName( int index );

/// The film's rate. Beat is 24 frames to a beat of the host's tempo, so a
/// leader count is a beat.
enum Rate
{
	kRate24 = 0,
	kRate25,
	kRate18,
	kRate16,
	kRateBeat,
	kRateCount
};
const char* RateName( int index );
/// Frames per second for the fixed rates; 0 for Beat.
double RateFps( int index );

enum Frames
{
	kFramesHeld = 0,
	kFramesLive,
	kFramesCount
};
const char* FramesName( int index );

enum StartOn
{
	kStartPress = 0,
	kStartBeat,
	kStartBar,
	kStartOnCount
};
const char* StartOnName( int index );

enum PopFrame
{
	kPopNumber = 0,
	kPopFlash,
	kPopFrameCount
};
const char* PopFrameName( int index );

enum AutoFlash
{
	kAutoOff = 0,
	kAutoBeat,
	kAutoBar,
	kAutoFlashCount
};
const char* AutoFlashName( int index );

enum Holes
{
	kHolesLight = 0,
	kHolesClear,
	kHolesBlack,
	kHolesCount
};
const char* HolesName( int index );

/// What is beyond the film's edges. Clip is the effect's own input.
enum Surround
{
	kSurroundClear = 0,
	kSurroundBlack,
	kSurroundLight,
	kSurroundClip,
	kSurroundCount
};
const char* SurroundName( int index );

/// The source's frames after a leader that does not loop.
enum After
{
	kAfterBlack = 0,
	kAfterClear,
	kAfterCount
};
const char* AfterName( int index );

int OptionIndex( float value, int count );

//--- sliders ---------------------------------------------------------------

/// Framing (cell): 0 fills the output with the picture, 1 shows the film's
/// width and a quarter again. Log-interpolated between the two scales.
float Framing( float value );

/// Rack (cell): the view's offset along the film, -0.5..+0.5 of a frame.
double RackFrames( float value );
float RackParam( double frames );

/// Drift (cell): the strip slipping through the view, -4..+4 frames a
/// second, cubic so the slow end has room.
double DriftFps( float value );
float DriftParam( double fps );

/// Size (strip): the film's width as a fraction of the output's height,
/// 0.05..1, logarithmic.
double SizeFraction( float value );
float SizeParam( double fraction );

/// Angle (strip): -180..+180 degrees. 0 lays the film's length left to
/// right with the reference edge at the top.
double AngleDegrees( float value );
float AngleParam( double degrees );

/// Position (strip): the film's centre line, -1..+1 of half the output's
/// height, across the strip.
double PositionFraction( float value );
float PositionParam( double fraction );

/// Travel (strip): frames a second, -24..+24, cubic.
double TravelFps( float value );
float TravelParam( double fps );

/// Shutter: the motion blur's exposure, 0..360 degrees of a host frame.
double ShutterDegrees( float value );
float ShutterParam( double degrees );

/// Track Level: the optical track's gain on the audio, 0..2.
float TrackLevel( float value );
float TrackLevelParam( float level );

} // namespace gauge::controls
