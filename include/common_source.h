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
    XMAS_SOURCE,
    GAME_SOURCE,
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
    uint64_t current_time; //in ns
    uint64_t time_delta;
    void(*construct)();
    void(*init)(int, int, uint64_t);
    int(*update)(int, ws2811_t*);
    void(*destruct)();
    void(*process_message)(const char*);
    int(*process_config)(const char*, const char*);
} BasicSource;

typedef struct SourceConfig {
    SourceColors** colors;
} SourceConfig;

extern SourceConfig source_config;

void BasicSource_construct(BasicSource* basic_source);
void BasicSource_init(BasicSource* basic_source, int n_leds, int time_speed, SourceColors* source_colors, uint64_t current_time);
float random_01();


#endif /* __COMMON_SOURCE_H__ */
