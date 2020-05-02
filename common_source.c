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

void init_BasicSource(BasicSource* basic_source, int n_leds, int time_speed)
{
    basic_source->n_leds = n_leds;
    basic_source->time_speed = time_speed;
}

void build_gradient(SourceGradient* sg, ws2811_led_t* colors, int* steps, int n_steps)
{
    int offset = 0;
    for (int i = 0; i < n_steps; i++)
    {
        fill_gradient(sg->colors, offset, colors[i], colors[i + 1], steps[i], GRADIENT_N - 1);
        offset += steps[i];
    }
}
