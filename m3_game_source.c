#define _CRT_SECURE_NO_WARNINGS

/********
 * TODO *
 ********
 
 [x] Splitting and merging game field, moving backwards
 [x] Emittor
 [x] Sounds
 [ ] Music
 [x] Players
 [x] Game phases/levels
 [x] Last level with clues
 [x] Better pitcher gameplay
 [x] End level/game screen
 [x] Playtesting, tune the constants
 [x] Tune colours, rendering of bullets
 [x] Fanfare at the end of level
 [x] Player highlight during game, teleport to start
 [x] Clue level, longer display, bigger spacing
 [x] Revert random bullets

*/
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <math.h>
#include <assert.h>
#ifdef __linux__
#include "ws2811.h"
#include <unistd.h>
#include <linux/input.h>
#include <fcntl.h>
#include <errno.h>
#else
#include "fakeled.h"
#endif // __linux__

#include "colours.h"
#include "common_source.h"
#include "source_manager.h"
#include "sound_player.h"
#include "controller.h"
#include "morse_source.h"

#include "m3_game_source.h"
#include "m3_field.h"
#include "m3_input_handler.h"
#include "m3_players.h"
#include "m3_bullets.h"
#include "m3_game.h"


/* Config data */
const struct Match3Config match3_config = {
    .collapse_time = 2,
    .clue_collapse_time = 15,
    .n_half_grad = 4,
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
    .retrograde_speed = -0.5,
    .slow_forward_speed = 0.1,
    .bullet_speed = 5,
    .emitor_cooldown = 500,
    .unswap_timeout = 150,
    .highlight_timeout = 500,
    .clue_colours = { 0xFFFFFF, 0xC3423F, 0x6E8D2A, 0xCAB302, 0x1686B6} //0=prosign (white), 1=street hint (red), 2=literal direction (green), 3=oblique hint (yellow), 4=separation (blue)
};

match3_LevelDefinition_t level_definitions[MATCH3_N_LEVELS] =
{
    //level 1
    {
        .field_length = 50,
        .n_gem_colours = 4,
        .same_gem_bias = 0.75,
        .speed_bias = 1,
        .start_offset = 0
    },

    //level 2
    {
        .field_length = 75,
        .n_gem_colours = 4,
        .same_gem_bias = 0.6,
        .speed_bias = 1.1,
        .start_offset = 0
    },

    //level 3
    {
        .field_length = 100,
        .n_gem_colours = 5,
        .same_gem_bias = 0.6,
        .speed_bias = 1.2,
        .start_offset = -25
    },

    //level 4
    {
        .field_length = 125,
        .n_gem_colours = 5,
        .same_gem_bias = 0.5,
        .speed_bias = 1.3,
        .start_offset = -50
    }/*,

    //level 5
    {
        .field_length = 150,
        .n_gem_colours = 6,
        .same_gem_bias = 0.5,
        .speed_bias = 1.5,
        .start_offset = -50
    }*/
};


//unambiguous 37 matches
//const jewel_type clue_level[] = { 5, 0, 0, 1, 3, 4, 4, 0, 1, 2, 2, 3, 3, 4, 5, 5, 0, 0, 2, 1, 3, 3, 4, 5, 0, 1, 1, 2, 0, 3, 3, 2, 4, 5, 0, 0, 1, 1, 4, 2, 2, 1, 3, 3, 4, 4, 2, 5, 0, 0, 2, 1, 3, 2, 3, 4, 4, 5, 5, 3, 5, 4, 2, 1, 5, 1, 0, 4, 5, 3, 0, 5, 5, 3, 4, 2, 1, 2, 0, 5, 5, 4, 4, 3, 2, 5, 1, 1, 0, 5, 4, 4, 3, 2, 1, 1, 0, 0, 2, 5, 5, 4, 3, 3, 1, 2, 2, 5, 1, 0, 5 };

