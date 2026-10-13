#include "Camera.h"

#include <algorithm>
#include <cmath>

namespace gauge::camera
{
namespace
{
constexpr double kPi = 3.14159265358979323846;

/// Split an absolute position in frames into a base frame and the offset of
/// the position from that frame's top, in mm.
void splitFrames( double frames, double pitch, int64_t& base, double& offsetMm )
{
	const double whole = std::floor( frames );
	base               = static_cast< int64_t >( whole );
	offsetMm           = ( frames - whole ) * pitch;
}
} // namespace

double CellScale( const strip::Format& format, int width, int height, double framing )
{
	const double w = std::max( width, 1 );
	const double h = std::max( height, 1 );

	//The picture across and along the output's own x and y.
	const double imageX = format.imageAcross ? format.imageHeight : format.ImageWidth();
	const double imageY = format.imageAcross ? format.ImageWidth() : format.imageHeight;
	//The film's width runs across the output's x for a motion picture format
	//and down its y for the still one.
	const double filmExtent = format.imageAcross ? h : w;

	const double cover = std::min( imageX / w, imageY / h );
	const double wide  = 1.25 * format.width / filmExtent;
	return cover * std::pow( wide / cover, std::clamp( framing, 0.0, 1.0 ) );
}

Camera Cell( const strip::Format& format, int width, int height, double framing, double centreFrames )
{
	Camera c;
	c.mmPerPixel = CellScale( format, width, height, framing );
	const double s = c.mmPerPixel;

	double offset = 0.0;
	splitFrames( centreFrames, format.pitch, c.base, offset );
	//Centred on the picture when it fills the output, on the film when the whole
	//width is in view: a sound print's picture sits off the film's centre line,
	//and a wide view centred on it shows one edge and not the other.
	const double t       = std::clamp( framing, 0.0, 1.0 );
	const double centreX = 0.5 * ( format.imageX0 + format.imageX1 ) * ( 1.0 - t ) + 0.5 * format.width * t;
	const double halfW   = 0.5 * std::max( width, 1 );
	const double halfH   = 0.5 * std::max( height, 1 );

	if( format.imageAcross )
	{
		//The still format: the output's x runs along the film, its y across.
		c.axisXx = 0.0;
		c.axisXy = s;
		c.axisYx = s;
		c.axisYy = 0.0;
		c.originX = centreX - halfH * s;
		c.originY = offset - halfW * s;
	}
	else
	{
		c.axisXx = s;
		c.axisXy = 0.0;
		c.axisYx = 0.0;
		c.axisYy = s;
		c.originX = centreX - halfW * s;
		c.originY = offset - halfH * s;
	}
	return c;
}

Camera Strip( const strip::Format& format, int width, int height, double size, double angleDegrees, double position,
              double centreFrames )
{
	Camera c;
	const double h = std::max( height, 1 );
	c.mmPerPixel   = format.width / ( std::max( size, 1e-3 ) * h );
	const double s = c.mmPerPixel;

	double offset = 0.0;
	splitFrames( centreFrames, format.pitch, c.base, offset );

	//Along the film (toward the tail) is u = ( cos a, sin a ) on the output, y
	//down. Across it (toward the far edge) is a quarter turn from u that keeps
	//the picture's handedness: the Cell view puts a motion picture's across on
	//the output's x and its along on y, so turning that until along is u puts
	//across a quarter turn anticlockwise of u; the still format's Cell view
	//already has along on x and across on y. Swapping the two axes instead
	//would be a reflection, and every frame would be mirrored.
	const double a  = angleDegrees * kPi / 180.0;
	const double ux = std::cos( a ), uy = std::sin( a );
	const double nx = format.imageAcross ? -uy : uy;
	const double ny = format.imageAcross ? ux : -ux;

	//Position moves the film's centre line down the output at Angle 0,
	//whichever way its far edge faces.
	const double px = -uy, py = ux;
	const double cx = 0.5 * std::max( width, 1 ) + position * 0.5 * h * px;
	const double cy = 0.5 * h + position * 0.5 * h * py;

	//film y = offset + s ( p - C ) . u ;  film x = W / 2 + s ( p - C ) . n
	c.axisXy  = s * ux;
	c.axisYy  = s * uy;
	c.axisXx  = s * nx;
	c.axisYx  = s * ny;
	c.originY = offset - s * ( cx * ux + cy * uy );
	c.originX = 0.5 * format.width - s * ( cx * nx + cy * ny );
	return c;
}

void AlongRange( const Camera& camera, int width, int height, double& lo, double& hi )
{
	lo = 1e300;
	hi = -1e300;
	const double xs[ 2 ] = { 0.0, static_cast< double >( width ) };
	const double ys[ 2 ] = { 0.0, static_cast< double >( height ) };
	for( double px : xs )
		for( double py : ys )
		{
			double x = 0.0, y = 0.0;
			camera.Film( px, py, x, y );
			lo = std::min( lo, y );
			hi = std::max( hi, y );
		}
}

} // namespace gauge::camera
