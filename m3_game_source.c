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
#include "m3_input_handler.h"



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
const double player_move_cooldown = 0.2;    //in seconds
const int player_dit_length = 250;          //in milliseconds
const int player_n_dits = 4;                //it will be in config, no need for define
unsigned char player_patterns[C_MAX_CONTROLLERS][4] = { //each byte turns respecive player on/off for one dit
    {1, 0, 1, 0},
    {1, 1, 0, 0},
    {1, 1, 1, 0},
    {1, 1, 1, 1}
};

/* Config data end */

/* 
 * General description:
 *   -- there is emittor of bullets at the end of the chain (n_leds - 1)
 * 
 *   -- the bullets always move to the beginning, i.e. to lower numbers
 * 
 *   -- the field segments generally move toward the end, but they can move back (toward beginning) when coalescing
 * 
 *   -- when bullet meets a segment, it will nudge the balls toward the end thus:
 * 
 *             frame 10:     . b . . . 1 2 3 4 5 . . . . . 6 7 8
 *             frame 20:     . . b . . 1 2 3 4 5 . . . . . 6 7 8
 *             frame 30:     . . . b 1 2 3 4 5 . . . . . 6 7 8
 *             frame 40:     . . . 1 b 2 3 4 5 . . . . . 6 7 8
 *             frame 50:     . . 1 2 3 b 4 5 . . . . . 6 7 8
 *             frame 60:     . . 1 2 3 4 b 5 . . . . . 6 7 8
 *             frame 70:     . 1 2 3 4 5 . b . . . . 6 7 8 
 *             frame 80:     . 1 2 3 4 5 . . b . . . 6 7 8 
 * 
 *      note how the distance between 5 and 6 increased from frames 7 on, the discombobulation of the segment is permanent, 
 *      it will not snap back
 * 
 *   -- the movement of the segment is simulated by moving balls one-by-one thus:
 * 
 *             frame 1:     . . . . 1 2 3 4 5 . . . . . 6 7 8
 *             frame 2:     . . . 1 . 2 3 4 5 . . . . 6 . 7 8
 *             frame 3:     . . . 1 2 . 3 4 5 . . . . 6 . 7 8
 *             frame 4:     . . . 1 2 3 . 4 5 . . . . 6 7 . 8
 *             frame 5:     . . . 1 2 3 4 . 5 . . . . 6 7 . 8
 *             frame 6:     . . . 1 2 3 4 5 . . . . . 6 7 8 .
 * 
 *      note that the "hole" moves faster through the longer segment
 *
 */

//! playing field
typedef struct TJewel {
    unsigned char type;     //!< type == N_GEMS_COLORS => this is a player
    double sin_phase;
    double cos_phase;
    unsigned char is_collapsing;
} jewel_t;
jewel_t* field;
int field_length = 0;

//! Segments of the field that move as one block
typedef struct TSegment {
    int start;      //!< this is index into field
    double speed;   //!< in leds/second
    double shift;   //!< offset againt start, first led of the field is start + shift
    //uint64_t last_move;
    unsigned char is_discombobulated;
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

//! Players
typedef struct TPlayer {
    double position;
    double last_move;
} player_t;
player_t players[C_MAX_CONTROLLERS];

//! collapse progress: 0 collapse is finished, 1 collapse is finished
double collapse_progress = 0;

int* canvas3;
int* zbuffer;
const int C_LED_Z = 1;       //!< 0 means there is nothing in z buffer, so led with index 0 must be 1
const int C_BULLET_Z = 4096; //!< bullets z index will be C_BULLET_Z + bullet


/************** Implementation start ********************/

inline double seconds_from_start()
{
    return (double)((match3_game_source.basic_source.current_time - match3_game_source.start_time) / 1000l) / 1e6;
}


/* player input handling */

static void Player_move(int player_index, signed char direction)
{
    //printf("Player %i moved in direction %i\n", player_index, direction);
    assert(player_index <= match3_game_source.n_players);
    assert(direction * direction == 1);
    double t = seconds_from_start();
    if (t - players[player_index].last_move > player_move_cooldown)
    {
        players[player_index].position += direction;
        players[player_index].last_move = t;
    }
    players[player_index].position = max(0, players[player_index].position);
    players[player_index].position = min(players[player_index].position, match3_game_source.basic_source.n_leds - 1);
}


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
    double time_delta = (double)(match3_game_source.basic_source.time_delta / 1000L) / 1e6;
    for (int segment = 0; segment < n_segments; ++segment)
    {
        if (segments[segment].speed == 0)
            continue;
        segments[segment].shift += segments[segment].speed * time_delta;
    }
}