//ambiguous 36 matches -- inverted order
const jewel_type clue_level[] = { 1, 5, 4, 4, 1, 2, 1, 0, 3, 5, 5, 0, 5, 0, 1, 1, 2, 2, 1, 3, 3, 0, 1, 4, 5, 5, 1, 2, 2, 1, 0, 0, 3, 1, 0, 2, 0, 2, 5, 3, 4, 2, 3, 2, 2, 3, 5, 4, 4, 0, 5, 0, 1, 2, 1, 3, 4, 4, 3, 5, 4, 3, 5, 2, 2, 1, 3, 1, 1, 2, 3, 4, 4, 0, 5, 4, 4, 3, 3, 0, 3, 4, 5, 5, 2, 0, 5, 3, 1, 2, 0, 1, 5, 5, 0, 4, 4, 5, 4, 0, 1, 3, 2, 2, 4, 3, 0, 0 };

//ambiguous 48 matches
//const jewel_type clue_level[] = { 1, 5, 4, 4, 1, 2, 1, 0, 3, 5, 5, 4, 3, 3, 4, 3, 0, 4, 5, 0, 1, 1, 2, 2, 1, 3, 3, 0, 1, 4, 5, 5, 1, 2, 2, 1, 0, 0, 3, 1, 0, 2, 0, 2, 5, 3, 4, 1, 2, 1, 0, 5, 5, 0, 5, 1, 0, 3, 2, 2, 3, 5, 4, 4, 0, 5, 0, 1, 2, 1, 3, 4, 4, 3, 5, 4, 3, 5, 2, 2, 1, 4, 5, 4, 4, 0, 5, 5, 3, 0, 0, 1, 1, 2, 3, 4, 4, 0, 5, 4, 4, 3, 3, 1, 2, 1, 1, 0, 2, 2, 3, 4, 5, 5, 2, 0, 5, 3, 1, 2, 0, 1, 5, 5, 0, 2, 3, 2, 2, 4, 3, 3, 4, 5, 4, 0, 1, 3, 2, 2, 4, 3, 0, 0 };

//const char* clue_letters[] = { "mo4 ji4", "shi1 fu0", "zhi1 jian1", "jin1 ping2", "jin1 zi4 ta3" };
//ink blot = mo4 ji4, master = shuo4 shi4 or shi1 fu0, between = zhi1 jian1, plum = "jin1 ping2", pyramid = jin1 zi4 ta3

const char clue_letters[6][6] = { 
    {'*','s','z','n','i','n'},
    {'m','h','h','-','n','z'},
    {'o','i','i','j','g','i'},
    {'j','f','j','i','-','t'},
    {'i','u','i','n','j','a'},
    {'-','-','a','p','i','.'}
};
const unsigned char clue_letter_types[6][6] = {
    {0,1,2,2,3,2},
    {1,1,2,0,3,2},
    {1,1,2,3,3,2},
    {1,1,2,3,0,2},
    {1,1,2,3,2,2},
    {0,0,2,3,2,0}
};

int clue_index[6] = { 0 };
jewel_type last_clue_type = N_GEM_COLORS;
 
 /* Config data end */


/** Private properties **/

int current_level = 0;
int end_phase_requested = 0;
#define C_MAX_ANNOUNCEMENTS 8
char announcement_queue[C_MAX_ANNOUNCEMENTS][64];
int current_announcement = 0;
int announcement_queue_end = 0;
int is_announcement_in_progress = 0;

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

double miliseconds_from_start(void)
{
    return (double)((match3_game_source.basic_source.current_time - match3_game_source.start_time) / 1000l) / 1e3;
}

void match3_announce(char* wav, char* message)
{
    announcement_queue_end = (announcement_queue_end + 1) % C_MAX_ANNOUNCEMENTS;
    sprintf(announcement_queue[announcement_queue_end], "sound/m3_%s.wav", wav);
    printf(ANSI_COLOR_RED "%s\n" ANSI_COLOR_RESET, message);
    is_announcement_in_progress = 1;
}

static void update_announcements(void)
{
    int t = SoundPlayer_play(SE_N_EFFECTS);
    if (t < 0 && (current_announcement < announcement_queue_end || (announcement_queue_end == 0 && current_announcement > 0)))
    {
        current_announcement = (current_announcement + 1) % C_MAX_ANNOUNCEMENTS;
        SoundPlayer_start(announcement_queue[current_announcement]);
        is_announcement_in_progress = 1;
    }
    else if (t < 0)
    {
        is_announcement_in_progress = 0;
    }
    else
    {
        is_announcement_in_progress = 1;
    }
}

/***************** Application Flow **********************/

int Match3_GameSource_get_n_jewels(void)
{
    return level_definitions[current_level].n_gem_colours;
}

