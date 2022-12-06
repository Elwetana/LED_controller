#define _CRT_SECURE_NO_WARNINGS

/********
 * TODO *
 ********
 
 [x] Splitting and merging game field, moving backwards
 [x] Emittor
 [ ] Sounds
 [ ] Music
 [x] Players -- what's their gameplay?
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
#include "m3_players.h"


/* Config data */
const struct Match3Config match3_config = {
    .n_half_grad = 4,
    .collapse_time = 2,
    .gem_freq = { 0.5, 0.75, 1.0, 1.5, 1.25 },
    .player_colour = 0xFFFFFF,
    .collapse_colour = 0xFFD700,
    .player_move_cooldown = 200,
    .player_catch_cooldown = 100,
    .player_catch_distance = 2,
    .player_dit_length = 250,
    .player_n_dits = 4,
    .player_patterns = {
        {1, 1, 1, 1},
        {1, 0, 1, 0},
        {1, 1, 1, 0},
        {2, 3, 0, 0}
    },
    .max_accelaration = 0.05,
    .normal_forward_speed = 0.2,
    .retrograde_speed = -0.3,
    .slow_forward_speed = 0.01,
    .bullet_speed = 3,
    .emitor_cooldown = 150,
    .unswap_timeout = 150
};

#define N_MAX_BULLETS 16
/* Config data end */

//! \brief all pulse functions expect progress prg going from 0 to 1
//! @param prg progress from 0 to 1
//! @return alpha from 0 to 1
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
    jewel_type jewel_type;
    double position; //!< relative to field
    double speed;    //!< could be positive or negative, in leds/second
    int player;      //!< index into players; player that fired the bullet
} bullet_t;
//! array of bullets
bullet_t bullets[N_MAX_BULLETS];
//! number of bullets
n_bullets = 0;

//! Emitor fires the bullets
//! there is only one emitor, so we don't need typedef for this struct
struct {
    jewel_type jewel_type;
    double last_fire;
    const int length;
} emitor = {
    .last_fire = 0, 
    .jewel_type = 0,
    .length = 3
};

int* canvas3;
int* zbuffer;

#ifdef DEBUG_M3
int* debug_fi_current;
int* debug_fi_previous;
#endif // DEBUG_M3

const int C_LED_Z = 1;          //!< 0 means there is nothing in z buffer, so led with index 0 must be 1
const int C_SEGMENT_SHIFT = 10; //!< z buffer for jewels is (segment_index << C_SEGMENT_SHIFT) | field_index
const int C_BULLET_Z = N_MAX_SEGMENTS << 10;    //!< bullets z index will be C_BULLET_Z + bullet


void bullet_into_jewel(int bullet_pos);
const int swap_jewels(player_pos, dir); 

/************** Implementation start ********************/

/************* Utility functions ************************/

inline double miliseconds_from_start()
{
    return (double)((match3_game_source.basic_source.current_time - match3_game_source.start_time) / 1000l) / 1e3;
}

static void get_segment_and_position(int led, int* segment, int* position)
{
    *segment = zbuffer[led] >> C_SEGMENT_SHIFT;
    *position = ((zbuffer[led] - C_LED_Z) & ((1 << C_SEGMENT_SHIFT) - 1));
}

void m3_print_info(int led)
{
    if (zbuffer[led] > 0 && zbuffer[led] < C_BULLET_Z) 
        Segments_print_info(zbuffer[led] >> C_SEGMENT_SHIFT);
}

/******************* Emitor *****************************/

const int Match3_Emitor_get_length()
{
    return emitor.length;
}

const int Match3_Emitor_fire()
{
    double t = miliseconds_from_start();
    if (t - emitor.last_fire < match3_config.emitor_cooldown)
        return 1;
    emitor.last_fire = t;

    ASSERT_M3(emitor.jewel_type < N_GEM_COLORS, (void)0);
    bullets[n_bullets].jewel_type = emitor.jewel_type;
    bullets[n_bullets].speed = -match3_config.bullet_speed; // +random_01();
    bullets[n_bullets].position = match3_game_source.basic_source.n_leds - 1 - emitor.length;
    n_bullets++;
    return 0;
}

const int Match3_Emitor_reload(int dir)
{
    emitor.jewel_type = (emitor.jewel_type + dir) % N_GEM_COLORS;
    return 1;
}

