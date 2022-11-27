#define _CRT_SECURE_NO_WARNINGS

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


//TODO allow this to be called somehow and from/to colours set somehow
void render_blend_test(ws2811_t* ledstrip)
{
    int from = 0xFF0000;
    int to = 0x00FF00;
    const int row_length = 25;
    for (int i = 0; i < row_length; ++i) {
        double alpha = i / (double)row_length;
        int a = (int)(0xFF * alpha);
        //first row -- just alpha
        ledstrip->channel[0].leds[i] = a << 16 | a << 8 | a;
        //second row HSL blend
        ledstrip->channel[0].leds[i + row_length] = mix_rgb_alpha_over_hsl(from, alpha, to, 1.0 - alpha);
        //third row lightness preserving
        ledstrip->channel[0].leds[i + 2 * row_length] = mix_rgb_alpha_preserve_lightness(from, alpha, to, 1.0 - alpha);
        //fourth row direct blend
        ledstrip->channel[0].leds[i + 3 * row_length] = mix_rgb_alpha_direct(from, alpha, to, 1.0 - alpha);
        //fifth row through black
        ledstrip->channel[0].leds[i + 4 * row_length] = mix_rgb_alpha_through_black(from, alpha, to, 1.0 - alpha);
        //sixth row no blend
        ledstrip->channel[0].leds[i + 5 * row_length] = mix_rgb_alpha_no_blend(from, alpha, to, 1.0 - alpha);
        //seventh row alpha again
        ledstrip->channel[0].leds[i + 6 * row_length] = a << 16 | a << 8 | a;
        //eigth row -- black
        ledstrip->channel[0].leds[i + 7 * row_length] = 0;
    }
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

//msg = color?xxxxxx
void ColorSource_process_message(const char* msg)
{
    char* sep = strchr(msg, '?');
    if (sep == NULL)
    {
        printf("ColorSource: message does not contain target %s\n", msg);
        return;
    }
    if ((sep - msg) >= 32)
    {
        printf("ColorSource: target is too long or poorly formatted: %s\n", msg);
        return;
    }
    if ((strlen(sep + 1) >= 64))
    {
        printf("ColorSource: message too long or poorly formatted: %s\n", msg);
        return;
    }
    char target[32];
    char payload[64];
    strncpy(target, msg, sep - msg);
    strncpy(payload, sep + 1, 64);
    target[sep - msg] = 0x0;
    if (!strncasecmp(target, "color", 5))
    {
        int col;
        col = (int)strtol(payload, NULL, 16);
        color_source.basic_source.gradient.colors[0] = col;
        color_source.first_update = 0;
        printf("Switched colour in ColorSource to: %s = %x\n", payload, col);
    }
    else
        printf("ColorSource: Unknown target: %s, payload was: %s\n", target, payload);

}

void ColorSource_init(int n_leds, int time_speed, uint64_t current_time)
{
    BasicSource_init(&color_source.basic_source, n_leds, time_speed, source_config.colors[COLOR_SOURCE], current_time);
    color_source.first_update = 0;
}

void ColorSource_construct()
{
    BasicSource_construct(&color_source.basic_source);
    color_source.basic_source.update = ColorSource_update_leds;
    color_source.basic_source.init = ColorSource_init;
    color_source.basic_source.process_message = ColorSource_process_message;
}

ColorSource color_source = {
    .basic_source.construct = ColorSource_construct,
    .first_update = 0 
};
