#define _CRT_SECURE_NO_WARNINGS

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
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
#include "pulse_object.h"
#include "input_handler.h"
#include "stencil_handler.h"
#include "game_source_priv.h"
#include "game_source.h"

#define GAME_DEBUG

const int C_BKGRND_OBJ_INDEX =   0;
const int C_OBJECT_OBJ_INDEX =  32; //ships and asteroids
const int C_PROJCT_OBJ_INDEX = 128; //projectiles


enum PulseModes
{
    PM_STEADY,  //!< no blinking, copy source_color to moving_object.color
    PM_REPEAT,  //!< keep pulsing with the same parameter
    PM_ONCE,    //!< switch to normal steady light after repetions cycles switch to steady and execute callback
    PM_FADE     //!< decrease amplitude at end, after repetions cycles switch to steady and execute callback
};

void PulseObject_update_steady(game_object_t* o)
{
    for (int i = 0; i < o->body.length; ++i)
    {
        o->body.color[i] = o->source_color[i];
    }
    o->pulse.repetitions = -1;
}

void PulseObject_check_pulse_end(pulse_object_t* po, uint64_t cur_time, game_object_t* go)
{
    assert(po->pulse_mode == PM_FADE || po->pulse_mode == PM_ONCE || po->pulse_mode == PM_REPEAT);
    if (cur_time < po->end_time)
    {
        return;
    }
    if (po->pulse_mode == PM_ONCE || po->pulse_mode == PM_FADE)
    {
        if (!--po->repetitions)
        {
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
    PulseObject_check_pulse_end(&o->pulse, time_ms, o);
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

PulseObject_update(game_object_t* object)
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
        break;
    }
}

void PulseObject_init_steady(pulse_object_t* po)
{
    po->pulse_mode = PM_STEADY;
}

/*! The sequence of actions during one loop:
*   - process inputs - this may include timers?
*   - process collisions using stencil buffer -- this may also trigger events
*   - process colors for blinking objects
*   - move and render all objects
* 
* \param frame      current frame - not used
* \param ledstrip   pointer to rendering device
* \returns          1 when render is required, i.e. always
*/
int GameSource_update_leds(int frame, ws2811_t* ledstrip)
{
    (void)frame;
    //unit_tests();
    Canvas_clear(ledstrip->channel[0].leds);
    InputHandler_process_input();
    Stencil_check_movement();

    //"AI"
    //Colors

    for (int p = 0; p < MAX_N_OBJECTS; ++p)
    {
        if (!game_objects[p].body.deleted)
        {
            MovingObject_render(&game_objects[p].body, &game_objects[p].mr, ledstrip->channel[0].leds, 1);
            MovingObject_update(&game_objects[p].body, &game_objects[p].mr);
        }
    }
    return 1;
}

GameObjects_init()
{
    for (int i = 0; i < MAX_N_OBJECTS; ++i)
        game_objects[i].body.deleted = 1;
    PlayerObject_init();
}

/*! Init all game objects and modes */
Game_source_init_objects()
{
    //placeholder -- config will be read from file
    config.player_start_position = 180;
    config.player_ship_speed = 1;
    config.player_ship_size = 5;
    config.color_index_R = 0;
    config.color_index_G = 1;
    config.color_index_B = 2;
    config.color_index_C = 3;
    config.color_index_M = 4;
    config.color_index_Y = 5;
    config.color_index_W = 6;
    config.color_index_player = 7;
    config.player_health_levels = 6; //i.e 7 - 12 is index of player health levels

    InputHandler_init();
    GameObjects_init();
    Stencil_init();
}

//msg = color?xxxxxx
void GameSource_process_message(const char* msg)
{
    char* sep = strchr(msg, '?');
    if (sep == NULL)
    {
        printf("GameSource: message does not contain target %s\n", msg);
        return;
    }
    if ((sep - msg) >= 32)
    {
        printf("GameSource: target is too long or poorly formatted: %s\n", msg);
        return;
    }
    if ((strlen(sep + 1) >= 64))
    {
        printf("GameSource: message too long or poorly formatted: %s\n", msg);
        return;
    }
    char target[32];
    char payload[64];
    strncpy(target, msg, sep - msg);
    strncpy(payload, sep + 1, 64);
    target[sep - msg] = 0x0;
    payload[63] = 0x0;
    if (!strncasecmp(target, "color", 5))
    {
        int col;
        col = (int)strtol(payload, NULL, 16);
        game_source.basic_source.gradient.colors[0] = col;
        game_source.first_update = 0;
        printf("Switched colour in GameSource to: %s = %x\n", payload, col);
    }
    else
        printf("GameSource: Unknown target: %s, payload was: %s\n", target, payload);

}

void GameSource_init(int n_leds, int time_speed, uint64_t current_time)
{
    BasicSource_init(&game_source.basic_source, n_leds, time_speed, source_config.colors[GAME_SOURCE], current_time);
    game_source.first_update = 0;
    canvas = malloc(sizeof(pixel_t) * n_leds);
    Game_source_init_objects();
}

void GameSource_destruct()
{
    free(canvas);
}

void GameSource_construct()
{
    BasicSource_construct(&game_source.basic_source);
    game_source.basic_source.update = GameSource_update_leds;
    game_source.basic_source.init = GameSource_init;
    game_source.basic_source.destruct = GameSource_destruct;
    game_source.basic_source.process_message = GameSource_process_message;
}

GameSource game_source = {
    .basic_source.construct = GameSource_construct,
    .first_update = 0 
};
