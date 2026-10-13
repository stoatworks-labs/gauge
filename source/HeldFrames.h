#pragma once

#include <FFGLSDK.h>

#include <cstdint>
#include <vector>

namespace gauge
{
/**
	The frames on the strip that have been exposed: a ring of pictures in a
	GL_TEXTURE_2D_ARRAY, one layer per slot, and the film frame index each slot
	holds.

	Frame i lives in slot i mod N. A slot is only ever read when its tag says it
	holds the frame asked for, so a frame that has scrolled out of the ring is
	never shown as somebody else's picture.

	**A resize keeps what is held.** The cell size follows the camera, so a
	host raster change or a Size drag reallocates -- and reallocation draws
	every held layer into the new array, resampled, under the same frame index.
	A moving strip that lost its frames whenever the composition changed size
	would be the photofinish trap; `gutest --resize` checks it.
*/
class HeldFrames
{
public:
	/// Make sure the ring is at least `layers` deep at `width` x `height`.
	/// Returns false if GL could not allocate it. `copyProgram` draws one layer
	/// of the old array (uniform `Layer`, sampler unit 0) across the viewport;
	/// `quad` is the screen quad it is drawn with.
	bool Ensure( int width, int height, int layers, GLuint copyProgram, ffglex::FFGLScreenQuad& quad );

	/// Bind the framebuffer to the slot that holds (or will hold) frame `index`
	/// and set the viewport to it. The caller draws, then calls Unbind.
	int BeginSlot( int64_t index );
	void Unbind();

	int SlotOf( int64_t index ) const;
	bool Holds( int64_t index ) const;
	void Tag( int64_t index );
	/// Forget everything held, without touching GL.
	void Forget();

	GLuint Texture() const
	{
		return texture;
	}
	int Width() const
	{
		return width;
	}
	int Height() const
	{
		return height;
	}
	int Layers() const
	{
		return layers;
	}

	void Release();

private:
	GLuint texture = 0;
	GLuint fbo     = 0;
	int width      = 0;
	int height     = 0;
	int layers     = 0;
	std::vector< int64_t > tags;
};

} // namespace gauge