void update_bullets()
{
    double time_delta = (double)(match3_game_source.basic_source.time_delta / 1000L) / 1e6;
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
    if (random_01() < 0.001)
    {
        bullets[n_bullets].jewel.type = (unsigned char)trunc(random_01() * N_GEM_COLORS);
        bullets[n_bullets].speed = -10; // +random_01();
        bullets[n_bullets].position = match3_game_source.basic_source.n_leds - 1;
        n_bullets++;
        printf("bullet fired\n");
    }
}

void render_players()
{
    double d = 1000. * seconds_from_start() / (double)player_dit_length;
    int dit = (int)trunc(d) % player_n_dits;
    double prg = d - trunc(d);

    for (int player = 0; player < match3_game_source.n_players; ++player)
    {
        //player is displayed when its patter is on or when it is just moving
        int is_moving = (seconds_from_start() - players[player].last_move) < 2 * player_move_cooldown;
        if (player_patterns[player][dit] || is_moving)
        {
            if (is_moving) 
                prg = 0;
            double pos = trunc(players[player].position);
            //double shift = players[player].position - pos;
            zbuffer[(int)pos] = C_BULLET_Z + N_MAX_BULLETS + player;
            //if (shift > 0) zbuffer[(int)pos + 1] = C_BULLET_Z + N_MAX_BULLETS + player;
            unsigned char alpha = (unsigned char)((1.0 - prg) * 0xFF);
            canvas3[(int)pos] = player_colour | alpha << 24;
            //if (shift > 0) canvas3[(int)pos + 1] = player_colour | (0xFF - alpha) << 24;
        }
    }
}

void render_bullets()
{
    for (int bullet = 0; bullet < n_bullets; ++bullet)
    {
        int led = (int)bullets[bullet].position;
        zbuffer[led] = C_BULLET_Z + bullet;
        int gradient_index = (2 * N_HALF_GRAD + 1) * bullets[bullet].jewel.type + N_HALF_GRAD;
        int bullet_colour = match3_game_source.basic_source.gradient.colors[gradient_index];
        canvas3[led] = bullet_colour | 0xFF << 24;
    }
}

