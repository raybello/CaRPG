// GL shader compile / link / hot-reload helper.
// Designed to work with both desktop GL 3.0 and WebGL 1 (GLSL ES 100).
#pragma once

#include <string>

#if defined(__EMSCRIPTEN__)
#  include <GLES3/gl3.h>
#elif defined(__APPLE__)
// <OpenGL/gl3.h> exposes the full OpenGL 3.x core profile including all
// GL 2.0 symbols (glDeleteProgram, glUniform3f, glUniformMatrix4fv, …).
// <SDL_opengl.h> on macOS only wraps <OpenGL/gl.h> which stops at GL 1.x.
#  include <OpenGL/gl3.h>
#else
// Linux / Windows: declare extension prototypes so GL 2+ symbols are visible.
#  ifndef GL_GLEXT_PROTOTYPES
#    define GL_GLEXT_PROTOTYPES
#  endif
#  include <GL/gl.h>
#  include <GL/glext.h>
#endif

// Returns true on success.  On failure `outLog` contains compiler/linker errors.
// On success `outProgram` is a freshly-created GL program object (caller must
// glDeleteProgram).
bool compileShaderProgram(const std::string& vertSrc,
                          const std::string& fragSrc,
                          GLuint& outProgram,
                          std::string& outLog);

// Convenience: returns the GLSL version preamble appropriate for the
// current build target.  Use it to prefix user-edited shader source if the
// user did not provide their own #version directive.
const char* defaultGlslVersion();
