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
		float h; // <0,1>
		float s; // <0,1>
		float l; // <0,1>
	};
	float f[3];
} hsl_t;

ws2811_led_t alpha_blend_rgb(ws2811_led_t upper, ws2811_led_t lower, double upper_alpha);
ws2811_led_t multiply_rgb_color(ws2811_led_t rgb, double t);
/* Will not overflow white for t > 1 */
ws2811_led_t multiply_rgb_color_ratchet(ws2811_led_t rgb, double t);
ws2811_led_t mix_rgb_color(ws2811_led_t rgb1, ws2811_led_t rgb2, double t);
ws2811_led_t mix_rgb_alpha_over_hsl(int rgb1, double alpha1, int rgb2, double alpha2);
ws2811_led_t mix_rgb_alpha_direct(int rgb1, double alpha1, int rgb2, double alpha2);
ws2811_led_t mix_rgb_alpha_through_black(int rgb1, double alpha1, int rgb2, double alpha2);
ws2811_led_t mix_rgb_alpha_no_blend(int rgb1, double alpha1, int rgb2, double alpha2);
ws2811_led_t mix_rgb_alpha_preserve_lightness(int rgb1, double alpha1, int rgb2, double alpha2);
void rgb2hsl(ws2811_led_t rgb, hsl_t* hsl);
ws2811_led_t hsl2rgb(hsl_t* hsl);
void rgb2rgb_array(int rgb_in, double* rgb_out);
void test_rgb2hsl();

/*!
 * @brief Created gradient from `from_color` to `to_color` with `steps`
 *
 * Gradient from 0 to 6 with 3 steps and next_steps = 0 will be 0, 3, 6. With next_steps != 0 it will be 0, 2, 4
 * @param gradient		pointer to array that will be filled
 * @param offset		index into `gradient` of the first colour
 * @param from_color	left endpoint of gradient
 * @param to_color		right ednpoint of gradient, see `next_steps`
 * @param steps			how many steps to generate
 * @param next_steps	if 0, the to_color will be included in gradient, otherwise it will not and the gradient step will be smaller
 * @param max_index		max index of gradient, colours beyond this will be silently ignored
*/
void fill_gradient(ws2811_led_t* gradient, int offset, ws2811_led_t from_color, ws2811_led_t to_color, int steps, int next_steps, int max_index);
/*! \returns  hsl1 for t == 0 and hsl2 for t == 1 */
void lerp_hsl(const hsl_t* hsl1, const hsl_t* hsl2, const float t, hsl_t* hsl_out);
/*! \returns  rgb1 for t == 0 and rgb2 for t == 1 */
ws2811_led_t lerp_rgb(const ws2811_led_t rgb1, const ws2811_led_t rgb2, const float t);
void hsl_copy(const hsl_t* hsl_in, hsl_t* hsl_out);
void test_rgb2hsl();

#ifdef __cplusplus
}
#endif

#endif /* __COLOURS_H__ */
