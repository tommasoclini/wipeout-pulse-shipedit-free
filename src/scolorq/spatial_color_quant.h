#pragma once

#ifdef __cplusplus
extern "C" {
#endif

int
spatial_color_quant_inplace(int width, int height, unsigned char *rgb_pixels, int num_colors);

#ifdef __cplusplus
}
#endif
