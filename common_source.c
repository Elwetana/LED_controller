#define _CRT_SECURE_NO_WARNINGS

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

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
        return EMBERS;
    }
    else if (!strncasecmp("PERLIN", source, 6)) {
        return PERLIN;
    }
    else if (!strncasecmp("COLOR", source, 5)) {
        return COLOR;
    }
    else {
        printf("Unknown source");
        exit(-1);
    }
}

SourceConfig source_config = {
    .embers_colors = NULL,
    .perlin_colors = NULL,
    .color_colors = NULL
};

void SourceConfig_init(char* source_name, SourceColors* source_colors)
{
    enum SourceType source_type = string_to_SourceType(source_name);
    switch (source_type)
    {
    case EMBERS:
        source_config.embers_colors = source_colors;
        break;
    case PERLIN:
        source_config.perlin_colors = source_colors;
        break;
    case COLOR:
        source_config.color_colors = source_colors;
        break;
    case N_SOURCE_TYPES:
        printf("Unknown type");
        exit(-1);
        break;
    }
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
    SourceColors_destruct(source_config.embers_colors);
    SourceColors_destruct(source_config.perlin_colors);
    SourceColors_destruct(source_config.color_colors);
}

void read_config()
{
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
        int color, step;
        for (int i = 0; i < n_steps; i++)
        {
            n = sscanf(buf, "%x %i", &color, &step);
            if (n != 2) {
                printf("Error reading config -- colors\n");
                exit(-6);
            }
            sc->colors[i] = color;
            sc->steps[i] = step;
        }
        n = sscanf(buf, "%x", &color);
        if (n != 1) {
            printf("Error reading config -- colors end\n");
            exit(-6);
        }
        sc->colors[n_steps] = color;
        SourceConfig_init(name, sc);
    }
}
