#ifndef __COLOURS_H__
#define __COLOURS_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "ws2811.h"

#define float2int(c)   ((int)(c * 255 + 0.5 - FLOAT_ERROR))
#define FLOAT_ERROR    0.0000005
#define BIAS(x, w)     (x / ((((1.0 / w) - 2.0) * (1.0 - x)) + 1.0))
#define GAIN(x, w)     ((x < 0.5) ? BIAS((x * 2.0), w) / 2.0 : 1.0 - BIAS((2.0 - x * 2.0), w) / 2.0)

void rgb2hsl(ws2811_led_t rgb, float* hsl);
ws2811_led_t hsl2rgb(float* hsl);
void test_rgb2hsl();
void fill_gradient(ws2811_led_t* gradient, int offset, ws2811_led_t from_color, ws2811_led_t to_color, int steps, int max_index);
void test_rgb2hsl();

#ifdef __cplusplus
}
#endif

#endif /* __COLOURS_H__ */
