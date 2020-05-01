#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include "ws2811.h"

#include "fire_source.h"
#include "colours.h"

#define M_PI           3.14159265358979323846  /* pi */

float random_01()
{
    return (double)rand() / (double)RAND_MAX;
}


void init_Ember(Ember* e, int i, EmberData* ember_data, int age, enum EmberType ember_type) 
{
    float amp = ember_data->amp + random_01() * ember_data->amp_rand;
    e->i = i;
    e->x = (i - 1.0 + random_01()/3.0) * ember_data->x_space;
    e->amp = amp;
    e->osc_amp = amp * ember_data->osc_amp;
    e->osc_freq = ember_data->osc_freq + random_01() * ember_data->osc_freq_rand;
    e->osc_shift = random_01() * 2 * M_PI;
    e->sigma = ember_data->sigma + random_01() * ember_data->sigma_rand;
    e->decay = ember_data->decay + random_01() * ember_data->decay_rand;
    e->age = age;
    e->type = ember_type;
    e->cos_table_length = (2 * M_PI) / e->osc_freq;
    //printf(.cos %i\n., cos_table_length);
    e->cos_table = (float*) malloc(sizeof(float) * e->cos_table_length);
    e->contrib_table = (float**) malloc(sizeof(float*) * e->cos_table_length);
    for(int t = 0; t < e->cos_table_length; ++t)
    {
        float tcos = cosf(t * e->osc_freq + e->osc_shift);
        e->cos_table[t] = tcos;
        e->contrib_table[t] = (float*) malloc(sizeof(float) * 12 * e->sigma);
        for(int x = -6 * e->sigma; x < 6 * e->sigma; ++x)
        {
            float osc = e->osc_amp * tcos;
            int x_index = x + 6 * e->sigma;
            float f = (x / e->sigma * e->amp / (e->amp + osc)); 
            e->contrib_table[t][x_index] = (e->amp + osc) * expf(-0.5 * f * f);
        }
    }
}

void destruct_Ember(Ember* e)
{
    for(int i = 0; i < e->cos_table_length; ++i)
    {
        free(e->contrib_table[i]);
    }
    free(e->contrib_table);
    free(e->cos_table);
}

float get_Ember_contrib(Ember* e, int x, int t)
{
    int cos_t = t % e->cos_table_length;
    int dx = (int)e->x - x + (int)(6.0f * e->sigma);
    if(e->decay == 0)
        return e->contrib_table[cos_t][dx];
    else
        return e->contrib_table[cos_t][dx] * exp(-0.5 * e->decay * (e->age - t) * (e->age - t));
}

void build_embers(FireSource* fs)
{
    int n_embers = 0;
    for(int ember_type = 0; ember_type < N_EMBER_TYPES; ++ember_type)
    {
        fs->n_embers_per_type[ember_type] = 1 + (fs->n_leds + 1) / fs->ember_data[ember_type].x_space;
        n_embers += fs->n_embers_per_type[ember_type];
    }
    fs->embers = (Ember*) malloc(sizeof(Ember) * n_embers);
    n_embers = 0;
    for(int ember_type = 0; ember_type < N_EMBER_TYPES; ++ember_type)
    {
        //TODO: it would be better if x-coordinate is passed to constructor
        //int x = (n_embers_per_type[ember_type] * fs->ember_data[ember_type]->x_size - fs->n_leds) / 2;
        for(int i = 0; i < fs->n_embers_per_type[ember_type]; ++i)
        {
            init_Ember(&(fs->embers[n_embers + i]), i, &(fs->ember_data[ember_type]), 0, ember_type);
        }
        n_embers += fs->n_embers_per_type[ember_type];
    }
    fs->n_embers = n_embers;
}

//TODO: this is not very nice, the colours should be somewhere more convenient
void build_gradient(FireSource* fs)
{
    ws2811_led_t colors[] = {0x110000, 0xBF2100, 0xFFB20F, 0xFFFFAF};
    int steps[] = {40, 50, 51};
    int offset = 0;
    for(int i = 0; i < 3; i++)
    {
        fill_gradient(fs->gradient, offset, colors[i], colors[i+1], steps[i], 99);
        offset += steps[i];
    }
}

void init_FireSource(int n_leds, int time_speed)
{
    fire_source.n_leds = n_leds;
    fire_source.time_speed = time_speed;
    build_gradient(&fire_source);
    build_embers(&fire_source);
}

void destruct_FireSource()
{
    for(int i = 0; i < fire_source.n_embers; ++i)
    {
        destruct_Ember(&(fire_source.embers[i]));
    }
    free(fire_source.embers);
}

void update_embers(FireSource* fs, int frame)
{
    int spark_offset = 0;
    int spark_count = 0;
    int i = 0;
    do {
        spark_offset += fs->n_embers_per_type[i];
    } while(++i != SPARK);
    spark_count = fs->n_embers_per_type[i];
    for(i = spark_offset; i < spark_offset + spark_count; i++)
    {
        Ember* e = &(fs->embers[i]);
        if(e->age < frame && e->decay * (e->age - frame) * (e->age - frame) > 10.0f)
        {
            //printf("replacing ember")
            int ei = e->i;
            destruct_Ember(e);
            init_Ember(e, ei, &(fs->ember_data[SPARK]), frame, SPARK);
        }
    }
}

int get_gradient_index(FireSource* fs, int led, int frame)
{
    float y = 0.0f;
    for(int ember = 0; ember < fs->n_embers; ember++)
    {
        Ember* e = &(fs->embers[ember]);
        if(fabs(led - e->x) < 6.0f * e->sigma)
        {
	    float contrib = get_Ember_contrib(e, led, fs->time_speed * frame);
            y += contrib; 
        }
    }
    if (y > 1) y = 1.0f;
    y = GAIN(y, 0.25);
    return (int)(100 * y);
}

void update_leds_FireSource(int frame, ws2811_t* ledstrip)
{
    if(frame % 4 == 0)
    {
        update_embers(&fire_source, fire_source.time_speed * frame);
    }
    for(int led = 0; led < fire_source.n_leds; ++led)
    {
        int y = get_gradient_index(&fire_source, led, frame);
        ledstrip->channel[0].leds[led] = fire_source.gradient[y];
    }
}

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