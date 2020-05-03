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

#ifdef _MSC_VER
#define strncasecmp _strnicmp
#define strcasecmp _stricmp
#endif

enum SourceType {
    EMBERS,
    PERLIN,
    COLOR,
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
    SourceColors* perlin_colors;
    SourceColors* embers_colors;
    SourceColors* color_colors;
} SourceConfig;

extern SourceConfig source_config;

void BasicSource_init(BasicSource* basic_source, int n_leds, int time_speed, SourceColors* source_colors);
//void BasicSource_build_gradient(BasicSource *bs, ws2811_led_t* colors, int* steps, int n_steps);
float random_01();
enum SourceType string_to_SourceType(char*);
void read_config();
void SourceConfig_init(char* source_name, SourceColors* source_colors);
void SourceConfig_destruct();
void SourceColors_destruct(SourceColors* source_colors);

#ifdef __cplusplus
}
#endif

#endif /* __COMMON_SOURCE_H__ */
