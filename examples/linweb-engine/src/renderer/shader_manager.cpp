#include "renderer/shader_manager.h"
#include <iostream>
#include <algorithm>

namespace linweb {

GLuint compile_shader(GLenum type, const char* source) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        GLint log_len = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &log_len);
        std::string log(static_cast<size_t>(std::max(0, log_len)), '\0');
        if (log_len > 0) glGetShaderInfoLog(shader, log_len, nullptr, log.data());
        std::cerr << "Shader compile failed: " << log << std::endl;
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

GLuint link_program(GLuint vs, GLuint fs) {
    GLuint program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glLinkProgram(program);

    GLint ok = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (!ok) {
        GLint log_len = 0;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &log_len);
        std::string log(static_cast<size_t>(std::max(0, log_len)), '\0');
        if (log_len > 0) glGetProgramInfoLog(program, log_len, nullptr, log.data());
        std::cerr << "Program link failed: " << log << std::endl;
        glDeleteProgram(program);
        return 0;
    }
    return program;
}

void use_program(GLuint program) {
    glUseProgram(program);
}

void set_uniform(GLint location, const std::array<float, 16>& matrix) {
    if (location >= 0) glUniformMatrix4fv(location, 1, GL_FALSE, matrix.data());
}

void set_uniform(GLint location, float value) {
    if (location >= 0) glUniform1f(location, value);
}

void set_uniform(GLint location, int value) {
    if (location >= 0) glUniform1i(location, value);
}

void set_uniform(GLint location, float x, float y) {
    if (location >= 0) glUniform2f(location, x, y);
}

} // namespace linweb