/************ Game interactions *************************/

const int Match3_Game_catch_bullet(int led)
{
    int dist = 0;
    int catch = 999;
    while (dist <= match3_config.player_catch_distance && catch == 999)
    {
        if (zbuffer[led + dist] >= C_BULLET_Z) catch = dist;
        if (zbuffer[led - dist] >= C_BULLET_Z) catch = -dist;
        dist++;
    }
    if (catch == 999)
    {
        printf("bullet missed\n");
        return 1;
    }
    else
    {
        printf("Bullet caught %i\n", led + catch);
        bullet_into_jewel(led + catch);
        return 0;
    }
}

const int Match3_Game_swap_jewels(int led, int dir)
{
    if (zbuffer[led] < C_BULLET_Z && zbuffer[led + dir] < C_BULLET_Z &&
        zbuffer[led] > 0 && zbuffer[led + dir] > 0)
    {
        printf("Switching bullets at position %i\n", led);
        return swap_jewels(led, dir);
    }
    else
    {
        printf("Cannot switch jewels here\n");
        return 1;
    }
}

static void delete_bullet(int bullet)
{
    //printf("Before: "); for (int b = 0; b < n_bullets; ++b) printf("%i ", bullets[b].jewel.type); printf("\n");
    //printf("deleting bullet %i of type %i, n_bullets %i\n", bullet, bullets[bullet].jewel.type, n_bullets);
    ASSERT_M3(bullet < n_bullets, (void)0);
    for (int b = bullet; b < n_bullets - 1; ++b)
    {
        bullets[b] = bullets[b + 1];
    }
    n_bullets--;
    //printf("After: "); for (int b = 0; b < n_bullets; ++b) printf("%i ", bullets[b].jewel.type); printf("\n");
}

static void update_bullets()
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
        //TODO -- update segment shift if inside segment, otherwise discombobulation is lost
    }
}

static void bullet_into_jewel(int bullet_led)
{
    //insert new jewel in the field
    int bullet = zbuffer[bullet_led] - C_BULLET_Z;
    while (zbuffer[bullet_led] >= C_BULLET_Z) 
        bullet_led++;

    if (zbuffer[bullet_led] == 0)  //we are not in a segment, nothing can be inserted TODO -- we could check to the left of the bullet
        return;

    //zbuffer[led] = C_LED_Z + (segment << C_SEGMENT_SHIFT | pos);
    int segment, insert_pos;
    get_segment_and_position(bullet_led, &segment, &insert_pos);
    int clean_led = (int)Segments_get_position(segment) + insert_pos;
    printf("Inserting jewel type %i at position %i, dis.: %i\n", bullets[bullet].jewel_type, Segments_get_field_index(segment, insert_pos), bullet_led - clean_led);
    Field_insert_and_evaluate(segment, insert_pos, bullets[bullet].jewel_type, bullet_led - clean_led);
    //destroy the bullet
    delete_bullet(bullet);
}

static const int swap_jewels(int player_led, int dir)
{
    int left_led = (dir > 0) ? player_led : player_led - 1;
    int segment, switch_pos;
    get_segment_and_position(left_led, &segment, &switch_pos);
    int led_discomb = left_led - (int)Segments_get_position(segment) - switch_pos;
    return Field_swap_and_evaluate(segment, switch_pos, led_discomb);
}

ws2811_led_t get_jewel_color(jewel_type jewel_type)
{
    int gradient_index = (2 * match3_config.n_half_grad + 1) * jewel_type + match3_config.n_half_grad;
    return match3_game_source.basic_source.gradient.colors[gradient_index];
}


/***************** Renderigs *******************************/

static void render_players()
{
    double d = miliseconds_from_start() / (double)match3_config.player_dit_length;
    int dit = (int)d % match3_config.player_n_dits;
    double prg = d - trunc(d);

    for (int player = 0; player < match3_game_source.n_players; ++player)
    {
        //player is displayed when its pattern is on or when it is just moving
        int is_moving = Match3_Player_is_moving(player);
        if (match3_config.player_patterns[player][dit] || is_moving)
        {
            int pos = Match3_Player_get_position(player);
            unsigned char alpha = is_moving ? 0xFF : (unsigned char)(pulse_functions[match3_config.player_patterns[player][dit]](prg) * 0xFF);
            canvas3[pos] = match3_config.player_colour | alpha << 24;
        }
    }
}

