#pragma once

#include "Strip.h"

#include <cstdint>

/**
	The camera: which film millimetre each output pixel sees.

	`film = origin + px * axisX + py * axisY`, where ( px, py ) is a pixel
	CENTRE counted from the top-left of the output, and film y is measured
	from the top of frame `base` (so it stays a small number whatever frame the
	film has reached: a 64-bit index never reaches the GPU). Pure arithmetic,
	no GL, so the harness can ask where anything is.

	**Cell** looks at one frame the way it would be scanned: the picture upright,
	the frame's centre in the middle of the output, the reference edge on the
	left (or at the top, for the still format, whose picture runs along the
	film). Framing 0 fills the output with the picture; 1 shows the film's width
	and a quarter again.

	**Strip** lays the film across the output: its length along Angle (0 runs
	toward the tail left to right), its width Size of the output's height, its
	centre line Position of half the height off centre. It is the Cell view
	turned, never reflected: a motion picture frame lies on its side with its
	top toward the head, a still frame stands upright.
*/
namespace gauge::camera
{

struct Camera
{
	double originX = 0.0, originY = 0.0;///< film mm at pixel coordinate ( 0, 0 )
	double axisXx = 0.0, axisXy = 0.0;  ///< film mm per pixel step in x: ( across, along )
	double axisYx = 0.0, axisYy = 0.0;  ///< film mm per pixel step in y
	double mmPerPixel = 1.0;
	int64_t base      = 0;///< the frame whose top is film y = 0

	/// Film position of pixel centre ( px, py ), top-down pixel coordinates.
	void Film( double px, double py, double& x, double& y ) const
	{
		x = originX + px * axisXx + py * axisYx;
		y = originY + px * axisXy + py * axisYy;
	}
};

/// Where the view's centre is along the film, in frames from frame 0's top:
/// frame k's centre is at k + 0.5.
Camera Cell( const strip::Format& format, int width, int height, double framing, double centreFrames );

Camera Strip( const strip::Format& format, int width, int height, double size, double angleDegrees, double position,
              double centreFrames );

/// The range of film y (relative to the camera's base) the output covers.
void AlongRange( const Camera& camera, int width, int height, double& lo, double& hi );

/// The scale of a Cell camera at a framing, mm per output pixel.
double CellScale( const strip::Format& format, int width, int height, double framing );

} // namespace gauge::camera
