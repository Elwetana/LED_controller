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
#include "xmas_source.h"

//returns 1 if leds were updated, 0 if update is not necessary
int XmasSource_update_leds(int frame, ws2811_t* ledstrip)
{
    (void)frame;
    if (xmas_source.first_update > 0)
    {
        return 0;
    }
    for (int led = 0; led < xmas_source.basic_source.n_leds; ++led)
    {
        ledstrip->channel[0].leds[led] = led == xmas_source.led_index ? xmas_source.basic_source.gradient.colors[0] : 0;
    }
    xmas_source.first_update = 1;
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


void XmasSource_init(int n_leds, int time_speed)
{
    BasicSource_init(&xmas_source.basic_source, n_leds, time_speed, source_config.colors[COLOR_SOURCE]);
    xmas_source.first_update = 0;
    xmas_source.basic_source.update = XmasSource_update_leds;
    xmas_source.basic_source.process_message = XmasSource_process_message;
}

XmasSource xmas_source = {
    .basic_source.init = XmasSource_init,
    .first_update = 0,
    .led_index = 0
};
