#include "HeldFrames.h"

#include <algorithm>

namespace gauge
{
namespace
{
int64_t floorMod( int64_t a, int64_t n )
{
	const int64_t r = a % n;
	return r < 0 ? r + n : r;
}

GLuint makeArray( int width, int height, int layers )
{
	GLuint texture = 0;
	glGenTextures( 1, &texture );
	glBindTexture( GL_TEXTURE_2D_ARRAY, texture );
	glTexImage3D( GL_TEXTURE_2D_ARRAY, 0, GL_RGBA8, width, height, layers, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr );
	glTexParameteri( GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
	glTexParameteri( GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
	glTexParameteri( GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
	glTexParameteri( GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
	glBindTexture( GL_TEXTURE_2D_ARRAY, 0 );
	return texture;
}
} // namespace

bool HeldFrames::Ensure( int newWidth, int newHeight, int newLayers, GLuint copyProgram, ffglex::FFGLScreenQuad& quad )
{
	newWidth  = std::max( newWidth, 1 );
	newHeight = std::max( newHeight, 1 );
	newLayers = std::max( newLayers, 1 );
	if( texture != 0 && newWidth == width && newHeight == height && newLayers <= layers )
		return true;
	//Grow the depth, never shrink it on its own: a ring that shrinks and grows
	//with a strip being dragged about would lose frames every time.
	newLayers = std::max( newLayers, layers );

	const GLuint fresh = makeArray( newWidth, newHeight, newLayers );
	if( fresh == 0 )
		return false;
	if( fbo == 0 )
		glGenFramebuffers( 1, &fbo );

	std::vector< int64_t > freshTags( static_cast< size_t >( newLayers ), INT64_MIN );

	GLint previousFBO = 0;
	glGetIntegerv( GL_FRAMEBUFFER_BINDING, &previousFBO );
	GLint previousViewport[ 4 ] = { 0, 0, 0, 0 };
	glGetIntegerv( GL_VIEWPORT, previousViewport );

	//Clear every new layer: undefined texture memory is not "noise on the first
	//frame", it is whatever the driver handed back.
	glBindFramebuffer( GL_FRAMEBUFFER, fbo );
	glViewport( 0, 0, newWidth, newHeight );
	for( int layer = 0; layer < newLayers; ++layer )
	{
		glFramebufferTextureLayer( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, fresh, 0, layer );
		glClearColor( 0.0f, 0.0f, 0.0f, 1.0f );
		glClear( GL_COLOR_BUFFER_BIT );
	}

	//Carry every held frame across, under the same index, in index order so
	//that if two land in one slot the later frame wins.
	if( texture != 0 && copyProgram != 0 )
	{
		std::vector< std::pair< int64_t, int > > held;
		for( int slot = 0; slot < layers; ++slot )
			if( tags[ static_cast< size_t >( slot ) ] != INT64_MIN )
				held.emplace_back( tags[ static_cast< size_t >( slot ) ], slot );
		std::sort( held.begin(), held.end() );

		glUseProgram( copyProgram );
		glActiveTexture( GL_TEXTURE0 );
		glBindTexture( GL_TEXTURE_2D_ARRAY, texture );
		glUniform1i( glGetUniformLocation( copyProgram, "Old" ), 0 );
		const GLint layerUniform = glGetUniformLocation( copyProgram, "Layer" );
		for( const auto& h : held )
		{
			const int to = static_cast< int >( floorMod( h.first, newLayers ) );
			glFramebufferTextureLayer( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, fresh, 0, to );
			glUniform1i( layerUniform, h.second );
			quad.Draw();
			freshTags[ static_cast< size_t >( to ) ] = h.first;
		}
		glBindTexture( GL_TEXTURE_2D_ARRAY, 0 );
		glUseProgram( 0 );
	}

	glFramebufferTextureLayer( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, 0, 0, 0 );
	glBindFramebuffer( GL_FRAMEBUFFER, static_cast< GLuint >( previousFBO ) );
	glViewport( previousViewport[ 0 ], previousViewport[ 1 ], previousViewport[ 2 ], previousViewport[ 3 ] );

	if( texture != 0 )
		glDeleteTextures( 1, &texture );
	texture = fresh;
	width   = newWidth;
	height  = newHeight;
	layers  = newLayers;
	tags    = std::move( freshTags );
	return true;
}

int HeldFrames::SlotOf( int64_t index ) const
{
	return layers > 0 ? static_cast< int >( floorMod( index, layers ) ) : 0;
}

bool HeldFrames::Holds( int64_t index ) const
{
	return layers > 0 && tags[ static_cast< size_t >( SlotOf( index ) ) ] == index;
}

void HeldFrames::Tag( int64_t index )
{
	if( layers > 0 )
		tags[ static_cast< size_t >( SlotOf( index ) ) ] = index;
}

void HeldFrames::Forget()
{
	std::fill( tags.begin(), tags.end(), INT64_MIN );
}

int HeldFrames::BeginSlot( int64_t index )
{
	const int slot = SlotOf( index );
	glBindFramebuffer( GL_FRAMEBUFFER, fbo );
	glFramebufferTextureLayer( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, texture, 0, slot );
	glViewport( 0, 0, width, height );
	return slot;
}

void HeldFrames::Unbind()
{
	glBindFramebuffer( GL_FRAMEBUFFER, fbo );
	glFramebufferTextureLayer( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, 0, 0, 0 );
	glBindFramebuffer( GL_FRAMEBUFFER, 0 );
}

void HeldFrames::Release()
{
	if( texture != 0 )
		glDeleteTextures( 1, &texture );
	if( fbo != 0 )
		glDeleteFramebuffers( 1, &fbo );
	texture = fbo = 0;
	width = height = layers = 0;
	tags.clear();
}

} // namespace gauge
