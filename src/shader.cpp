#include "shader.h"

#include <vector>
#include <cstdio>

const char* defaultGlslVersion() {
#if defined(IMGUI_IMPL_OPENGL_ES2) || defined(__EMSCRIPTEN__)
    return "#version 100\nprecision mediump float;\n";
#else
    // Query the active GL context and derive the matching GLSL version string.
    //
    // Mapping (from the GLSL spec):
    //   GL 2.0 → GLSL 110 | GL 2.1 → 120
    //   GL 3.0 → 130 | 3.1 → 140 | 3.2 → 150
    //   GL 3.3 → 330 | 4.0 → 400 | 4.1 → 410 | …  (major*100 + minor*10)
    static char buf[32];
    GLint major = 0, minor = 0;
    glGetIntegerv(GL_MAJOR_VERSION, &major);
    glGetIntegerv(GL_MINOR_VERSION, &minor);

    int glsl;
    if (major > 3 || (major == 3 && minor >= 3))
        glsl = major * 100 + minor * 10;   // 330, 400, 410, …
    else if (major == 3)
        glsl = 130 + minor * 10;           // 130, 140, 150
    else
        glsl = 100 + minor * 10 + 10;      // GL 2.0→110, 2.1→120

    snprintf(buf, sizeof(buf), "#version %d\n", glsl);
    return buf;
#endif
}

static bool compileOne(GLenum type, const std::string& src, GLuint& outShader, std::string& outLog) {
    GLuint sh = glCreateShader(type);
    const char* csrc = src.c_str();
    // Pass nullptr for the length array so OpenGL treats the source as
    // null-terminated.  This avoids "premature EOF" if the std::string's
    // size() ever diverges from the actual text content (e.g. after ImGui
    // writes directly into the buffer without updating size()).
    glShaderSource(sh, 1, &csrc, nullptr);
    glCompileShader(sh);

    GLint ok = 0;
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        GLint logLen = 0;
        glGetShaderiv(sh, GL_INFO_LOG_LENGTH, &logLen);
        std::vector<char> buf(logLen > 1 ? logLen : 1);
        glGetShaderInfoLog(sh, (GLsizei)buf.size(), nullptr, buf.data());
        outLog += (type == GL_VERTEX_SHADER) ? "[vertex] " : "[fragment] ";
        outLog += buf.data();
        outLog += "\n";
        glDeleteShader(sh);
        return false;
    }
    outShader = sh;
    return true;
}

bool compileShaderProgram(const std::string& vertSrc,
                          const std::string& fragSrc,
                          GLuint& outProgram,
                          std::string& outLog) {
    outLog.clear();
    GLuint vs = 0, fs = 0;
    if (!compileOne(GL_VERTEX_SHADER, vertSrc, vs, outLog)) return false;
    if (!compileOne(GL_FRAGMENT_SHADER, fragSrc, fs, outLog)) {
        glDeleteShader(vs);
        return false;
    }

    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    // Bind attribute locations to fixed slots so the renderer doesn't need
    // to query them — works the same on GL 3.0 core and WebGL 1.
    glBindAttribLocation(prog, 0, "aPos");
    glBindAttribLocation(prog, 1, "aNormal");
    glLinkProgram(prog);

    GLint ok = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        GLint logLen = 0;
        glGetProgramiv(prog, GL_INFO_LOG_LENGTH, &logLen);
        std::vector<char> buf(logLen > 1 ? logLen : 1);
        glGetProgramInfoLog(prog, (GLsizei)buf.size(), nullptr, buf.data());
        outLog += "[link] ";
        outLog += buf.data();
        outLog += "\n";
        glDeleteShader(vs);
        glDeleteShader(fs);
        glDeleteProgram(prog);
        return false;
    }

    glDetachShader(prog, vs);
    glDetachShader(prog, fs);
    glDeleteShader(vs);
    glDeleteShader(fs);

    outProgram = prog;
    return true;
}
