#ifndef __COMMON_SOURCE_H__
#define __COMMON_SOURCE_H__

#ifdef __cplusplus
extern "C" {
#endif

/*
#ifdef __linux__
  #include "ws2811.h"
#else
  #include "fakeled.h"
#endif // __linux__
*/

#define M_PI           3.14159265358979323846
#define GRADIENT_N     100

enum SourceType {
    EMBERS,
    PERLIN,
    COLOR,
    N_SOURCE_TYPES
};

typedef struct SourceGradient
{
    ws2811_led_t colors[GRADIENT_N];
} SourceGradient;

typedef struct BasicSource
{
    int n_leds;
    int time_speed;
    SourceGradient gradient;
} BasicSource;


void BasicSource_init(BasicSource* basic_source, int n_leds, int time_speed, ws2811_led_t* colors, int* steps, int n_steps);
//void BasicSource_build_gradient(BasicSource *bs, ws2811_led_t* colors, int* steps, int n_steps);
float random_01();

#ifdef __cplusplus
}
#endif

#endif /* __COMMON_SOURCE_H__ */
