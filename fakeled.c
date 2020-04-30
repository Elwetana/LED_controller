#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "ws2811.h"

ws2811_return_t ws2811_init(ws2811_t *ws2811)
{
    ws2811_channel_t *channel = &ws2811->channel[0];
    channel->leds = malloc(sizeof(ws2811_led_t) * channel->count);
    return WS2811_SUCCESS;
}

void ws2811_fini(ws2811_t *ws2811) 
{

}

ws2811_return_t ws2811_render(ws2811_t *ws2811)
{
    return WS2811_SUCCESS;
}

const char * ws2811_get_return_t_str(const ws2811_return_t state)
{
    return "x";
}
