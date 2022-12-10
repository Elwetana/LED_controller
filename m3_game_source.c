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
#include "m3_bullets.h"
#include "m3_game.h"


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
/* Config data end */


/** Private properties **/

enum EMatch3GamePhase
{
    M3GP_SELECT,
    M3GP_PLAY,
    M3GP_END,
    M3GP_N_PHASES
} current_phase = M3GP_SELECT;
int current_level = 0;

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
 *      note how the distance between 5 and 6 increased from frame 70 on, the discombobulation of the segment is permanent, 
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
 * 
 *  In the end, we have a combination of 2.a and 3
 * 
 * ==Application flow==
 * 
 * 
 * 
 */

const int C_LED_Z = 1;          //!< 0 means there is nothing in z buffer, so led with index 0 must be 1
const int C_SEGMENT_SHIFT = 10; //!< z buffer for jewels is (segment_index << C_SEGMENT_SHIFT) | field_index
const int C_BULLET_Z = N_MAX_SEGMENTS << 10;    //!< bullets z index will be C_BULLET_Z + bullet


/************** Utility functions ************************/

double miliseconds_from_start()
{
    return (double)((match3_game_source.basic_source.current_time - match3_game_source.start_time) / 1000l) / 1e3;
}

/*********************************************************/

static void level_won()
{
    printf(ANSI_COLOR_RED "YOU WIN" ANSI_COLOR_RESET);
}


static void level_lost()
{
    printf(ANSI_COLOR_RED "YOU LOSE" ANSI_COLOR_RESET);
}


/***************** Application Flow **********************/



match3_LevelDefinition_t level_definitions[MATCH3_N_LEVELS] = 
{
    {
        .field_length = 100,
        .n_gem_colours = 4,
        .same_gem_bias = 0.75,
        .speed_bias = 1,
        .start_offset = -50
    }
};


static void select_update(int frame, ws2811_t* ledstrip)
{

    Match3_Game_render_leds(frame, ledstrip);
}

static void play_update(int frame, ws2811_t* ledstrip)
{
    Match3_InputHandler_process_input();
    Match3_Bullets_update();
    Segments_update();
    Match3_Game_render_field();
    Match3_Game_render_leds(frame, ledstrip);
}

static void play_init()
{
    Field_init(level_definitions[current_level]);
    Match3_Players_init();
}


struct
{
    const void (*init)();
    const void (*update)(int frame, ws2811_t* ledstrip);
} phase_definitions[M3GP_N_PHASES - 1] = 
{
    {
        .init = play_init,
        .update = play_update
    }
};



//! The update has the following phases:
//!  - if collapse is in progress, just render and check if the collapse is over
//!  - check input, start moving players and/or start swapping jewels
//!  - render field, possibly just collapsing, leaving holes for the players
//!  - render player(s), possibly just moving

int Match3GameSource_update_leds(int frame, ws2811_t* ledstrip)
{
    phase_definitions[current_phase].update(frame, ledstrip);
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
    Match3_InputHandler_init(); // inits the controller
    Match3_Game_init();

    match3_game_source.start_time = current_time;
    match3_game_source.n_players = Controller_get_n_players();
    phase_definitions[current_phase].init();
}

void Match3GameSource_destruct()
{
    Match3_Game_destruct();
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

match3_GameSource_t match3_game_source = {
    .basic_source.construct = Match3GameSource_construct,
    .level_phase = M3LP_IN_PROGRESS
};
