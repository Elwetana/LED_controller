#define _CRT_SECURE_NO_WARNINGS

/********
 * TODO *
 ********
 
 [ ] Splitting and merging game field, moving backwards
 [ ] Emittor
 [ ] Sounds
 [ ] Music
 [ ] Players -- what's their gameplay?
 [ ] Game phases/levels

*/
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
#include "m3_field.h"
#include "m3_input_handler.h"


/* Config data */
const struct Match3Config match3_config = {
    .n_half_grad = 4,
    .collapse_time = 5,
    .gem_freq = { 0.5, 0.75, 1.0, 1.5, 1.25 },
    .player_colour = 0xFFFFFF,
    .collapse_colour = 0xFFD700,
    .player_move_cooldown = 0.2,
    .player_dit_length = 250,
    .player_n_dits = 4,
    .player_patterns = { 
        {1, 1, 1, 1},
        {1, 0, 1, 0},
        {1, 1, 1, 0},
        {2, 3, 0, 0}
    },
    .max_accelaration = 0.05,
    .normal_forward_speed = 0.05,
    .retrograde_speed = -0.025,
    .slow_forward_speed = 0.01,
    .bullet_speed = 2
};


#define N_MAX_BULLETS 16

/* Config data end */

//! \brief all pulse functions expect progress prg going from 0 to 1
double saw_tooth(double prg)
{
    return 1.0 - prg;
}
double saw_tooth_first_half(double prg)
{
    return 1.0 - 0.5 * prg;
}
double saw_tooth_second_half(double prg)
{
    return 0.5 - 0.5 * prg;
}

double(*pulse_functions[4])(double) = { 0x0, saw_tooth, saw_tooth_first_half, saw_tooth_second_half};


/* 
 * General description of rendering moving objects:
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
 * 
 * ==Game modes==
 * 
 * -- Zuma-like -- 1. Basic: Bullet in emittor is generated at random, all players are equal, everybody can move, 
 * |            |            everybody can fire the bullet. There may be subvariants depending on how the bullet
 * |            |            moves (is caught, lands at the spot where player was when fired, some limited control...
 * |            |
 * |             - Gunner -- 2.a Gunner fires: Gunner select the bullet, fires it, players only catch it
 * |                      |
 * |                       - 2.b Player fires: Gunner only selects the bullet, any player can fire it and only
 * |                                           this player can catch it
 * |
 *  - True Match Three -- 3. All together: Players move around the field (field is static), can swap two neighbours
 *                                         when this creates new triplet, this is eliminated. The field is generated
 *                                         in a way that guarantees it is solvable.
 */


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

int* canvas3;
int* zbuffer;
const int C_LED_Z = 1;          //!< 0 means there is nothing in z buffer, so led with index 0 must be 1
const int C_BULLET_Z = 4096;    //!< bullets z index will be C_BULLET_Z + bullet
const int C_SEGMENT_SHIFT = 10; //!< z buffer for jewels is (segment_index << C_SEGMENT_SHIFT) | field_index


void bullet_into_jewel(int bullet);

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
    if (t - players[player_index].last_move > match3_config.player_move_cooldown)
    {
        players[player_index].position += direction;
        players[player_index].last_move = t;
    }
    players[player_index].position = max(0, players[player_index].position);
    players[player_index].position = min(players[player_index].position, match3_game_source.basic_source.n_leds - 1);
}

static void PLayer_press_button(int player, enum EM3_BUTTONS button)
{
    printf("Catch bullet attempted\n");
    double min_d = C_MAX_FIELD_LENGTH * C_MAX_FIELD_LENGTH;
    int min_bullet = -1;
    for (int bullet = 0; bullet < n_bullets; ++bullet)
    {
        double d = bullets[bullet].position - players[player].position;
        d *= d;
        if (d < min_d)
        {
            min_d = d;
            min_bullet = bullet;
        }
    }
    if (min_d != -1 && min_d < 9)
    {
        printf("Bullet caught %i\n", min_bullet);
        bullet_into_jewel(min_bullet);
    }
    else
        printf("bullet missed %f\n", min_d);

}


void delete_bullet(int bullet)
{
    //printf("Before: "); for (int b = 0; b < n_bullets; ++b) printf("%i ", bullets[b].jewel.type); printf("\n");
    //printf("deleting bullet %i of type %i, n_bullets %i\n", bullet, bullets[bullet].jewel.type, n_bullets);
    assert(bullet < n_bullets);
    for (int b = bullet; b < n_bullets - 1; ++b)
    {
        bullets[b] = bullets[b + 1];
    }
    n_bullets--;
    //printf("After: "); for (int b = 0; b < n_bullets; ++b) printf("%i ", bullets[b].jewel.type); printf("\n");
}

