/*
    The one GL entry point emscripten's WebGL2 library cannot take from the
    plugin as it is: glShaderSource. Every other GL call the plugin and the FFGL
    SDK make -- the held frames' GL_TEXTURE_2D_ARRAY and its layers bound to a
    framebuffer one at a time, the RGBA32I content table, the R32F spectrum,
    the R8 font and atlas, the RGBA32F metrics -- goes straight to emscripten's
    implementation, on the page's own WebGL2 context, with the formats and
    types the plugin asks for: WebGL2 accepts each of them as it is, and the
    only float textures are read with texelFetch, never filtered or rendered
    into. zoetrope's shim, with one entry point instead of four (gauge never
    touches GL_PROGRAM_POINT_SIZE).

    glShaderSource
        The plugin hands GL desktop GLSL 4.10 (`#version 410 core`); WebGL2
        compiles GLSL ES 3.00. The page's `gaugeShaderSource` first REQUIRES
        the text to be byte for byte one of the page's own assemblies of the
        plugin's shaders (demo/shaders.js, which demo/tools/check_shaders.py
        holds to source/Shaders.cpp and to the order its Vertex(), Capture(),
        Composite() and Copy() join the pieces in), then applies the demo
        kit's `port()`: the version line and the ES precision defaults,
        nothing else. A shader the plugin assembled differently from the
        page's copy is refused (an empty source, so the plugin's InitGL fails
        and logs which pass), never compiled quietly.
*/
#include <GLES3/gl3.h>
#include <emscripten/emscripten.h>

#include <cstring>
#include <string>

namespace
{
// clang-format off
EM_JS( void, guShaderSource, ( GLuint shader, const char* source ), {
	const object = GL.shaders[ shader ];
	const stage  = GLctx.getShaderParameter( object, 0x8B4F /* GL_SHADER_TYPE */ ) === 0x8B31 ? 'vertex' : 'fragment';
	GLctx.shaderSource( object, Module[ 'gaugeShaderSource' ]( stage, UTF8ToString( source ) ) );
} );
// clang-format on
} // namespace

extern "C"
{
void glShaderSource( GLuint shader, GLsizei count, const GLchar* const* string, const GLint* length )
{
	std::string source;
	for( GLsizei i = 0; i < count; ++i )
	{
		if( string[ i ] == nullptr )
			continue;
		const size_t n = ( length != nullptr && length[ i ] >= 0 ) ? static_cast< size_t >( length[ i ] ) : std::strlen( string[ i ] );
		source.append( string[ i ], n );
	}
	guShaderSource( shader, source.c_str() );
}
} // extern "C"
