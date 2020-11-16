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
#include "perlin_source.h"
#include "colours.h"


static void PerlinSource_build_noise()
{
    for (int f = 0; f < PERLIN_FREQ_N; ++f)
    {
        perlin_source.noise[f] = (struct noise_t*)malloc(sizeof(struct noise_t) * 100); // perlin_source.noise_freq[f]);
        for (int i = 0; i < perlin_source.noise_freq[f]; ++i)
        {
            perlin_source.noise[f][i].amplitude = 2.0f * random_01() - 1.0f;
            perlin_source.noise[f][i].phase = 2.0f * random_01() * (float)M_PI;
        }
    }
}

/*
Because the weights are - 1.. + 1, the value in the middle of the interval is 0.5 at most, therefore
the result is in range - 0.5.. + 0.5
*/
static double PerlinSource_sample_noise(int freq, double x, float p, int frame)
{
    struct noise_t* noise = perlin_source.noise[freq];
    int i = (int)x;
    double dx = x - i;
    double n0 = dx * noise[i].amplitude * cosf(frame * p + noise[i].phase); 
    double n1 = (dx - 1) * noise[i + 1].amplitude * cosf(frame * p + noise[i + 1].phase);
    double w = dx * dx * (3 - 2 * dx);  // 3 dx ^ 2 - 2 dx ^ 3
    /*
    2(a - b)x - (3a - 5b)x - 3bx + ax  https ://eev.ee/blog/2016/05/29/perlin-noise/
    ax(1 - 3x ^ 2 + 2x ^ 3) + b(x - 1)(3x ^ 2 - 2x ^ 3)
    2ax ^ 4 - 2bx ^ 4 - 3ax ^ 3 + 3bx ^ 3 + 2bx ^ 3 - 3bx ^ 2 + ax
    */
    double n = n0 * (1 - w) + n1 * w;
    // print("% 2.4f\t% 2.4f\t% 2.4f\t% 2.4f" % (x, n0, n1, n))
    return n + 0.5;
}

static int PerlinSource_get_gradient_index(int led, int frame)
{
    double y = 0;
    for (int f = 0; f < PERLIN_FREQ_N; ++f)
    {
        float freq = (float)perlin_source.noise_freq[f];
        double x = led * (freq - 2.0) / perlin_source.basic_source.n_leds + 0.5;
        y += PerlinSource_sample_noise(f, x, freq / 1000.0f, perlin_source.basic_source.time_speed * frame) * perlin_source.noise_weight[f];
    }
    return (int)(GRADIENT_N * GAIN(y, 0.1));
}

int PerlinSource_update_leds(int frame, ws2811_t* ledstrip)
{
    for (int led = 0; led < perlin_source.basic_source.n_leds; ++led)
    {
        int y = PerlinSource_get_gradient_index(led, frame);
        ledstrip->channel[0].leds[led] = perlin_source.basic_source.gradient.colors[y];
    }
    return 1;
}

void PerlinSource_destruct()
{
    for (int f = 0; f < PERLIN_FREQ_N; ++f)
    {
        free(perlin_source.noise[f]);
    }
}

void PerlinSource_init(int n_leds, int time_speed)
{
    BasicSource_init(&perlin_source.basic_source, n_leds, time_speed, source_config.colors[PERLIN_SOURCE]);
    perlin_source.basic_source.update = PerlinSource_update_leds;
    perlin_source.basic_source.destruct = PerlinSource_destruct;
    PerlinSource_build_noise();
}

PerlinSource perlin_source =
{
    .basic_source.init = PerlinSource_init,
    .noise_freq = { 5, 11, 23, 47 },
    .noise_weight = { 8.0 / 15.0, 4.0 / 15.0, 2.0 / 15.0, 1.0 / 15.0 }
};
