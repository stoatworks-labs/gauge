#pragma once

#include <cmath>

/**
	Resolume's transport, as FFGL 2.1 gives it: `SetBeatInfo( bpm, barPhase )`.

	jackpot's, with one addition: a count of whole bars, so the film clock can be
	24 frames to a beat from a fixed origin and a leader can start on a bar and
	land its picture on another. The SDK defaults the BPM to 120; nothing in the
	fleet has measured how often Resolume calls SetBeatInfo or whether
	`barPhase` is a usable downbeat (pattern's note), so this keeps its own phase
	from the BPM and the host's elapsed time and RE-ALIGNS to the host's
	barPhase whenever one arrives, trusting it as a 0..1 position in a 4/4 bar.
	A host phase that falls by more than half a bar is a new bar. No GL, so the
	harness drives it directly.
*/
namespace gauge
{
class Transport
{
public:
	static constexpr double kBeatsPerBar = 4.0;

	void SetBeatInfo( double newBpm, double newBarPhase )
	{
		if( newBpm > 0.0 && std::isfinite( newBpm ) )
			bpm = newBpm;
		if( std::isfinite( newBarPhase ) )
		{
			hostBarPhase = newBarPhase - std::floor( newBarPhase );
			hasHostPhase = true;
		}
	}

	/// Advance by the frame's host dt (real seconds). The phase runs at the
	/// BPM; a host bar phase overrides it.
	void Advance( double dtHostSeconds )
	{
		double next;
		if( hasHostPhase )
		{
			next         = hostBarPhase;
			hasHostPhase = false;
		}
		else
			next = barPhase + dtHostSeconds * bpm / 60.0 / kBeatsPerBar;

		if( next >= 1.0 )
		{
			bars += std::floor( next );
			next -= std::floor( next );
		}
		else if( next < barPhase - 0.5 )
			bars += 1.0;//the host's phase wrapped
		barPhase = next;
	}

	double Bpm() const
	{
		return bpm;
	}
	double BarPhase() const
	{
		return barPhase;
	}
	/// Beats since this transport started counting, whole bars and the phase.
	double Beats() const
	{
		return ( bars + barPhase ) * kBeatsPerBar;
	}
	double BeatSeconds() const
	{
		return 60.0 / bpm;
	}
	double BarSeconds() const
	{
		return kBeatsPerBar * 60.0 / bpm;
	}

	/// Real seconds to the next beat (bar = false) or bar boundary that is not
	/// in the past. A boundary this frame sits on exactly counts as next.
	double SecondsToNext( bool bar ) const
	{
		const double period = bar ? BarSeconds() : BeatSeconds();
		const double phase  = bar ? barPhase : ( barPhase * kBeatsPerBar - std::floor( barPhase * kBeatsPerBar ) );
		if( phase <= 1e-9 )
			return 0.0;
		return ( 1.0 - phase ) * period;
	}

private:
	double bpm          = 120.0;
	double barPhase     = 0.0;
	double bars         = 0.0;
	double hostBarPhase = 0.0;
	bool hasHostPhase   = false;
};

} // namespace gauge