static void render_bullets()
{
    for (int bullet = 0; bullet < n_bullets; ++bullet)
    {
        int led = (int)bullets[bullet].position;
        double frac = bullets[bullet].position - led;
        frac *= 2;
        while (frac > 1.) frac -= 1;
        double alpha = saw_tooth(frac);
        zbuffer[led] = C_BULLET_Z + bullet;
        int colour = get_jewel_color(bullets[bullet].jewel_type);
        canvas3[led] = colour | (int)(0xFF * alpha) << 24;
    }
}

static void render_bullets_alpha()
{
    for (int bullet = 0; bullet < n_bullets; ++bullet)
    {
        int led = (int)bullets[bullet].position;
        for (int i = 1; i < 4; ++i)
        {
            int alpha = (int)((0.25 + (double)i / 8.) * 0xFF);
            int canvas_colour = canvas3[led + i] & 0xFFFFFF; 
            canvas3[led + i] = alpha << 24 | canvas_colour;
            canvas_colour = canvas3[led - i] & 0xFFFFFF;
            canvas3[led - i] = alpha << 24 | canvas_colour;
        }
    }
}

static void render_collapsing_segments()
{
    int segment = Segments_get_next_collapsing(-1);
    while (segment > -1)
    {
        int segment_length = Segments_get_length(segment);
        int segment_position = (int)trunc(Segments_get_position(segment));
        double collapse_progress = 4. * Segments_get_collapse_progress(segment);
        while (collapse_progress > 1) collapse_progress--;
        for (int pos = 0; pos < segment_length; ++pos)
        {
            int led = pos + segment_position;
            canvas3[led] = ((int)(0xFF * collapse_progress) << 24) | match3_config.collapse_colour;;
            zbuffer[led] = 0;
#ifdef DEBUG_M3
            debug_fi_current[led] = 1 + Segments_get_field_index(segment, pos);
#endif // DEBUG_M3
        }
        segment = Segments_get_next_collapsing(segment);
    }
}

static void render_moving_segments()
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
        int hole_position = Segments_get_hole_position(segment);
        int hole_direction = -Segments_get_direction(segment);

        int led_discombobulation = 0;
        for (int pos = 0; pos < segment_length; ++pos)
        {
            int led = pos + segment_position + led_discombobulation;
            if (pos >= hole_position)
            {
                led++;
            }
            if ((led + hole_direction) >= match3_game_source.basic_source.n_leds)
                continue;

            if (pos == hole_position) //we will outptut two leds for this pos, one for the hole, one for the jewel
            {
                if (zbuffer[led + hole_direction] > 0) //if there is bullet on the hole position, we have to shift even more
                {
                    ASSERT_M3_CONTINUE(zbuffer[led + hole_direction] >= C_BULLET_Z); //we should never render one field over another
                    led += 1;
                    ++led_discombobulation;
                }
            }
            if (led >= match3_game_source.basic_source.n_leds)
                continue;
            //check collision with bullets
            while (zbuffer[led] > 0)
            {
                ASSERT_M3_CONTINUE(zbuffer[led] >= C_BULLET_Z); //we should never render one segment over another
                led += 1;
                ++led_discombobulation;
                if (led >= match3_game_source.basic_source.n_leds)
                    break;
            }

            if (led >= match3_game_source.basic_source.n_leds)
                continue;

            if (pos == 0 && led_discombobulation > 0)
            {
                //printf("segment %i, pos %i, dsc %i\n", segment, pos, led_discombobulation);
                Segments_add_shift(segment, led_discombobulation);
                segment_position += led_discombobulation;
                led_discombobulation = 0;
            }

            jewel_t jewel = Segments_get_jewel(segment, pos);
            jewel_type type = jewel.type;
            //this will transform ampl to <0, 2 * N_HALF_GRAD>
            double ampl = (sins[type] * jewel.cos_phase + coss[type] * jewel.sin_phase + 1) * match3_config.n_half_grad;
            int gradient_index = (int)ampl;
            double blend = ampl - (int)ampl;
            gradient_index += (2 * match3_config.n_half_grad + 1) * type;
            ASSERT_M3_CONTINUE(gradient_index >= type * (2 * match3_config.n_half_grad + 1));
            ASSERT_M3_CONTINUE(gradient_index < (type + 1) * (2 * match3_config.n_half_grad + 1));
            ASSERT_M3_CONTINUE(gradient_index + 1 < match3_game_source.basic_source.gradient.n_colors);
            gradient_index = (2 * match3_config.n_half_grad + 1) * type + match3_config.n_half_grad;

            canvas3[led] = 0xFF << 24 | match3_game_source.basic_source.gradient.colors[gradient_index];
            zbuffer[led] = C_LED_Z + (segment << C_SEGMENT_SHIFT | pos);

#ifdef DEBUG_M3
            debug_fi_current[led] = 1 + Segments_get_jewel_id(segment, pos);
#endif // DEBUG_M3
        }
        Segments_set_discombobulation(segment, led_discombobulation);
        segment = Segments_get_next_moving(segment);
    }
}

