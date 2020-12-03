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
	return (float)rand() / (float)RAND_MAX;
}

void BasicSource_build_gradient(BasicSource* basic_source, ws2811_led_t* colors, int* steps, int n_steps)
{
    int offset = 0;
    for (int i = 0; i < n_steps; i++)
    {
        fill_gradient(basic_source->gradient.colors, offset, colors[i], colors[i + 1], steps[i], GRADIENT_N - 1);
        offset += steps[i];
    }
    printf("Gradient initialized with %i colours\n", offset);
}

//returns 1 if leds were updated, 0 if update is not necessary
int BasicSource_update(int n_leds, ws2811_t* strip) 
{
    (void)n_leds;
    (void)strip;
    return 0;
}

void BasicSource_process_message(const char* msg)
{
    (void)msg;
}

int BasicSource_process_config(const char* name, const char* value)
{
    (void)name;
    (void)value;
    return 1;
}

void BasicSource_destruct() {}

void BasicSource_construct(BasicSource* basic_source)
{
    basic_source->update = BasicSource_update;
    basic_source->destruct = BasicSource_destruct;
    basic_source->process_message = BasicSource_process_message;
    basic_source->process_config = BasicSource_process_config;
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
