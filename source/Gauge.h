#pragma once

#include "Camera.h"
#include "HeldFrames.h"
#include "StoatworksAboutParams.h"
#include "Strip.h"
#include "Transport.h"

#include <FFGLSDK.h>

#include <array>
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

/**
	Gauge -- the film strip itself, as an FFGL effect (`SW Gauge`) and a
	countdown-leader source (`SW Gauge Leader`), from one class.

	**The one idea.** A film strip is a standard, not a texture. Every gauge's
	holes, pitch, apertures and soundtrack areas are published dimensions, and
	a strip is a sequence of frames that each hold one exposure. So the strip is
	modelled in film millimetres (`Strip.h`), a camera looks at it
	(`Camera.h`), and each frame is exposed once -- when it passes the gate in
	Cell mode, or when it comes into view on a moving Strip -- and keeps that
	picture as it travels (`HeldFrames.h`). The leader is film too: spliced on,
	played at the film's rate, so on a moving strip it passes as a row of
	numbered frames.

	Two passes: **capture** (the clip, fitted and squeezed into the aperture,
	into a held slot) and **composite** (every output pixel looked up on the
	film, holes, frame lines, soundtracks, edge print, pictures and leader,
	averaged over the shutter for motion blur). See AGENTS.md for the traps.
*/
namespace gauge
{

/// Every control either plugin can have. Each plugin declares a subset, in
/// its own order; the host's parameter index maps to one of these.
enum Kind : int
{
	K_FORMAT,
	K_STOCK,
	K_SQUEEZE,
	K_FIT,
	K_EDGE_PRINT,
	K_SOUNDTRACKS,
	K_TRACK_LEVEL,
	K_AUDIO,

	K_MODE,
	K_FRAMING,
	K_RACK,
	K_DRIFT,
	K_SIZE,
	K_ANGLE,
	K_POSITION,
	K_TRAVEL,
	K_SHUTTER,

	K_RATE,
	K_FRAMES,

	K_LOOP,
	K_ROLL_LEADER,
	K_START_ON,
	K_POP_FRAME,
	K_FLASH,
	K_AUTO_FLASH,
	K_AFTER,

	K_HOLES,
	K_SURROUND,
	K_MIX,

	K_KIND_COUNT
};

/// What a frame on the strip shows.
enum Content : int
{
	C_LIVE   = 0,///< the clip as it is now
	C_HELD   = 1,///< a held slot
	C_LEADER = 2,///< leader frame j
	C_FLASH  = 3,///< a sync flash: one fully exposed frame
	C_BLACK  = 4,///< dense black film
	C_CLEAR  = 5,///< nothing: the picture area is transparent (the source's After Clear)
};

/// The leader, in film frames from PICTURE START.
struct Leader
{
	static constexpr int kCount       = 24; ///< frames to a count (a second at 24 fps, a beat at Rate Beat)
	static constexpr int kFirstNumber = 8;
	static constexpr int kTwo         = 144;///< the 2: one frame
	static constexpr int kPicture     = 192;///< the first frame of picture, 8 counts after PICTURE START
	static constexpr int kLength      = kPicture;

	/// The numeral: its ink height in the picture's height, and how much
	/// bolder than the stroked face it is drawn, in its own height.
	static constexpr float kDigitHeight = 0.40f;
	static constexpr float kDigitBold   = 0.04f;

	/// The number shown on leader frame j, 0 for PICTURE START, -1 for black.
	static int NumberAt( int j )
	{
		if( j == 0 )
			return 0;
		if( j > 0 && j < kTwo )
			return kFirstNumber - j / kCount;
		if( j == kTwo )
			return 2;
		return -1;
	}
};

class GaugePlugin : public CFFGLPlugin
{
public:
	explicit GaugePlugin( bool isEffect );

	//CFFGLPlugin
	FFResult InitGL( const FFGLViewportStruct* vp ) override;
	FFResult ProcessOpenGL( ProcessOpenGLStruct* pGL ) override;
	FFResult DeInitGL() override;

	FFResult SetFloatParameter( unsigned int index, float value ) override;
	float GetFloatParameter( unsigned int index ) override;
	FFResult SetTime( double time ) override;
	void SetBeatInfo( float bpm, float barPhase ) override;

	char* GetTextParameter( unsigned int index ) override;
	/// The About line must accept its own default or the host deletes the
	/// instance (instantiateGL pushes every default back through the setters).
	FFResult SetTextParameter( unsigned int index, const char* value ) override;

	bool IsEffect() const
	{
		return isEffect;
	}

	/// The host index of a control, or -1 if this plugin does not have it.
	int IndexOf( Kind kind ) const
	{
		return hostIndex[ kind ];
	}
	unsigned int ParamCount() const
	{
		return static_cast< unsigned int >( order.size() ) + stoatworks::about::kParamCount;
	}
	unsigned int AboutFirst() const
	{
		return static_cast< unsigned int >( order.size() );
	}