void update_bullets()
{
    double time_delta = (double)(match3_game_source.basic_source.time_delta / 1000L) / 1e6;
    int remove_bullet[N_MAX_BULLETS] = { 0 };
    for (int bullet = 0; bullet < n_bullets; ++bullet)
    {
        bullets[bullet].position += bullets[bullet].speed * time_delta;
        if (bullets[bullet].position > match3_game_source.basic_source.n_leds - 1 || bullets[bullet].position < 0)
        {
            remove_bullet[bullet] = 1;
        }
    }
    //remove bullets that are over limit
    for (int bullet = n_bullets - 1; bullet >= 0; --bullet)
    {
        if (!remove_bullet[bullet])
            continue;
        delete_bullet(bullet);
    }

    //test hack
    if (random_01() < (0.005 / (n_bullets + 1) ))
    {
        bullets[n_bullets].jewel.type = (unsigned char)trunc(random_01() * N_GEM_COLORS);
        bullets[n_bullets].speed = -match3_config.bullet_speed; // +random_01();
        bullets[n_bullets].position = match3_game_source.basic_source.n_leds - 1;
        n_bullets++;
        printf("bullet fired\n");
    }
}

void bullet_into_jewel(int bullet)
{
    //insert new jewel in the field
    int bullet_pos = (int)bullets[bullet].position;

    if (zbuffer[bullet_pos + 1] == 0)  //we are not in a segment, nothing can be inserted
        return;

    int segment = zbuffer[bullet_pos + 1] >> C_SEGMENT_SHIFT;
    int fi_insert = (zbuffer[bullet_pos + 1] & ((1 << C_SEGMENT_SHIFT) - 1)) - 1;
    Field_insert_and_evaluate(fi_insert, segment, bullets[bullet].jewel);
    printf("Inserted jewel type %i at position %i\n", bullets[bullet].jewel.type, fi_insert);
    //destroy the bullet
    delete_bullet(bullet);
}