static void render_emitor()
{
    int from = match3_game_source.basic_source.n_leds - 1 - emitor.length;
    int to = match3_game_source.basic_source.n_leds;
    for (int led = from; led < to; ++led)
    {
        canvas3[led] = get_jewel_color(emitor.jewel_type) | 0xFF << 24;
    }
}


//! How to render the field
//!  1. render bullets
//!  2. render jewels (i.e segments), may be affected bullets
//!  3. render players (no z-checks, painted over everything)
static void render_field(int frame, ws2811_t* ledstrip)
{
    //clear z-buffer
    memset(zbuffer, 0, sizeof(int) * match3_game_source.basic_source.n_leds);
    //clear canvas
    memset(canvas3, 0, sizeof(int) * match3_game_source.basic_source.n_leds);

    //render all objects to canvas and zbuffer
    render_bullets();
    render_moving_segments();
    //render_bullets_alpha();
    render_players();
    render_emitor();
    render_collapsing_segments();

    //output canvas into leds
#ifdef DEBUG_M3
    static int leds_moved_all = 0;
    int leds_moved = 0;
#endif // DEBUG_M3

    for (int led = 0; led < match3_game_source.basic_source.n_leds; ++led)
    {
        double canvas_alpha = (double)((canvas3[led] & 0xFF000000) >> 24) / (double)0xFF;
        int canvas_colour = canvas3[led] & 0xFFFFFF;
        ledstrip->channel[0].leds[led] = multiply_rgb_color(canvas_colour, canvas_alpha);
#ifdef DEBUG_M3
        if(debug_fi_current[led] != debug_fi_previous[led]) leds_moved++;
        debug_fi_previous[led] = 0;
#endif // DEBUG_M3
    }

#ifdef DEBUG_M3
    if (frame > 2)
    {
        leds_moved_all += leds_moved;
        if(leds_moved > 6)
            printf("LEDs moved this frame %i, average %f\n", leds_moved, (double)leds_moved_all / frame);
    }
    int* tmp = debug_fi_previous;
    debug_fi_previous = debug_fi_current;
    debug_fi_current = tmp;
#else
    (void)frame;
#endif // DEBUG_M3
}

//! The update has the following phases:
//!  - if collapse is in progress, just render and check if the collapse is over
//!  - check input, start moving players and/or start swapping jewels
//!  - render field, possibly just collapsing, leaving holes for the players
//!  - render player(s), possibly just moving

int Match3GameSource_update_leds(int frame, ws2811_t* ledstrip)
{
    Match3_InputHandler_process_input();
    update_bullets();
    Segments_update();
    render_field(frame, ledstrip);
    return 1;
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
    Match3_InputHandler_init();
    match3_game_source.start_time = current_time;
    //Game_source_init_objects();
    canvas3 = malloc(sizeof(int) * match3_game_source.basic_source.n_leds);
    zbuffer = malloc(sizeof(int) * match3_game_source.basic_source.n_leds);
#ifdef DEBUG_M3
    debug_fi_current = malloc(sizeof(int) * match3_game_source.basic_source.n_leds);
    debug_fi_previous = malloc(sizeof(int) * match3_game_source.basic_source.n_leds);
#endif // DEBUG_M3

    match3_game_source.n_players = Controller_get_n_players();
    Field_init();
    Match3_Players_init();
}

void Match3GameSource_destruct()
{
    free(canvas3);
    free(zbuffer);
#ifdef DEBUG_M3
    free(debug_fi_current);
    free(debug_fi_previous);
#endif // DEBUG_M3

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