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



/* Config data */
#define N_GEM_COLORS 6
#define N_MAX_BULLETS 16
#define N_MAX_SEGMENTS 16

const int N_HALF_GRAD = 4;  //!< the whole gradient is 2 * half_grad + 1 colours long, with the basic colour in the middle
//i.e. 0  1 .. N_HALF_GRAD  N_HALF_GRAD+1 .. 2 * N_HALF_GRAD
//     |           ^this is the basic colour              |
//     ^ this is the darkest colour                       |
//                                                        ^ this is the lightest colour

const int COLLAPSE_TIME = 2000; //!< in miliseconds
const double GEM_FREQ[N_GEM_COLORS] = { 0.5, 0.75, 1.0, 1.5, 1.25 };

int player_colour = 0xFFFFFF;

/* Config data end */

/* move to their own files 


typedef struct MovingObject {
    double position;
    double speed;
    Match3GameSource* match3_game_source;
} moving_object_t;


void MovingObject_move(moving_object_t* mo)
{

}*/


//! playing field
typedef struct TJewel {
    unsigned char type;     //!< type == N_GEMS_COLORS => this is a player
    double sin_phase;
    double cos_phase;
    unsigned char is_collapsing;
} jewel_t;
jewel_t* field;

//! Segments of the field that move as one block
typedef struct TSegment {
    int start;      //!< this is index into field
    double speed;   //!< in leds/second
    double shift;   //!< offset againt start, first led of the field is start + shift
    uint64_t last_move;
    int tmp;
} segment_t;
segment_t segments[N_MAX_SEGMENTS];
int n_segments = 0;

//! Jewels fired by the players
typedef struct TBullet {
    jewel_t jewel;
    double position; //!< relative to field
    double speed;    //!< could be positive or negative, in leds/second
    int player;      //!< index into players; player that fired the bullet
} bullet_t;
//! array of bullets
bullet_t bullets[N_MAX_BULLETS];
//! number of bullets
n_bullets = 0;

//! Players positions
bullet_t players[C_MAX_CONTROLLERS];

//! collapse progress: 0 collapse is finished, 1 collapse is finished
double collapse_progress = 0;

int* canvas3;
int* zbuffer;
const int C_BULLET_Z = 16; //!< bullets z index will be C_BULLET_Z + bullet


//! This will find segments with three or more gems of the same colour and starts
//! @param length will be filled with the length of the segment found (at least 3)
//! @return the index in the field where the triplet is found, -1 if no sequence is found
const int evaluate_field(int* length)
{
    length = 0;
    return -1;
}


void update_segments()
{
    const double x = 2; // segments only move when in 1/x from end, at x * speed
    double time_delta = match3_game_source.basic_source.time_delta / 1000L / 1e6;
    for (int segment = 0; segment < n_segments; ++segment)
    {
        if (segments[segment].speed == 0)
            continue;
        double fraction = segments[segment].shift - trunc(segments[segment].shift);
        double delta = 0;
        /*
        double delta = segments[segment].speed * time_delta;
        double squeeze = (1.5 - 4 * (0.5 - fraction) * (0.5 - fraction));
        segments[segment].shift += squeeze * squeeze * squeeze * delta;
        printf("squeeze %f\n", squeeze);
        */
        double sec_per_led = fabs(1.0 / segments[0].speed); 
        double sec_from_last_move = (match3_game_source.basic_source.current_time - segments[0].last_move) / 1000l / 1e6;
        if (sec_from_last_move > sec_per_led * (x - 1) / x) 
        {
            delta = segments[segment].speed * time_delta * x;
        }
        if (fraction + delta > 1.0 || (fraction + delta < 0 && fraction > 0.0001)) 
        {
            segments[segment].last_move = match3_game_source.basic_source.current_time;
            //delta = (delta > 0) ? 1.0 - fraction : -fraction;
        }
        //printf("s/led %f, last move %f  fraction %f  delta %f\n", sec_per_led, sec_from_last_move, fraction, delta);
        delta = -time_delta / sec_per_led;
        segments[segment].shift += delta;
    }
}

void update_bullets()
{
    double time_delta = match3_game_source.basic_source.time_delta / 1000L / 1e6;
    int remove_bullet[N_MAX_BULLETS] = { 0 };
    for (int bullet = 0; bullet < n_bullets; ++bullet)
    {
        bullets[bullet].position += bullets[bullet].speed * time_delta;
        if (bullets[bullet].position > match3_game_source.basic_source.n_leds || bullets[bullet].position < 0)
        {
            remove_bullet[bullet] = 1;
        }
    }
    //remove bullets that are over limit
    for (int bullet = n_bullets - 1; bullet >= 0; --bullet)
    {
        if (!remove_bullet[bullet])
            continue;
        for (int b = bullet; b < n_bullets - 2; ++b)
        {
            bullets[b] = bullets[b + 1];
        }
        n_bullets--;
    }

    //test hack
    //double t = (match3_game_source.basic_source.current_time - match3_game_source.start_time) / 1000l / 1e6;
    if (random_01() < 0.0001)
    {
        bullets[n_bullets].jewel.type = trunc(random_01() * N_GEM_COLORS);
        bullets[n_bullets].speed = 0.2; // +random_01();
        bullets[n_bullets].position = 0;
        n_bullets++;
        printf("bullet fired\n");
    }
}