int Match3_GameSource_is_clue_level(void)
{
    return !(current_level < MATCH3_N_LEVELS);
}

static void finish_phase(enum EMatch3GamePhase phase)
{
    if (phase == match3_game_source.game_phase)
    {
        end_phase_requested = 1;
    }
}

static void select_phase_init(void)
{
    Match3_Players_init();
    if (Match3_GameSource_is_clue_level())
    {
        match3_announce("finalLevel", "This is the final level, everyone is a switcher. Get ready.");
    }
    else
    {
        match3_announce("selectRoles", "Select your roles: X for pitcher, Y for catcher, B for switcher. Press A to highlight your cursor");
    }
    match3_game_source.level_phase = M3LP_IN_PROGRESS;
}

static void select_phase_update(void)
{
    if (is_announcement_in_progress)
        Match3_InputHandler_drain_input();
    else
        Match3_InputHandler_process_input();
    Match3_Player_process_event();
    Match3_Game_render_select();
    if (match3_game_source.level_phase == M3LP_LEVEL_WON && end_phase_requested == 0)
    {
        match3_announce("get_ready", "Get ready! Go!");
        finish_phase(M3GP_SELECT);
    }
}

static void play_phase_init(void)
{
    if (Match3_GameSource_is_clue_level())
    {
        Field_init_with_clue(clue_level, sizeof(clue_level) / sizeof(jewel_type));
    }
    else
    {
        Field_init(level_definitions[current_level]);
    }
    match3_game_source.level_phase = M3LP_IN_PROGRESS;
    printf("Starting music\n");
    SoundPlayer_start_looped("sound/KingdomZumeDeliveranceCE_loop.wav");
}

static void play_phase_update(void)
{
    Match3_InputHandler_process_input();
    int n = Match3_Player_process_event();
    if (n > 0) printf("Remaining event queue %i\n", n);
    Match3_Bullets_update();
    Segments_update();
    if (Match3_GameSource_is_clue_level())
    {
        jewel_type t = Field_get_last_match();
        if (t != last_clue_type)
        {
            if(last_clue_type != N_GEM_COLORS)
                clue_index[last_clue_type]++;
            last_clue_type = t;
            printf("New clue will be %i, clue %c\n", t, clue_letters[last_clue_type][clue_index[last_clue_type]]);
        }
        char morse_char[6] = ".....";
        int ct = 0;
        if (last_clue_type < N_GEM_COLORS)
        {
            assert(clue_index[last_clue_type] < N_GEM_COLORS);
            unsigned char c = clue_letters[last_clue_type][clue_index[last_clue_type]];
            ct = clue_letter_types[last_clue_type][clue_index[last_clue_type]];
            if (c == '*')
                strcpy(morse_char, "-.-.-"); //message start (CT)
            else if (c == '.')
                strcpy(morse_char, ".-.-."); //message end (EC)
            else if (c == '-')
                strcpy(morse_char, "-...-"); //message break (BT)
            else
                MorseSource_get_code(morse_char, toupper(c));
        }
        Match3_Game_render_clue_field(morse_char, ct);
    }
    else
    {
        Match3_Game_render_field();
    }
    if ((match3_game_source.level_phase == M3LP_LEVEL_LOST || match3_game_source.level_phase == M3LP_LEVEL_WON) && end_phase_requested == 0)
    {
        finish_phase(M3GP_PLAY);
        printf("Stopping music\n");
        SoundPlayer_stop();
    }
}

static void end_phase_init(void)
{
    //TODO -- queue announcements
    Match3_Players_init();
    switch (match3_game_source.level_phase)
    {
    case M3LP_LEVEL_LOST:
        match3_announce("lostRetry", "You lost! Press Start to retry.");
        break;
    case M3LP_LEVEL_WON:
        match3_announce("winContinue", "You won! Press Start to continue.");
        SoundPlayer_play(SE_M3_Fanfare);
        current_level++;
        if (current_level == MATCH3_N_LEVELS)
        {
            match3_announce("you_win", "Game won");
        }
        break;
    case M3LP_IN_PROGRESS:
    case M3LP_N_PHASES:
    default:
        printf("Level must be either won or lost");
        assert(0);
    }
    match3_game_source.level_phase = M3LP_IN_PROGRESS;
}

