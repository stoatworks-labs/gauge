#pragma once

#include <string>

/**
	The plugin's GLSL, assembled at run time.

	Three passes. **capture** puts the clip, fitted and squeezed, into a held
	slot. **composite** draws the output: every pixel looked up on the film.
	**copy** carries a held layer across when the ring is reallocated.

	`gutest --dump-shaders` writes exactly these strings, and `tools/verify.sh`
	compiles what it writes, so what is checked is what the driver is given.
*/
namespace gauge::shaders
{
std::string Vertex();
std::string Capture();
std::string Composite();
std::string Copy();
} // namespace gauge::shaders