/*void rgb2rgb(int rgb, int* r, int* g, int* b)
{
    r = (int)((rgb >> 16) & 0xFF);
    g = (int)((rgb >> 8) & 0xFF);
    b = (int)(rgb & 0xFF);
}*/

void rgb2rgb(int rgb_in, double* rgb_out)
{
    rgb_out[0] = (double)((rgb_in >> 16) & 0xFF)/0xFF;
    rgb_out[1] = (double)((rgb_in >> 8) & 0xFF)/0xFF;
    rgb_out[2] = (double)(rgb_in & 0xFF) / 0xFF;
}

int mix_rgb_alpha(int rgb1, double alpha1, int rgb2, double alpha2)
{
    assert(alpha1 >= 0.0);
    assert(alpha2 >= 0.0);
    assert(alpha1 + alpha2 <= 1.0);
    /*hsl_t c1, c2, out;
    rgb2hsl(rgb1, &c1);
    rgb2hsl(rgb2, &c2);
    float t = (float)(alpha1 / (alpha1 + alpha2));
    lerp_hsl(&c1, &c2, 1-t, &out);*/

    double rgb1a[3], rgb2a[3], rgb_out[3];
    rgb2rgb(rgb1, rgb1a);
    rgb2rgb(rgb2, rgb2a);
    double l1 = 0, l2 = 0, l_out = 0;
    for (int i = 0; i < 3; ++i)
    {
        l1 += rgb1a[i] * rgb1a[i];
        l2 += rgb2a[i] * rgb2a[i];
        rgb_out[i] = rgb1a[i] * alpha1 + rgb2a[i] * alpha2;
        l_out += rgb_out[i] * rgb_out[i];
    }
    double l_target = l1 * alpha1 + l2 * alpha2;
    double norm = (l_out != 0) ? sqrt(l_target / l_out) :  1;
    int a = (int)(0xFF * (alpha1 + alpha2));
    int r = (int)(norm * rgb_out[0] * 0xFF);
    int g = (int)(norm * rgb_out[1] * 0xFF);
    int b = (int)(norm * rgb_out[2] * 0xFF);

    /*int r = (int)(((rgb1 >> 16) & 0xFF) * alpha1 + ((rgb2 >> 16) & 0xFF) * alpha2);
    int g = (int)(((rgb1 >> 8) & 0xFF) * alpha1 + ((rgb2 >> 8) & 0xFF) * alpha2);
    int b = (int)(((rgb1) & 0xFF) * alpha1 + ((rgb2) & 0xFF) * alpha2);
    int a = (int)(0xFF * alpha1 + 0xFF * alpha2);*/
    return a << 24 | r << 16 | g << 8 | b;
    
    //return a << 24 | hsl2rgb(&out);
}