	//--- test hooks: the harness drives the clock and the transport, and the
	//--- negative controls perturb the plugin's model, never the harness's.
	void SetClockScaleForTest( double scale );
	void SetTransportForTest( double bpm, double barPhase );
	void SetPerturbForTest( int bits );
	/// Feed the FFT input's 64 bins directly, as the host would.
	void SetAudioForTest( const float* bins, int count );

	/// The film's state after the last frame, for the checks to predict from.
	struct State
	{
		double film       = 0.0;///< the film clock in frames
		int64_t gate      = 0;  ///< the frame in the gate
		double rate       = 24.0;
		double stripFrames = 0.0;///< the strip's centre, in frames
		int64_t leaderStart = INT64_MIN;
		camera::Camera camera;
		int64_t visibleLo = 0, visibleHi = -1;
		int heldWidth = 0, heldHeight = 0, heldLayers = 0;
	};
	const State& StateForTest() const
	{
		return state;
	}
	/// Content of frame `index` as the last composite drew it: kind and value.
	bool ContentForTest( int64_t index, int& kind, int& value ) const;

	static constexpr int kAudioBins   = 64;
	static constexpr int kMaxVisible  = 128;
	static constexpr int kMaxFlashes  = 32;

private:
	void declare();
	float value( Kind kind ) const
	{
		return values[ kind ];
	}
	int option( Kind kind, int count ) const;
	double nowSeconds();

	bool compileShaders();
	/// The frame a leader or a flash lands on, `overshoot` seconds after the
	/// boundary it waited for (press: the next frame).
	int64_t startIndex( bool press, double overshoot ) const;
	void fire( bool leader, int64_t at );
	void contentFor( int64_t index, int& kind, int& value ) const;
	void readAudio();

	bool isEffect;
	std::vector< Kind > order;
	std::array< int, K_KIND_COUNT > hostIndex{};
	std::array< float, K_KIND_COUNT > values{};
	std::array< bool, K_KIND_COUNT > pressed{};///< an event control's last level

	ffglex::FFGLShader captureShader;
	ffglex::FFGLShader compositeShader;
	ffglex::FFGLShader copyShader;
	ffglex::FFGLScreenQuad quad;

	HeldFrames ring;
	GLuint contentTexture  = 0;///< RGBA32I, kMaxVisible x 1: kind, value, spectrum row
	GLuint spectrumTexture = 0;///< R32F, 128 x rows: 64 magnitudes then 64 phases
	GLuint fontTexture     = 0;///< the 5x7 edge-print font
	GLuint glyphTexture    = 0;///< the leader's distance-field atlas
	GLuint metricsTexture  = 0;///< RGBA32F, 44 x 2: the atlas's glyph metrics
	int spectrumRows       = 0;
	std::vector< float > spectrum;///< CPU copy, row-major

	//--- the clock (readout's unit voting) -------------------------------------
	bool hostTimeSeen   = false;
	double clockScale   = 0.0;
	double wallStart    = -1.0;
	double lastWallTime = -1.0;
	double lastRawTime  = -1.0;
	int secondsVotes    = 0;
	int millisVotes     = 0;
	double lastNow      = -1.0;
	bool started        = false;

	Transport transport;
	double lastBeats = 0.0;

	//--- the film ------------------------------------------------------------
	State state;
	double filmClock   = 0.0;///< frames, for the fixed rates
	double drift       = 0.0;///< frames, wrapped to a frame
	double stripOffset = 0.0;///< frames
	int lastMode       = -1;
	int lastFormat     = -1;
	int travelSign     = 1;

	int64_t leaderStart = INT64_MIN;///< the PICTURE START frame, or none
	int leaderDirection = 1;        ///< -1 when a strip running backwards brings it in
	std::deque< int64_t > flashes;  ///< frames exposed as sync flashes
	bool pendingLeader  = false;
	bool pendingFlash   = false;
	double pendingLeaderBeat = 0.0; ///< the transport beat count a pending start waits for
	double pendingFlashBeat  = 0.0;
	int rollPresses     = 0;
	int flashPresses    = 0;

	std::array< float, kAudioBins > audio{};
	bool audioForTest = false;

	//--- what the last composite drew, by frame --------------------------------
	int64_t drawnLo = 0;
	std::vector< int > drawnKind;
	std::vector< int > drawnValue;

	int perturb = 0;

	std::string aboutText;
};

/// Bits for SetPerturbForTest: each breaks one claim, so a check can be shown to fail.
enum Perturb : int
{
	kPerturbHoleSwap    = 1,  ///< a hole's across and along dimensions swapped
	kPerturbFrameLine   = 2,  ///< 35 mm frame line through a hole, not between two
	kPerturbSqueeze     = 4,  ///< the squeeze ignored
	kPerturbLiveHeld    = 8,  ///< held frames re-exposed every frame
	kPerturbLeaderCount = 16, ///< 25 frames to a count
	kPerturbFlashLong   = 32, ///< a flash two frames long
	kPerturbTrackSpeed  = 64, ///< the track drawn at 25 fps whatever the rate
	kPerturbHolesOpaque = 128,///< clear holes drawn opaque
	kPerturbResizeForget = 256,///< a resize forgets the held frames
	kPerturbImageCentred = 512,///< the sound prints' picture centred on the film
};

} // namespace gauge
