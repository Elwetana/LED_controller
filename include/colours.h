#ifndef __COLOURS_H__
#define __COLOURS_H__

#ifdef __cplusplus
extern "C" {
#endif

#ifdef __linux__
  #include "ws2811.h"
#else
  #include "fakeled.h"
#endif // __linux__


#define float2int(c)   ((int)(c * 255 + 0.5 - FLOAT_ERROR))
#define FLOAT_ERROR    0.0000005
#define BIAS(x, w)     (x / ((((1.0f / w) - 2.0f) * (1.0f - x)) + 1.0f))
#define GAIN(x, w)     ((x < 0.5f) ? BIAS((x * 2.0f), w) / 2.0f : 1.0f - BIAS((2.0f - x * 2.0f), w) / 2.0f)

typedef union
{
	struct {
		float h;
		float s;
		float l;
	};
	float f[3];
} hsl_t;


void rgb2hsl(ws2811_led_t rgb, hsl_t* hsl);
ws2811_led_t hsl2rgb(hsl_t* hsl);
void test_rgb2hsl();
void fill_gradient(ws2811_led_t* gradient, int offset, ws2811_led_t from_color, ws2811_led_t to_color, int steps, int max_index);
void lerp_hsl(const hsl_t* hsl1, const hsl_t* hsl2, const float t, hsl_t* hsl_out);
void hsl_copy(const hsl_t* hsl_in, hsl_t* hsl_out);
void test_rgb2hsl();

#ifdef __cplusplus
}
#endif

#endif /* __COLOURS_H__ */