//! How to render the field
//! There are:
//!    a. moving players
//!    b. moving jewels
//!    c. moving field
//!    d. collapsing jewels 
//! this is the order of precedence
void render_field(ws2811_t* ledstrip)
{
    double sins[N_GEM_COLORS];
    double coss[N_GEM_COLORS];
    for (int i = 0; i < N_GEM_COLORS; ++i)
    {
        sins[i] = sin(2 * M_PI * GEM_FREQ[i] * (match3_game_source.basic_source.current_time - match3_game_source.start_time) / 1e9);
        coss[i] = cos(2 * M_PI * GEM_FREQ[i] * (match3_game_source.basic_source.current_time - match3_game_source.start_time) / 1e9);
    }

    //clear z-buffer
    memset(zbuffer, 0, sizeof(int) * match3_game_source.basic_source.n_leds);
    //clear canvas
    memset(canvas3, 0, sizeof(int) * match3_game_source.basic_source.n_leds);

    //render players
    for (int player = 0; player < match3_game_source.n_players; ++player)
    {
        double pos = trunc(players[player].position);
        double shift = players[player].position - pos;
        zbuffer[(int)pos] = C_BULLET_Z + N_MAX_BULLETS + player;
        if (shift > 0) zbuffer[(int)pos + 1] = C_BULLET_Z + N_MAX_BULLETS + player;
        unsigned char alpha = (unsigned char)((1.0 - shift) * 0xFF);
        canvas3[(int)pos] = player_colour | alpha << 24;
        if (shift > 0) canvas3[(int)pos + 1] = player_colour | (0xFF - alpha) << 24;
    }
    for (int bullet = 0; bullet < n_bullets; ++bullet)
    {
        double pos = trunc(bullets[bullet].position);
        double shift = bullets[bullet].position - pos;
        zbuffer[(int)pos] = C_BULLET_Z + bullet;
        if (shift > 0) zbuffer[(int)pos + 1] = C_BULLET_Z + bullet;
        unsigned char alpha = (unsigned char)((1.0 - shift) * 0xFF);
        int gradient_index = (2 * N_HALF_GRAD + 1) * bullets[bullet].jewel.type + N_HALF_GRAD;
        int bullet_colour = player_colour; // match3_game_source.basic_source.gradient.colors[gradient_index];
        canvas3[(int)pos] = bullet_colour | alpha << 24;
        if (shift > 0) canvas3[(int)pos + 1] = bullet_colour | (0xFF - alpha) << 24;
    }

    //render segments
    int led_offset = 0;
    for (int segment = 0; segment < n_segments; ++segment)
    {
        int segment_start = segments[segment].start;
        int segment_end = (segment == n_segments - 1) ? match3_game_source.basic_source.n_leds : segments[segment + 1].start;
        double segment_shift = segments[segment].shift;
        for (int fi = segment_end - 1; fi >= segment_start; --fi)  //fi -- field_index 
        {
            unsigned char type = field[fi].type;
            //this will transform ampl to <0, 2 * N_HALF_GRAD>
            double ampl = (sins[type] * field[fi].cos_phase + coss[type] * field[fi].sin_phase + 1) * N_HALF_GRAD;
            int gradient_index = (int)ampl;
            double blend = ampl - (int)ampl;
            gradient_index += (2 * N_HALF_GRAD + 1) * type;
            assert(gradient_index >= type * (2 * N_HALF_GRAD + 1)); 
            assert(gradient_index < (type + 1)* (2 * N_HALF_GRAD + 1));
            assert(gradient_index + 1 < match3_game_source.basic_source.gradient.n_colors);


            gradient_index = (2 * N_HALF_GRAD + 1) * type + N_HALF_GRAD;
            int colour = match3_game_source.basic_source.gradient.colors[gradient_index];

            //now we have to blend it with canvas; if there is something in z-buffer, we will shift to avoid
            int led = (int)trunc(fi + segment_shift);

            //there are two limits: how much we want to show -- depending on segment_shift -- and how much can fit -- depending on the canvas_alpha
            double total_alpha = 0.0;


            double alpha = ((fi + segment_shift) - (double)led);
            led += led_offset;
            if (led >= match3_game_source.basic_source.n_leds || led < 0) continue;
            led_offset += 2; //the next loop will normally iterate twice
            while (total_alpha < 1 && led < match3_game_source.basic_source.n_leds && led >= 0)
            {
                int canvas_colour = canvas3[led] & 0xFFFFFF;
                double canvas_alpha = (double)((canvas3[led] & 0xFF000000) >> 24) / (double)0xFF;
                double led_alpha = min(alpha, 1 - canvas_alpha);
                canvas3[led] = mix_rgb_alpha(colour, led_alpha, canvas_colour, canvas_alpha);
                total_alpha += led_alpha;
                alpha = 1. - total_alpha;
                led--;
                led_offset--;
            }
            if (fi == 0) printf("ss %i, sh %f, led %i\n", segment_start, segment_shift, led);
        }
    }


    //output canvas into leds
    for (int led = 0; led < match3_game_source.basic_source.n_leds; ++led)
    {
        double canvas_alpha = (double)((canvas3[led] & 0xFF000000) >> 24) / (double)0xFF;
        int canvas_colour = canvas3[led] & 0xFFFFFF;
        //if (zbuffer[led] == 0) canvas_alpha *= 0.5;
        ledstrip->channel[0].leds[led] = multiply_rgb_color(canvas_colour, canvas_alpha);
        if (led == 0)
        {
            //printf("%i %f\n", gradient_index, blend);
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
//! Rendering collapsing jewels
//! 
//! 
//!  o o o o o . . . o o o o
//!  ^start[0]       ^start[1]
//!  
int Match3GameSource_update_leds(int frame, ws2811_t* ledstrip)
{
    update_bullets();
    update_segments();
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
    segments[0].start = 0;
    segments[0].speed = -0.2;
    segments[0].shift = 30.01;
    segments[0].last_move = match3_game_source.basic_source.current_time;
    segments[0].tmp = 0;
    n_segments = 1;
}

void Match3GameSource_init_player_position()
{
    double d = (double)match3_game_source.basic_source.n_leds / ((double)match3_game_source.n_players + 1.0);
    for (int i = 0; i < match3_game_source.n_players; ++i)
    {
        players[i].position = (int)d * (i + 1);
        players[i].player = i;
        players[i].speed = 0;
        players[i].jewel.type = N_GEM_COLORS;
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
    canvas3 = malloc(sizeof(int) * match3_game_source.basic_source.n_leds);
    zbuffer = malloc(sizeof(int) * match3_game_source.basic_source.n_leds);
    match3_game_source.n_players = Controller_get_n_players();
    Match3GameSource_init_field();
    Match3GameSource_init_player_position();
}

void Match3GameSource_destruct()
{
    free(field);
    free(canvas3);
    free(zbuffer);
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