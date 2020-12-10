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
#include "moving_object.h"
#include "player_object.h"
#include "pulse_object.h"
#include "game_source_priv.h"
#include "game_source.h"


enum PulseModes
{
    PM_STEADY,  //!< no blinking, copy next_color to moving_object.color
    PM_REPEAT,  //!< keep pulsing with the same parameter
    PM_ONCE,    //!< switch to normal steady light after repetions cycles switch to steady and execute callback
    PM_FADE     //!< decrease amplitude at end, after repetions cycles switch to steady and execute callback
};

void PulseObject_update_steady(game_object_t* o)
{
    for (int i = 0; i < o->body.length; ++i)
    {
        o->body.color[i] = o->pulse.next_color[i];
    }
    o->pulse.repetitions = -1;
}

void PulseObject_check_pulse_end(pulse_object_t* po, game_object_t* go)
{
    assert(po->pulse_mode == PM_FADE || po->pulse_mode == PM_ONCE || po->pulse_mode == PM_REPEAT);
    uint64_t cur_time = game_source.basic_source.current_time / (long)1e3;
    if (cur_time < po->end_time)
    {
        return;
    }
    if (po->pulse_mode == PM_ONCE || po->pulse_mode == PM_FADE)
    {
        if (!--po->repetitions)
        {
            po->pulse_mode = PM_STEADY;
            po->callback(go);
            return;
        }
    }
    if (po->pulse_mode == PM_FADE)
    {
        po->amplitude /= 1.5;
    }
    //f = 2*pi/t_period => t_period = 2 * pi / f
    po->start_time = cur_time;
    po->end_time = cur_time + (uint64_t)(2. * M_PI / po->frequency);
}

static double PulseObject_get_t(pulse_object_t* po, uint64_t time_ms, int led)
{
    return po->amplitude * pow((1. + cos(po->frequency * (time_ms - po->start_time) + po->phase * po->led_phase * led)) / 2., po->spec_exponent);
}

void PulseObject_update_pulse(game_object_t* o)
{
    uint64_t time_ms = game_source.basic_source.current_time / (long)1e3;
    double t = PulseObject_get_t(&o->pulse, time_ms, 0);
    for (int i = 0; i < o->body.length; ++i)
    {
        hsl_t res;
        lerp_hsl(&o->pulse.colors_0[i], &o->pulse.colors_1[i], t, &res);
        o->body.color[i] = hsl2rgb(&res);
    }
}

void PulseObject_update_pulse_per_led(game_object_t* o)
{
    uint64_t time_ms = game_source.basic_source.current_time / (long)1e3;
    for (int i = 0; i < o->body.length; ++i)
    {
        double t = PulseObject_get_t(&o->pulse, time_ms, i);
        hsl_t res;
        lerp_hsl(&o->pulse.colors_0[i], &o->pulse.colors_1[i], t, &res);
        o->body.color[i] = hsl2rgb(&res);
    }
}

void PulseObject_update(game_object_t* object)
{
    switch (object->pulse.pulse_mode)
    {
    case PM_STEADY:
        if (object->pulse.repetitions != -1)
        {
            PulseObject_update_steady(object);
        }
        break;
    case PM_REPEAT:
    case PM_ONCE:
    case PM_FADE:
        if (object->pulse.led_phase != 0)
        {
            PulseObject_update_pulse_per_led(object);
        }
        else
        {
            PulseObject_update_pulse(object);
        }
        PulseObject_check_pulse_end(&object->pulse, object);
        break;
    }
}

static void PulseObject_init(pulse_object_t* po, double amp, enum PulseMode pm, int repetitions, int period, double phase, double led_phase, double spec)
{
    po->amplitude = amp;
    po->pulse_mode = pm;
    po->repetitions = repetitions;
    po->frequency = 2. * M_PI / (double)period;
    po->phase = phase;
    po->led_phase = led_phase;
    po->spec_exponent = spec;

    po->start_time = game_source.basic_source.current_time / (long)1e3;
    po->end_time = po->start_time + period;
}

static void PulseObject_init_color(pulse_object_t* po, int color_index_0, int color_index_1, int next_color, int length)
{
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

void PulseObject_init_steady(pulse_object_t* po)
{
    po->pulse_mode = PM_STEADY;
}

void PulseObject_mark_deleted(game_object_t* go)
{
    go->body.deleted == 1;
}

void PulseObject_init_player_lost_health()
{
    pulse_object_t* po = &player_object->pulse;
    int length = PlayerObject_get_length();
    int player_health = config.player_health_levels - PlayerObject_get_health();
    int health_color = config.color_index_player + player_health;

    PulseObject_init(po, 1, PM_ONCE, 3, 500, M_PI / 2, 0, 1);
    PulseObject_init_color(po, health_color, health_color + 1, health_color + 1, length);
    po->callback = PlayerObject_take_hit;
}

void PulseObject_init_projectile_explosion(game_object_t* go)
{
    pulse_object_t* po = &go->pulse;
    int length = go->body.length; // MovingObject_get_length(go->body);

    PulseObject_init(po, 1, PM_ONCE, 1, 100, 0, 0, 10);
    PulseObject_init_color(po, config.color_index_K, config.color_index_W, config.color_index_K, length);
    po->callback = PulseObject_mark_deleted;

}