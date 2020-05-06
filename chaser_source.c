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
#include "chaser_source.h"

static ChaserSource chaser_source = { .heads = { 19, 246, 0, 38, 76, 114, 152, 190, 227, 265, 303, 341, 379, 417 } };

void ChaserSource_init(int n_leds, int time_speed)
{
    BasicSource_init(&chaser_source.basic_source, n_leds, time_speed, source_config.colors[CHASER_SOURCE]);
}

void ChaserSource_destruct() {}

int ChaserSource_update_leds(int frame, ws2811_t* ledstrip)
{
    int mframe = frame % chaser_source.basic_source.n_leds;
    for (int i = 0; i < N_HEADS; i++)
    {
        //self.shift[i] += int(random() * 4 - 2)
        chaser_source.cur_heads[i] = (chaser_source.heads[i] + mframe) % chaser_source.basic_source.n_leds; //<- target leds
    }
    chaser_source.cur_heads[0] = (chaser_source.cur_heads[0] + (int)(0.5 * mframe)) % chaser_source.basic_source.n_leds;
    chaser_source.cur_heads[1] = (chaser_source.cur_heads[1] + (int)(0.5 * mframe)) % chaser_source.basic_source.n_leds;

    for (int led = 0; led < chaser_source.basic_source.n_leds; ++led)
    {
        int min_dist = chaser_source.basic_source.n_leds;
        int min_i = -1;
        for (int i = 0; i < N_HEADS; i++)
        {
            int head = chaser_source.cur_heads[i];
            int dist = (head - led) % chaser_source.basic_source.n_leds;
            if (dist < min_dist)
            {
                min_dist = dist;
                min_i = i;
            }
        }
        int y = 0;
        if (min_dist >= 0 && min_dist < 19)
        {
            if (min_i < 2)
                y = 38 - min_dist;
            else
                y = 19 - min_dist;
        }
        ledstrip->channel[0].leds[led] = chaser_source.basic_source.gradient.colors[y];
    }
    return 1;
}