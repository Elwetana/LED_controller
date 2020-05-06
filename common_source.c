#define _CRT_SECURE_NO_WARNINGS

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __linux__
#include "ws2811.h"
#else
#include "fakeled.h"
#endif // __linux__
#include "colours.h"
#include "common_source.h"

float random_01()
{
	return (double)rand() / (double)RAND_MAX;
}

void BasicSource_build_gradient(BasicSource* basic_source, ws2811_led_t* colors, int* steps, int n_steps)
{
    int offset = 0;
    for (int i = 0; i < n_steps; i++)
    {
        fill_gradient(basic_source->gradient.colors, offset, colors[i], colors[i + 1], steps[i], GRADIENT_N - 1);
        offset += steps[i];
    }
}

void BasicSource_init(BasicSource* basic_source, int n_leds, int time_speed, SourceColors* source_colors)
{
    ws2811_led_t* colors = source_colors->colors;
    int* steps = source_colors->steps;
    int n_steps = source_colors->n_steps;
    basic_source->n_leds = n_leds;
    basic_source->time_speed = time_speed;
    BasicSource_build_gradient(basic_source, colors, steps, n_steps);
}

enum SourceType string_to_SourceType(char* source)
{
    if (!strncasecmp("EMBERS", source, 6)) {
        return EMBERS_SOURCE;
    }
    else if (!strncasecmp("PERLIN", source, 6)) {
        return PERLIN_SOURCE;
    }
    else if (!strncasecmp("COLOR", source, 5)) {
        return COLOR_SOURCE;
    }
    else if (!strncasecmp("CHASER", source, 6)) {
        return CHASER_SOURCE;
    }
    else {
        printf("Unknown source");
        exit(-1);
    }
}

SourceConfig source_config;

static void SourceConfig_init()
{
    source_config.colors = malloc(sizeof(SourceColors*) * N_SOURCE_TYPES);
}

void SourceConfig_add_color(char* source_name, SourceColors* source_colors)
{
    enum SourceType source_type = string_to_SourceType(source_name);
    source_config.colors[source_type] = source_colors;
}

void SourceColors_destruct(SourceColors* source_colors)
{
    if (source_colors)
    {
        free(source_colors->colors);
        free(source_colors->steps);
    }
    free(source_colors);
}

void SourceConfig_destruct()
{
    for (int i = 0; i < N_SOURCE_TYPES; ++i)
    {
        SourceColors_destruct(source_config.colors[i]);
    }
    free(source_config.colors);
}

void read_config()
{
    SourceConfig_init();
    FILE* config = fopen("config", "r");
    if (config == NULL) {
        printf("Config not found\n");
        exit(-4);
    }
    char buf[255];
    while (fgets(buf, 255, config) != NULL)
    {
        SourceColors* sc = malloc(sizeof(SourceColors));
        char name[16];
        int n_steps;
        int n = sscanf(buf, "%s %i", name, &n_steps);
        if (n != 2) {
            printf("Error reading config -- source name\n");
            exit(-5);
        }
        printf("Reading config for: %s\n", name);

        sc->colors = malloc(sizeof(ws2811_led_t) * (n_steps + 1));
        sc->steps = malloc(sizeof(int) * n_steps);
        sc->n_steps = n_steps;
        fgets(buf, 255, config);
        int color, step, offset;
        char* line = buf;
        offset = 0;
        for (int i = 0; i < n_steps; i++)
        {
            n = sscanf(line, "%x %i%n", &color, &step, &offset);
            if (n != 2) {
                printf("Error reading config -- colors\n");
                exit(-6);
            }
            sc->colors[i] = color;
            sc->steps[i] = step;
            line += offset;
        }
        n = sscanf(line, "%x", &color);
        if (n != 1) {
            printf("Error reading config -- colors end\n");
            exit(-6);
        }
        sc->colors[n_steps] = color;
        SourceConfig_add_color(name, sc);
    }
}
