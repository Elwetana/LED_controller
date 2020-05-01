#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ws2811.h"

FILE* _fake_led_output;
char line[2048]; //TODO: it would be better to allocate in init, based on number of LEDs

ws2811_return_t ws2811_init(ws2811_t *ws2811)
{
    ws2811_channel_t *channel = &ws2811->channel[0];
    channel->leds = malloc(sizeof(ws2811_led_t) * channel->count);
    fopen_s(&_fake_led_output, "led_output.csv", "w");
    return WS2811_SUCCESS;
}

void ws2811_fini(ws2811_t *ws2811) 
{
    fclose(_fake_led_output);
}

ws2811_return_t ws2811_render(ws2811_t *ws2811)
{
    /*strcpy_s(line, 1, "");
    ws2811_channel_t *channel = &ws2811->channel[0];
    for(int i = 0; i < channel->count; ++i)
    {
        char s[12];
        sprintf(s, "%x,", channel->leds[i]);
        strcat_s(line, strlen(s) + 1, s);
    }
    strcat_s(line,2, "\n");
    fputs(line, _fake_led_output);*/
    return WS2811_SUCCESS;
}

const char * ws2811_get_return_t_str(const ws2811_return_t state)
{
    return "x";
}
