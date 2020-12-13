#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <assert.h>
#ifdef __linux__
#include "ws2811.h"
#include <unistd.h>
#include <linux/input.h>
#include <fcntl.h>
#else
#include "fakeled.h"
#endif // __linux__

#include "common_source.h"
#include "colours.h"
#include "game_source.h"
#include "game_object.h"
#include "moving_object.h"
#include "player_object.h"
#include "pulse_object.h"


/*!
 * @brief Generate blinking, pulsing and so on
 * Use equation: t =  A * ((1. + cos(f * (t - t0) + phi + led * phi_led)) / 2.) ^ k;
 * Then use t to lerp between colors0 to colors1
 */
typedef struct PulseObject
{
    int index;
    uint64_t start_time;    //!< in ms
    uint64_t end_time;      //!< also in ms, end_time > start_time
    int repetitions;		//!< for ONCE and FADE modes how many times to repeat the animation. For STEADY if -1, 
                            //   it means the colors have been set; interpolation goes from 0 to 1 in odd cycles, from 1 to 0 in even
    int cur_cycle;          //!< current repetion cycle
    enum PulseModes pulse_mode;
    double amplitude;		//!< this should be <0, 1>
    double frequency;       //!< f = 2*pi/t_period
    double phase;           //!< phi
    double led_phase;		//!< phi_led
    double spec_exponent;   //!< k
    hsl_t colors_0[MAX_OBJECT_LENGTH];
    hsl_t colors_1[MAX_OBJECT_LENGTH];
    ws2811_led_t next_color[MAX_OBJECT_LENGTH];
    void (*on_end)(int);
} pulse_object_t;

static pulse_object_t pulse_objects[MAX_N_OBJECTS];


static uint64_t get_time_ms()
{
    return game_source.basic_source.current_time / (long)1e3 / (long)1e3;
}

static void PulseObject_update_steady(pulse_object_t* po)
{
    MovingObject_apply_colour(po->index, po->next_color);
    po->repetitions = -1;
}

static void PulseObject_check_pulse_end(pulse_object_t* po)
{
    assert(po->pulse_mode == PM_FADE || po->pulse_mode == PM_ONCE || po->pulse_mode == PM_REPEAT);
    uint64_t cur_time = get_time_ms();
    if (cur_time < po->end_time)
    {
        return;
    }
    ++po->cur_cycle;
    if (po->pulse_mode == PM_ONCE || po->pulse_mode == PM_FADE)
    {
        if (po->cur_cycle > po->repetitions)
        {
            po->pulse_mode = PM_STEADY;
            if (po->on_end) po->on_end(po->index);
            return;
        }
    }
    if (po->pulse_mode == PM_FADE)
    {
        po->amplitude /= 1.5;
    }
    //f = 2*pi/t_period => t_period = 2 * pi / f -- out frequence is 2x slower, odd + even repetition is the whole range
    po->start_time = cur_time;
    po->end_time = cur_time + (uint64_t)(M_PI / po->frequency);
}

static double PulseObject_get_t(pulse_object_t* po, uint64_t time_ms, int led)
{
    int odd = po->cur_cycle % 2;
    return po->amplitude * pow((1. - cos(M_PI * (odd - 1) + po->frequency * (time_ms - po->start_time) + po->phase + po->led_phase * led)) / 2., po->spec_exponent);
}

static void PulseObject_update_pulse(pulse_object_t* po)
{
    uint64_t time_ms = get_time_ms();
    double t = PulseObject_get_t(po, time_ms, 0);
    //printf("t %f, n %lli\n", t, time_ms);
    int length = MovingObject_get_length(po->index);
    ws2811_led_t result[MAX_OBJECT_LENGTH];
    for (int i = 0; i < length; ++i)
    {
        hsl_t res;
        lerp_hsl(&po->colors_0[i], &po->colors_1[i], t, &res);
        result[i] = hsl2rgb(&res);
    }
    MovingObject_apply_colour(po->index, result);
}

static void PulseObject_update_pulse_per_led(pulse_object_t* po)
{
    uint64_t time_ms = get_time_ms();
    int length = MovingObject_get_length(po->index);
    ws2811_led_t result[MAX_OBJECT_LENGTH];
    for (int i = 0; i < length; ++i)
    {
        double t = PulseObject_get_t(po, time_ms, i);
        printf("t: %f, time: %lli\n", t, time_ms);
        hsl_t res;
        lerp_hsl(&po->colors_0[i], &po->colors_1[i], t, &res);
        result[i] = hsl2rgb(&res);
    }
    MovingObject_apply_colour(po->index, result);
}

void PulseObject_update(int pi)
{
    pulse_object_t* po = &pulse_objects[pi];
    switch (po->pulse_mode)
    {
    case PM_STEADY:
        if (po->repetitions != -1)
        {
            PulseObject_update_steady(po);
        }
        break;
    case PM_REPEAT:
    case PM_ONCE:
    case PM_FADE:
        if (po->led_phase != 0)
        {
            PulseObject_update_pulse_per_led(po);
        }
        else
        {
            PulseObject_update_pulse(po);
        }
        PulseObject_check_pulse_end(po);
        break;
    }
}

void PulseObject_init(int pi, double amp, enum PulseModes pm, int repetitions, int period, double phase, double led_phase, double spec, void(*on_end)(int))
{
    pulse_object_t* po = &pulse_objects[pi];
    po->amplitude = amp;
    po->pulse_mode = pm;
    po->repetitions = repetitions;
    po->cur_cycle = 1;
    po->frequency =  M_PI / (double)period;
    po->phase = phase;
    po->led_phase = led_phase;
    po->spec_exponent = spec;

    po->start_time = get_time_ms();
    po->end_time = po->start_time + period;
    po->on_end = on_end;
}

void PulseObject_set_color_all(int pi, int color_index_0, int color_index_1, int next_color, int length)
{
    assert(length <= MAX_OBJECT_LENGTH);
    pulse_object_t* po = &pulse_objects[pi];
    hsl_t res0, res1;
    rgb2hsl(game_source.basic_source.gradient.colors[color_index_0], &res0);
    rgb2hsl(game_source.basic_source.gradient.colors[color_index_1], &res1);
    for (int i = 0; i < length; ++i)
    {
        po->colors_0[i] = res0;
        po->colors_1[i] = res1;
        po->next_color[i] = game_source.basic_source.gradient.colors[next_color];
    }
}

void PulseObject_set_color(int pi, int color0, int color1, int color_next, int led)
{
    pulse_object_t* po = &pulse_objects[pi];
    rgb2hsl(game_source.basic_source.gradient.colors[color0], po->colors_0 + led);
    rgb2hsl(game_source.basic_source.gradient.colors[color1], po->colors_1 + led);
    po->next_color[led] = game_source.basic_source.gradient.colors[color_next];
}

void PulseObject_init_steady(int pi, int color_index, int length)
{
    pulse_object_t* po = &pulse_objects[pi];
    po->index = pi;
    po->pulse_mode = PM_STEADY;
    for (int i = 0; i < length; ++i)
    {
        po->next_color[i] = game_source.basic_source.gradient.colors[color_index];
    }
    po->on_end = NULL;
}
