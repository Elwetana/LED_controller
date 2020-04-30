#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef __linux__
    #include <time.h>
#else
    #include "faketime.h"
#endif
#include <math.h>

#include "ws2811.h"
#include "source.h"

int main()
{
    struct timespec start_time;
    clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &start_time);
    printf("tv %lu, ns %lu\n", start_time.tv_sec, start_time.tv_nsec);

    printf("start\n");
    //test_rgb2hsl();
    srand(time(NULL));
    FireSource fire_source =
    {
        .ember_data = {
            [0] = 
            {
                .amp = 0.4f,
                .amp_rand = 0.1f,
                .x_space = 100,
                .sigma = 30,
                .sigma_rand = 2,
                .osc_amp = 0.2f,
                .osc_freq = 0.005f,
                .osc_freq_rand = 0.01,
                .decay = 0.0,
                .decay_rand = 0
            },
            [1] =
            {
                .amp = 0.2f,
                .amp_rand = 0.05f,
                .x_space = 60.0f,
                .sigma = 9.0f,
                .sigma_rand = 2.0f,
                .osc_amp = 0.2f,
                .osc_freq = 0.01f,
                .osc_freq_rand = 0.005f,
                .decay = 0.0f,
                .decay_rand = 0.0f
            },
            [2] = 
            {
                .amp = 0.1f,
                .amp_rand = 0.2f,
                .x_space = 25.0f,
                .sigma = 3.0f,
                .sigma_rand = 1.0f,
                .osc_amp = 0.2f,
                .osc_freq = 0.01f,
                .osc_freq_rand = 0.01f,
                .decay = 0.001f,
                .decay_rand = 0.001f
            }
        }
    };
    init_FireSource(&fire_source, 453, 1);
 
    //init_Ember(&e, 10, &ed, 10, BIG);
    Ember* e = &(fire_source.embers[1]);
    printf("b %f  %f\n", e->cos_table[2], e->contrib_table[3][2]);

    struct timespec end_time;
    clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &end_time);
    long diffInMs = ((end_time.tv_sec - start_time.tv_sec) * (long)1e9 + (end_time.tv_nsec - start_time.tv_nsec)) / 1e6;
    printf("Time: %li ms", diffInMs);
}