void render_segments(int frame)
{
    double sins[N_GEM_COLORS];
    double coss[N_GEM_COLORS];
    for (int i = 0; i < N_GEM_COLORS; ++i)
    {
        sins[i] = sin(2 * M_PI * GEM_FREQ[i] * (match3_game_source.basic_source.current_time - match3_game_source.start_time) / 1e9);
        coss[i] = cos(2 * M_PI * GEM_FREQ[i] * (match3_game_source.basic_source.current_time - match3_game_source.start_time) / 1e9);
    }

    for (int segment = 0; segment < n_segments; ++segment)
    {
        int segment_start = segments[segment].start;
        int segment_end = (segment == n_segments - 1) ? field_length : segments[segment + 1].start;
        int segment_shift = (int)trunc(segments[segment].shift);
        double offset = segments[segment].shift - segment_shift;
        int hole_position = (int)((segment_end - segment_start) * (1 - offset));
        int hole_direction = 0;
        if (segments[segment].speed > 0)
        {
            //moving left to right, the hole appears on right and travels to left, we shift jewels that are after the hole
            //offset is increasing
            hole_direction = -1;
        }
        else if (segments[segment].speed < 0)
        {
            //moving right to left, hole appears on left and travels right, we shift jewels that are before hole
            //offset is decreasing
            hole_direction = 1;
        }

        int led_discombobulation = 0;
        for (int fi = segment_start; fi < segment_end; ++fi)  //fi -- field_index 
        {
            int led = fi + segment_shift + led_discombobulation;
            if (fi * hole_direction <= hole_position * hole_direction)
            {
                led -= hole_direction;
            }
            if (led >= match3_game_source.basic_source.n_leds)
                continue;

            if (fi == hole_position) //we will outptut two leds for this fi, one for the hole, one for the jewel
            {
                if (zbuffer[led + hole_direction] != 0) //if there is bullet on the hole position, we have to shift even more
                {
                    assert(zbuffer[led + hole_direction] >= C_BULLET_Z); //we should never render one field over another
                    led += 1;
                    ++led_discombobulation;
                    printf("frame %i ds %i\n", frame, led_discombobulation);
                }
            }
            //check collision with bullets
            while (zbuffer[led] != 0)
            {
                assert(zbuffer[led] >= C_BULLET_Z); //we should never render one field over another
                led += 1;
                ++led_discombobulation;
            }

            if (led >= match3_game_source.basic_source.n_leds)
                continue;

            if (fi == segment_start && led_discombobulation > 0)
            {
                //printf("segment %i, fi %i, dsc %i\n", segment, fi, led_discombobulation);
                segments[segment].shift += led_discombobulation;
                segment_shift += led_discombobulation;
                led_discombobulation = 0;
            }

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

            canvas3[led] = 0xFF << 24 | colour;
            zbuffer[led] = C_LED_Z + (segment << 10 | fi); 
        }
        segments[segment].is_discombobulated = (led_discombobulation > 0) ? 1 : 0;
    }
}


//! How to render the field
//!  1. render bullets
//!  2. render jewels (i.e segments), may be affected bullets
//!  3. render players (no z-checks, painted over everything)
void render_field(int frame, ws2811_t* ledstrip)
{
    //clear z-buffer
    memset(zbuffer, 0, sizeof(int) * match3_game_source.basic_source.n_leds);
    //clear canvas
    memset(canvas3, 0, sizeof(int) * match3_game_source.basic_source.n_leds);

    //render all objects to canvas and zbuffer
    render_bullets();
    render_segments(frame);
    render_players();

    //output canvas into leds
    for (int led = 0; led < match3_game_source.basic_source.n_leds; ++led)
    {
        double canvas_alpha = (double)((canvas3[led] & 0xFF000000) >> 24) / (double)0xFF;
        int canvas_colour = canvas3[led] & 0xFFFFFF;
        if(zbuffer[led] < C_BULLET_Z && segments[zbuffer[led]>>10].is_discombobulated)
            canvas_alpha *= 0.5;
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

int Match3GameSource_update_leds(int frame, ws2811_t* ledstrip)
{
    Match3InputHandler_process_input();
    update_bullets();
    update_segments();
    render_field(frame, ledstrip);
    return 1;
}

void Match3GameSource_init_field()
{
    for (int i = 0; i < field_length; ++i)
    {
        field[i].type = (unsigned char)trunc(random_01() * (double)N_GEM_COLORS);
        double shift = random_01()* M_PI;
        field[i].sin_phase = sin(shift);
        field[i].cos_phase = cos(shift);
    }
    segments[0].start = 0;
    segments[0].speed = 0.05;
    segments[0].shift = 80.01;
    //segments[0].last_move = match3_game_source.basic_source.current_time;
    segments[0].tmp = 0;
    n_segments = 1;
}

void Match3GameSource_init_player_position()
{
    double d = (double)match3_game_source.basic_source.n_leds / ((double)match3_game_source.n_players + 1.0);
    for (int i = 0; i < match3_game_source.n_players; ++i)
    {
        players[i].position = (int)d * (i + 1);
        players[i].last_move = 0;
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
    Match3InputHandler_init();
    match3_game_source.start_time = current_time;
    match3_game_source.Player_move = Player_move;
    //Game_source_init_objects();
    field_length = match3_game_source.basic_source.n_leds / 2;
    field = malloc(sizeof(jewel_t) * field_length);
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