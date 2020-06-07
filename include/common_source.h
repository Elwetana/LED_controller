#ifndef __COMMON_SOURCE_H__
#define __COMMON_SOURCE_H__


#define M_PI           3.14159265358979323846
#define GRADIENT_N     100

#ifdef _MSC_VER
#define strncasecmp _strnicmp
#define strcasecmp _stricmp
#endif

enum SourceType {
    EMBERS_SOURCE,
    PERLIN_SOURCE,
    COLOR_SOURCE,
    CHASER_SOURCE,
    MORSE_SOURCE,
    DISCO_SOURCE,
    N_SOURCE_TYPES
};

typedef struct SourceColors
{
    int n_steps;
    ws2811_led_t* colors;
    int* steps;
} SourceColors;

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

typedef struct SourceConfig {
    SourceColors** colors;
} SourceConfig;

typedef struct SourceFunctions {
    void(*init)(int, int);
    int(*update)(int, ws2811_t*);
    void(*destruct)();
} SourceFunctions;

extern SourceConfig source_config;

void BasicSource_init(BasicSource* basic_source, int n_leds, int time_speed, SourceColors* source_colors);
//void BasicSource_destruct();
//void BasicSource_build_gradient(BasicSource *bs, ws2811_led_t* colors, int* steps, int n_steps);
float random_01();


#endif /* __COMMON_SOURCE_H__ */
