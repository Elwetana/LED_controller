#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#ifdef __linux__
#include "ws2811.h"
#else
#include "fakeled.h"
#endif // __linux__

#include "common_source.h"
#include "color_source.h"

static ColorSource color_source = { .first_update = 0 };

void ColorSource_init(int n_leds, int time_speed)
{
    BasicSource_init(&color_source.basic_source, n_leds, time_speed, source_config.color_colors);
    color_source.first_update = 0;
}

void ColorSource_destruct()
{
}

//returns 1 if leds were updated, 0 if update is not necessary
int ColorSource_update_leds(int frame, ws2811_t* ledstrip)
{
    (void)frame;
    if (color_source.first_update > 0)
    {
        return 0;
    }
    for (int led = 0; led < color_source.basic_source.n_leds; ++led)
    {
        ledstrip->channel[0].leds[led] = color_source.basic_source.gradient.colors[0];
    }
    color_source.first_update = 1;
    return 1;
}