static void end_phase_update(void)
{
    if (current_level < MATCH3_N_LEVELS)
    {
        Match3_Game_render_end();
    }
    else
    {
        Match3_Game_render_end(); //TODO display something more fancy
    }
    Match3_InputHandler_process_input();
    Match3_Player_process_event();
    if (match3_game_source.level_phase == M3LP_LEVEL_WON && end_phase_requested == 0)
    {
        finish_phase(M3GP_END);
    }
}


struct
{
    void (*const init)(void);
    void (*const update)(void);
} phase_definitions[M3GP_N_PHASES] = 
{
    {
        .init = select_phase_init,
        .update = select_phase_update
    },
    {
        .init = play_phase_init,
        .update = play_phase_update
    },
    {
        .init = end_phase_init,
        .update = end_phase_update
    }
};

int Match3GameSource_update_leds(int frame, ws2811_t* ledstrip)
{
    match3_game_source.cur_frame = frame;
    update_announcements();
    phase_definitions[match3_game_source.game_phase].update();
    Match3_Game_render_leds(frame, ledstrip);
    //check phase/level progress
    if (end_phase_requested && !is_announcement_in_progress)
    {
        enum EMatch3GamePhase phase = match3_game_source.game_phase;
        match3_game_source.game_phase = ((int)phase + 1) % M3GP_N_PHASES;
        end_phase_requested = 0;
        phase_definitions[match3_game_source.game_phase].init();
    }
    return 1;
}

//! @brief Process messages from HTTP server
//! All messages have to have format <command>?<parameter>. Available commands are:
//!     win?<next_level> (0 means current level + 1)
//!     lose?<next_level> (0 means retry current level)
//!     clue?0 (starts the final (clue) level)
//! @param msg 
void Match3GameSource_process_message(const char* msg)
{
    char* sep = strchr(msg, '?');
    if (sep == NULL)
    {
        printf("Match3GameSource: message does not contain target %s\n", msg);
        return;
    }
    if ((sep - msg) >= 32)
    {
        printf("Match3GameSource: target is too long or poorly formatted: %s\n", msg);
        return;
    }
    if ((strlen(sep + 1) >= 64))
    {
        printf("Match3GameSource: message too long or poorly formatted: %s\n", msg);
        return;
    }
    char target[32];
    char payload[64];
    strncpy(target, msg, sep - msg);
    strncpy(payload, sep + 1, 64);
    target[sep - msg] = 0x0;
    payload[63] = 0x0;
    char* checkPtr;
    errno = 0;
    long val = strtol(payload, &checkPtr, 10);
    if (checkPtr == payload || errno != 0 || !checkPtr || *checkPtr != '\0')
    {
        printf("Message parameter %s could not be converted to number\n", payload);
        return;
    }
    if (!strncasecmp(target, "win", 3))
    {
        if (val == 0)
        {
            val = current_level + 1;
        }
        current_level = val - 1;
        match3_game_source.level_phase = M3LP_LEVEL_WON;
        printf("Message WIN, next level will be: %i\n", current_level + 1);
    }
    else if (!strncasecmp(target, "lose", 4))
    {
        if (val == 0)
        {
            val = current_level;
        }
        current_level = val;
        match3_game_source.level_phase = M3LP_LEVEL_LOST;
        printf("Message LOST, next level will be: %i\n", current_level);
    }
    else if (!strncasecmp(target, "clue", 4))
    {
        current_level = MATCH3_N_LEVELS - 1;
        match3_game_source.level_phase = M3LP_LEVEL_WON;
        printf("Message CLUE\n");
    }
    else
        printf("Match3GameSource: Unknown command: %s, parameter was: %s\n", target, payload);
    
}

void Match3GameSource_init(int n_leds, int time_speed, uint64_t current_time)
{
    BasicSource_init(&match3_game_source.basic_source, n_leds, time_speed, source_config.colors[M3_GAME_SOURCE], current_time);
    SoundPlayer_init(20000);
    Match3_InputHandler_init(); // inits the controller
    Match3_Game_init();

    match3_game_source.start_time = current_time;
    match3_game_source.n_players = Controller_get_n_players();
    phase_definitions[match3_game_source.game_phase].init();
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
    .level_phase = M3LP_IN_PROGRESS,
    .game_phase = M3GP_SELECT
};
