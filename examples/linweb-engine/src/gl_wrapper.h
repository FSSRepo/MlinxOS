#pragma once

#ifdef WESTON_PLATFORM
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
// GLES2 has no sized formats: glTexImage2D wants internalformat == format, so the RGBA8 of desktop GL is plain RGBA
#ifndef GL_RGBA8
#define GL_RGBA8 GL_RGBA
#endif
#else
#include <glad/glad.h>
#endif
