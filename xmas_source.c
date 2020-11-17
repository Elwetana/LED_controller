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
#include "colours.h"
#include "faketime.h"
#include "xmas_source.h"

enum dir {
    UP,
    RIGHT = 2,
    DOWN = 4,
    LEFT = 6
};

#pragma region Snowflakes
typedef struct PeriodData {
    long lastChange;
    long nextChange;
    long basePeriod;
    long periodRange;
} period_data_t;

#define C_N_SNOWFLAKES 5
int snowflakes[] = { 10, 30, 50, 70, 90 };
period_data_t diff_data[C_N_SNOWFLAKES];
period_data_t spec_data[C_N_SNOWFLAKES];
const int hue = 210;
const double sat = 0.8;
double lgt = 0.5;
double k_diff = 0.5 / 2.;
double k_spec = 0.5 / 1.5;
double spec_phase = M_PI / 2;
double spec = 20;

void Snowflakes_init()
{
    for (int flake = 0; flake < C_N_SNOWFLAKES; ++flake)
    {
        diff_data[flake].basePeriod = 1500;
        diff_data[flake].periodRange = 500;
        spec_data[flake].basePeriod = 2000;
        spec_data[flake].periodRange = 500;
    }
}

double get_angle(period_data_t* period_data, long frame)
{
    if (frame >= period_data->nextChange)
    {
        period_data->lastChange = period_data->nextChange;
        period_data->nextChange = frame + period_data->basePeriod + (long)((random_01() - 0.5f) * period_data->periodRange);
    }
    return 2 * M_PI * (double)(frame - period_data->lastChange) / (double)(period_data->nextChange - period_data->lastChange);
}

#pragma endregion Snowflakes

//returns 1 if leds were updated, 0 if update is not necessary
int XmasSource_update_leds(int frame, ws2811_t* ledstrip)
{
    for (int led = 0; led < xmas_source.basic_source.n_leds; ++led)
    {
        ledstrip->channel[0].leds[led] = 0;
    }
    for (int flake = 0; flake < C_N_SNOWFLAKES; ++flake)
    {
        int led = snowflakes[flake];

        double diff_alpha = get_angle(&diff_data[flake], frame);
        double spec_alpha = get_angle(&spec_data[flake], frame);
        float hsl[3];
        hsl[0] = hue / 360.f;
        hsl[1] = (float)sat;
        double l = lgt + k_diff * cos(diff_alpha) + k_spec * pow(cos(spec_alpha + spec_phase), spec);
        hsl[2] = (float)l;

        ledstrip->channel[0].leds[led] = hsl2rgb(hsl);
        for (int dir = 0; dir < 8; dir += 2)
        {
            if ((xmas_source.geometry[flake][dir] != -1) && (xmas_source.geometry[flake][dir + 1] == 1))
            {
                int neighbor = xmas_source.geometry[flake][dir];
                hsl[2] = 0.5 * (float)pow(l, 2);
                ledstrip->channel[0].leds[neighbor] = hsl2rgb(hsl);
            }
        }
    }
    /*(void)frame;
    if (xmas_source.first_update > 0)
    {
        return 0;
    }
    for (int led = 0; led < xmas_source.basic_source.n_leds; ++led)
    {
        ledstrip->channel[0].leds[led] = led == xmas_source.led_index ? xmas_source.basic_source.gradient.colors[0] : 0;
    }
    xmas_source.first_update = 1;
    */
    return 1;
}

// The whole message is e.g. LED MSG MORSETEXT?HI%20URSULA
// This function will only receive the part after LED MSG
void XmasSource_process_message(const char* msg)
{
    char* sep = strchr(msg, '?');
    if (sep == NULL)
    {
        printf("Message does not contain target %s\n", msg);
        return;
    }
    if ((sep - msg) >= 32)
    {
        printf("Target is too long or poorly formatted: %s\n", msg);
        return;
    }
    if ((strlen(sep + 1) >= 64))
    {
        printf("Message too long or poorly formatted: %s\n", msg);
        return;
    }
    char target[32];
    char payload[64];
    strncpy(target, msg, sep - msg);
    strncpy(payload, sep + 1, 64);
    target[sep - msg] = 0x0;
    if (!strncasecmp(target, "MORSETEXT", 9))
    {
        xmas_source.led_index = atoi(payload);
        xmas_source.first_update = 0;
        printf("Debug info: %s\n", payload);
    }
    else if (!strncasecmp(target, "MORSEMODE", 9))
    {
        //int mode = atoi(payload);
        //MorseSource_change_mode(mode);
        //printf("Setting new MorseSource mode: %i\n", mode);
    }
    else
        printf("Unknown target: %s, payload was: %s\n", target, payload);
}

void XmasSource_read_geometry()
{
    xmas_source.geometry = malloc(xmas_source.basic_source.n_leds * sizeof(*xmas_source.geometry));
    FILE* fgeom = fopen("geometry", "r");
    if (fgeom == NULL) {
        printf("Geometry file not found\n");
        exit(-4);
    }
    int row = 0;
    while (row < xmas_source.basic_source.n_leds)
    {
        fscanf(fgeom, "%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\n", 
            &xmas_source.geometry[row][UP], &xmas_source.geometry[row][UP+1],
            &xmas_source.geometry[row][RIGHT], &xmas_source.geometry[row][RIGHT+1],
            &xmas_source.geometry[row][DOWN], &xmas_source.geometry[row][DOWN+1],
            &xmas_source.geometry[row][LEFT], &xmas_source.geometry[row][LEFT+1]);
        row++;
    }
}

void XmasSource_destruct()
{
    free(xmas_source.geometry);
}

void XmasSource_init(int n_leds, int time_speed)
{
    BasicSource_init(&xmas_source.basic_source, n_leds, time_speed, source_config.colors[COLOR_SOURCE]);
    xmas_source.basic_source.update = XmasSource_update_leds;
    xmas_source.basic_source.destruct = XmasSource_destruct;
    xmas_source.basic_source.process_message = XmasSource_process_message;
    xmas_source.first_update = 0;
    XmasSource_read_geometry();
}

XmasSource xmas_source = {
    .basic_source.init = XmasSource_init,
    .first_update = 0,
    .led_index = 0
};
