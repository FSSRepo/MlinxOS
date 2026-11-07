#pragma once
#include <stdint.h>

// I420 (planar YUV 4:2:0) to packed RGB888, with a bilinear upsampling of the chroma planes.
void yuv420rgb_multi_buffer(const uint8_t* y_p, const uint8_t* u_p, const uint8_t* v_p, uint8_t* rgb, int width, int height);