void render_players()
{
    double d = 1000. * seconds_from_start() / (double)match3_config.player_dit_length;
    int dit = (int)trunc(d) % match3_config.player_n_dits;
    double prg = d - trunc(d);

    for (int player = 0; player < match3_game_source.n_players; ++player)
    {
        //player is displayed when its patter is on or when it is just moving
        int is_moving = (seconds_from_start() - players[player].last_move) < 2 * match3_config.player_move_cooldown;
        if (match3_config.player_patterns[player][dit] || is_moving)
        {
            double pos = trunc(players[player].position);
            //double shift = players[player].position - pos;
            //if (zbuffer[(int)pos] == -1) //collapsing jewel
            //    continue;
            //zbuffer[(int)pos] = C_BULLET_Z + N_MAX_BULLETS + player;
            //if (shift > 0) zbuffer[(int)pos + 1] = C_BULLET_Z + N_MAX_BULLETS + player;
            unsigned char alpha = is_moving ? 0xFF : (unsigned char)(pulse_functions[match3_config.player_patterns[player][dit]](prg) * 0xFF);
            canvas3[(int)pos] = match3_config.player_colour | alpha << 24;
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
        int gradient_index = (2 * match3_config.n_half_grad + 1) * bullets[bullet].jewel.type + match3_config.n_half_grad;
        int bullet_colour = match3_game_source.basic_source.gradient.colors[gradient_index];
        canvas3[led] = bullet_colour | 0xFF << 24;
    }
}

void render_bullets_alpha()
{
    for (int bullet = 0; bullet < n_bullets; ++bullet)
    {
        int led = (int)bullets[bullet].position;
        for (int i = 1; i < 4; ++i)
        {
            int alpha = (0.25 + (double)i / 8.) * 0xFF;
            int canvas_colour = canvas3[led + i] & 0xFFFFFF; 
            canvas3[led + i] = alpha << 24 | canvas_colour;
            canvas_colour = canvas3[led - i] & 0xFFFFFF;
            canvas3[led - i] = alpha << 24 | canvas_colour;
        }
    }
}

void render_collapsing_segments()
{
    int segment = Segments_get_next_collapsing(-1);
    while (segment > -1)
    {
        int segment_length = Segments_get_length(segment);
        int segment_position = (int)trunc(Segments_get_position(segment));
        double collapse_progress = 4. * Segments_get_collapse_progress(segment);
        while (collapse_progress > 1) collapse_progress--;
        for (int fi = 0; fi < segment_length; ++fi)  //fi -- field_index 
        {
            int led = fi + segment_position;
            canvas3[led] = ((int)(0xFF * collapse_progress) << 24) | match3_config.collapse_colour;;
            zbuffer[led] = 0;
        }
        segment = Segments_get_next_collapsing(segment);
    }
}

void render_moving_segments()
{
    double sins[N_GEM_COLORS] = { 0 };
    double coss[N_GEM_COLORS] = { 0 };
    for (int i = 0; i < N_GEM_COLORS; ++i)
    {
        sins[i] = sin(2 * M_PI * match3_config.gem_freq[i] * (match3_game_source.basic_source.current_time - match3_game_source.start_time) / 1e9);
        coss[i] = cos(2 * M_PI * match3_config.gem_freq[i] * (match3_game_source.basic_source.current_time - match3_game_source.start_time) / 1e9);
    }

    int segment = Segments_get_next_moving(-1);
    while(segment > -1)
    {
        int segment_length = Segments_get_length(segment);
        int segment_position = (int)trunc(Segments_get_position(segment));
        double offset = Segments_get_position(segment) - segment_position;
        int hole_position = (int)(segment_length * (1 - offset));
        int hole_direction = -Segments_get_direction(segment);

        int led_discombobulation = 0;
        for (int fi = 0; fi < segment_length; ++fi)  //fi -- field_index 
        {
            int led = fi + segment_position + led_discombobulation;
            if (fi * hole_direction <= hole_position * hole_direction)
            {
                led -= hole_direction;
            }
            if ((led + hole_direction) >= match3_game_source.basic_source.n_leds)
                continue;

            if (fi == hole_position) //we will outptut two leds for this fi, one for the hole, one for the jewel
            {
                if (zbuffer[led + hole_direction] > 0) //if there is bullet on the hole position, we have to shift even more
                {
                    assert(zbuffer[led + hole_direction] >= C_BULLET_Z); //we should never render one field over another
                    led += 1;
                    ++led_discombobulation;
                }
            }
            if (led >= match3_game_source.basic_source.n_leds)
                continue;
            //check collision with bullets
            while (zbuffer[led] > 0)
            {
                assert(zbuffer[led] >= C_BULLET_Z); //we should never render one segment over another
                led += 1;
                ++led_discombobulation;
                if (led >= match3_game_source.basic_source.n_leds)
                    break;
            }

            if (led >= match3_game_source.basic_source.n_leds)
                continue;

            if (fi == 0 && led_discombobulation > 0)
            {
                //printf("segment %i, fi %i, dsc %i\n", segment, fi, led_discombobulation);
                Segments_add_shift(segment, led_discombobulation);
                segment_position += led_discombobulation;
                led_discombobulation = 0;
            }

            jewel_t jewel = Segments_get_jewel(segment, fi);
            unsigned char type = jewel.type;
            //this will transform ampl to <0, 2 * N_HALF_GRAD>
            double ampl = (sins[type] * jewel.cos_phase + coss[type] * jewel.sin_phase + 1) * match3_config.n_half_grad;
            int gradient_index = (int)ampl;
            double blend = ampl - (int)ampl;
            gradient_index += (2 * match3_config.n_half_grad + 1) * type;
            assert(gradient_index >= type * (2 * match3_config.n_half_grad + 1));
            assert(gradient_index < (type + 1) * (2 * match3_config.n_half_grad + 1));
            assert(gradient_index + 1 < match3_game_source.basic_source.gradient.n_colors);
            gradient_index = (2 * match3_config.n_half_grad + 1) * type + match3_config.n_half_grad;

            int colour = 0xFF << 24 | match3_game_source.basic_source.gradient.colors[gradient_index];
            int z_index = C_LED_Z + (segment << C_SEGMENT_SHIFT | fi);

            canvas3[led] = colour;
            zbuffer[led] = z_index;
        }
        Segments_set_discombobulation(segment, led_discombobulation);
        segment = Segments_get_next_moving(segment);
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
    render_moving_segments();
    render_bullets_alpha();
    render_players();
    render_collapsing_segments();

    //output canvas into leds
    for (int led = 0; led < match3_game_source.basic_source.n_leds; ++led)
    {
        double canvas_alpha = (double)((canvas3[led] & 0xFF000000) >> 24) / (double)0xFF;
        int canvas_colour = canvas3[led] & 0xFFFFFF;
        //debug
        int segment = -1;
        if (zbuffer[led] < C_BULLET_Z) segment = zbuffer[led] >> C_SEGMENT_SHIFT;
        if (segment > 0) 
            canvas_alpha = canvas_alpha / (segment + 1);
        //debug end
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
    update_bullets();
    Segments_update();
    render_field(frame, ledstrip);
    Match3InputHandler_process_input();
    return 1;
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
    match3_game_source.Player_press_button = PLayer_press_button;
    //Game_source_init_objects();
    canvas3 = malloc(sizeof(int) * match3_game_source.basic_source.n_leds);
    zbuffer = malloc(sizeof(int) * match3_game_source.basic_source.n_leds);
    match3_game_source.n_players = Controller_get_n_players();
    Field_init();
    Match3GameSource_init_player_position();
}

void Match3GameSource_destruct()
{
    free(canvas3);
    free(zbuffer);
    Field_destruct();
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