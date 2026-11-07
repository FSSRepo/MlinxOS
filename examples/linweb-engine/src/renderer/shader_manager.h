#pragma once
#include "gl_wrapper.h"
#include <string>
#include <array>

namespace linweb {

GLuint compile_shader(GLenum type, const char* source);
GLuint link_program(GLuint vs, GLuint fs);
void use_program(GLuint program);
void set_uniform(GLint location, const std::array<float, 16>& matrix);
void set_uniform(GLint location, float value);
void set_uniform(GLint location, int value);
void set_uniform(GLint location, float x, float y);

} // namespace linweb
