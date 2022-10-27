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

#include "colours.h"
#include "common_source.h"
#include "source_manager.h"
#include "sound_player.h"
#include "controller.h"

#include "m3_game_source.h"



//! playing field
typedef struct TJewel {
    unsigned char type;
    double sin_phase;
    double cos_phase;
} jewel_t;
jewel_t *field;

int player_position[C_MAX_CONTROLLERS] = { -1 };

//! collapse progress: 0 collapse is finished, 1 collapse is finished
double collapse_progress = 0;

/* Config data */
#define N_GEM_COLORS 6
const int N_HALF_GRAD = 4;  //!< the whole gradient is 2 * half_grad + 1 colours long, with the basic colour in the middle
//i.e. 0  1 .. N_HALF_GRAD  N_HALF_GRAD+1 .. 2 * N_HALF_GRAD
//     |           ^this is the basic colour              |
//     ^ this is the darkest colour                       |
//                                                        ^ this is the lightest colour

const int COLLAPSE_TIME = 2000; //!< in miliseconds
const double GEM_FREQ[N_GEM_COLORS] = {0.5, 0.75, 1.0, 1.5, 1.25}; 


//! This will find segments with three or more gems of the same colour and starts
//! @param length will be filled with the length of the segment found (at least 3)
//! @return the index in the field where the triplet is found, -1 if no sequence is found
const int evaluate_field(int* length)
{
    length = 0;
    return -1;
}


void update_field()
{

}

void render_field(ws2811_t* ledstrip)
{
    double sins[N_GEM_COLORS];
    double coss[N_GEM_COLORS];
    for (int i = 0; i < N_GEM_COLORS; ++i)
    {
        sins[i] = sin(2 * M_PI * GEM_FREQ[i] * (match3_game_source.basic_source.current_time - match3_game_source.start_time) / 1e9);
        coss[i] = cos(2 * M_PI * GEM_FREQ[i] * (match3_game_source.basic_source.current_time - match3_game_source.start_time) / 1e9);
    }

    for (int led = 0; led < match3_game_source.basic_source.n_leds; ++led)
    {
        unsigned char type = field[led].type;
        //this will transform ampl to <0, 2 * N_HALF_GRAD>
        double ampl = (sins[type] * field[led].cos_phase + coss[type] * field[led].sin_phase + 1) * N_HALF_GRAD;
        int gradient_index = (int)ampl;
        float blend = ampl - (int)ampl;
        gradient_index += (2 * N_HALF_GRAD + 1) * type;
        assert(gradient_index >= type * (2 * N_HALF_GRAD + 1));
        assert(gradient_index < (type + 1) * (2 * N_HALF_GRAD + 1));
        assert(gradient_index + 1 < match3_game_source.basic_source.gradient.n_colors);
        ws2811_led_t color = mix_rgb_color(match3_game_source.basic_source.gradient.colors[gradient_index],
            match3_game_source.basic_source.gradient.colors[gradient_index + 1], 1.0 - blend);
        ledstrip->channel[0].leds[led] = color;
        if (led == 0)
        {
            printf("%i %f\n", gradient_index, blend);
        }
    }
}



//! The update has the following phases:
//!  - if collapse is in progress, just render and check if the collapse is over
//!  - check input, start moving players and/or start swapping jewels
//!  - render field, possibly just collapsing, leaving holes for the players
//!  - render player(s), possibly just moving
//! 
//!   |111|222|333|... <-
//!   |112|223|334|...
//! 
//!   |111|PPP|222|333|... <-, player standing
//!   |112|PPP|223|334|...
//! 
//!   |111|PPP|222|333|... -, player ->
//!   |111|2PP|P22|333|... 
//! 
//!   |111|PPP|222|333|... <-, player ->
//!   |112|2PP|P23|334|... 
//! 
int Match3GameSource_update_leds(int frame, ws2811_t* ledstrip)
{
    render_field(ledstrip);
    //color = mix_rgb_color(trailing_color, object->color[color_index], (float)mr->body_offset);

    return 1;
}

void Match3GameSource_init_field()
{
    for (int i = 0; i < match3_game_source.basic_source.n_leds; ++i)
    {
        field[i].type = (unsigned char)trunc(random_01() * (double)N_GEM_COLORS);
        double shift = random_01()* M_PI;
        field[i].sin_phase = sin(shift);
        field[i].cos_phase = cos(shift);
    }
}

void Match3GameSource_init_player_position()
{
    double d = (double)match3_game_source.basic_source.n_leds / ((double)match3_game_source.n_players + 1.0);
    for (int i = 0; i < match3_game_source.n_players; ++i)
    {
        player_position[i] = (int)d * (i + 1);
    }
}

//msg = mode?xxxxxx
void Match3GameSource_process_message(const char* msg)
{
    /*
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
    if (!strncasecmp(target, "mode", 5))
    {
        enum GameModes gm_before = GameObjects_get_current_mode();
        GameObjects_set_level_by_message(payload);
        printf("Sent code: %s, game mode before: %i\n", payload, gm_before);
    }
    else
        printf("GameSource: Unknown target: %s, payload was: %s\n", target, payload);
    */
}

void Match3GameSource_init(int n_leds, int time_speed, uint64_t current_time)
{
    BasicSource_init(&match3_game_source.basic_source, n_leds, time_speed, source_config.colors[M3_GAME_SOURCE], current_time);
    match3_game_source.start_time = current_time;
    //Game_source_init_objects();
    field = malloc(sizeof(jewel_t) * match3_game_source.basic_source.n_leds);
    Match3GameSource_init_field();
    match3_game_source.n_players = Controller_get_n_players();
    Match3GameSource_init_player_position();
}

void Match3GameSource_destruct()
{
    free(field);
}

void Match3GameSource_construct()
{
    BasicSource_construct(&match3_game_source.basic_source);
    match3_game_source.basic_source.update = Match3GameSource_update_leds;
    match3_game_source.basic_source.init = Match3GameSource_init;
    match3_game_source.basic_source.destruct = Match3GameSource_destruct;
    match3_game_source.basic_source.process_message = Match3GameSource_process_message;
}

Match3GameSource match3_game_source = {
    .basic_source.construct = Match3GameSource_construct
};