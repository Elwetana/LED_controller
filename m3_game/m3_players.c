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


#include "controller.h"
#include "common_source.h"
#include "m3_game_source.h"
#include "m3_bullets.h"
#include "m3_game.h"
#include "m3_players.h"

enum EPlayerType
{
    PT_Pitcher,
    PT_Catcher,
    PT_Swapper,
    PT_Universal,
    PT_N_PlayerTypes
};

//! Players
typedef struct TPlayer {
    double position;
    double last_move;
    double last_catch;
    enum EPlayerType type;
    unsigned char is_ready;
} player_t;
player_t players[C_MAX_CONTROLLERS];

struct {
    int player_index;
    double highlight_start;
} player_highlight;

const int Match3_Player_get_highlight(void)
{
    if (miliseconds_from_start() - player_highlight.highlight_start < match3_config.highlight_timeout)
        return player_highlight.player_index;
    return -1;
}


const int Match3_Player_get_position(int player_index)
{
    return (int)players[player_index].position;
}

const int Match3_Player_is_moving(int player_index)
{
    return (miliseconds_from_start() - players[player_index].last_move) < 2 * match3_config.player_move_cooldown;
}

static int PlayerType_can_move(enum EPlayerType pt)
{
    switch (pt)
    {
    case PT_Pitcher:
        return 0;
    case PT_Catcher:
        return 1;
    case PT_Swapper:
        return 1;
    case PT_Universal:
        return 1;
    case PT_N_PlayerTypes:
        assert(0);
        break;
    default:
        assert(0);
        break;
    }
    return 0;
}

/* player input handling */

void Match3_Player_move(int player_index, signed char direction)
{
    //printf("Player %i moved in direction %i\n", player_index, direction);
    ASSERT_M3(player_index <= match3_game_source.n_players, (void)0);
    ASSERT_M3(direction * direction == 1, (void)0);
    if (!PlayerType_can_move(players[player_index].type))
        return;
    double t = miliseconds_from_start();
    if (t - players[player_index].last_move > match3_config.player_move_cooldown)
    {
        players[player_index].position += direction;
        players[player_index].last_move = t;
    }
    players[player_index].position = max(0, players[player_index].position);
    players[player_index].position = min(players[player_index].position, match3_game_source.basic_source.n_leds - 1 - Match3_Emitor_get_length());
}

static void Player_catch(int player_index)
{
    //printf("Catch bullet attempted\n");
    double t = miliseconds_from_start();
    if (t - players[player_index].last_catch < match3_config.player_catch_cooldown)
        return;
    players[player_index].last_catch = t;

    int player_pos = (int)players[player_index].position;
    Match3_Game_catch_bullet(player_pos);
}

static void Player_fire()
{
    Match3_Emitor_fire();
}

static void Player_reload(int direction)
{
    Match3_Emitor_reload(direction);
}

static void Player_swap_jewels(int player_index, int direction)
{
    int led = (int)players[player_index].position;
    Match3_Game_swap_jewels(led, direction);
}

static void print_info(int player_index)
{
    int player_pos = (int)players[player_index].position;
    Match3_print_info(player_pos);
}


/** Handlers for normal play **/

void Pitcher_press_button(int player_index, enum EM3_BUTTONS button)
{
    ASSERT_M3_CONTINUE(players[player_index].type == PT_Pitcher);
    switch (button)
    {
    case M3B_A:
        Player_fire();
        break;
    case M3B_B:
    case M3B_X:
    case M3B_Y:
        break;
    case M3B_DUP:
        Player_reload(1);
        break;
    case M3B_DRIGHT:
        break;
    case M3B_DDOWN:
        Player_reload(-1);
        break;
    case M3B_DLEFT:
    case M3B_START:
        break;
    case M3B_N_BUTTONS:
    default:
        printf("Switch not complete in Pitcher press button");
        assert(0);
        break;
    }
}

void Catcher_press_button(int player_index, enum EM3_BUTTONS button)
{
    ASSERT_M3_CONTINUE(players[player_index].type == PT_Catcher);
    switch (button)
    {
    case M3B_A:
        Player_catch(player_index);
        break;
    case M3B_B:
    case M3B_X:
    case M3B_Y:
    case M3B_DUP:
        break;
    case M3B_DRIGHT:
        Match3_Player_move(player_index, +1);
        break;
    case M3B_DDOWN:
        break;
    case M3B_DLEFT:
        Match3_Player_move(player_index, -1);
        break;
    case M3B_START:
        break;
    case M3B_N_BUTTONS:
    default:
        printf("Switch not complete in Catcher press button");
        assert(0);
        break;
    }
}

void Swapper_press_button(int player_index, enum EM3_BUTTONS button)
{
    ASSERT_M3_CONTINUE(players[player_index].type == PT_Swapper);
    switch (button)
    {
    case M3B_A:
        break;
    case M3B_B:
        Player_swap_jewels(player_index, +1);
        break;
    case M3B_X:
        Player_swap_jewels(player_index, -1);
        break;
    case M3B_Y:
        break;
    case M3B_DUP:
        break;
    case M3B_DRIGHT:
        Match3_Player_move(player_index, +1);
        break;
    case M3B_DDOWN:
        break;
    case M3B_DLEFT:
        Match3_Player_move(player_index, -1);
        break;
    case M3B_START:
        break;
    case M3B_N_BUTTONS:
    default:
        printf("Switch not complete in Catcher press button");
        assert(0);
        break;
    }
}

//! @brief This is only for debug
//! @param player_index 
//! @param  
void Universal_press_button(int player_index, enum EM3_BUTTONS button)
{
    ASSERT_M3_CONTINUE(players[player_index].type == PT_Universal);
    switch (button)
    {
    case M3B_A:
        Player_catch(player_index);
        break;
    case M3B_B:
        Player_fire();
        break;
    case M3B_X:
        break;
    case M3B_Y:
        break;
    case M3B_DUP:
        Player_reload(1);
        break;
    case M3B_DRIGHT:
        Player_swap_jewels(player_index, +1);
        break;
    case M3B_DDOWN:
        Player_reload(-1);
        break;
    case M3B_DLEFT:
        Player_swap_jewels(player_index, -1);
        break;
    case M3B_START:
        break;
    case M3B_N_BUTTONS:
    default:
        printf("Switch not complete in Catcher press button");
        assert(0);
        break;
    }
}


/** Select phase handlers **/

static int is_valid_assignment()
{
    int n_pitchers = 0;
    int n_catchers = 0;
    int n_univesal = 0;
    int n_ready = 0;
    for (int pi = 0; pi < match3_game_source.n_players; ++pi)
    {
        if (players[pi].type == PT_Catcher) n_catchers++;
        if (players[pi].type == PT_Pitcher) n_pitchers++;
        if (players[pi].type == PT_Universal) n_univesal++;
        if (players[pi].is_ready == 1) n_ready++;
    }
    char buf[64];
    if (n_ready != match3_game_source.n_players)
    {
        //don't announce anything
        return -1;
    }
#ifndef DEBUG_M3
    if (n_pitchers != 1)
    {
        match3_announce("The level cannot start, there must be exactly one pitcher");
        sprintf(buf, "There is currently %i pitchers", n_pitchers);
        match3_announce(buf);
        return -2;
    }
    if (n_catchers == 0)
    {
        match3_announce("The level cannot start, there must be at least one catcher");
        return -3;
    }
    if (n_univesal > 0)
    {
        match3_announce("The level cannot start, not all players have selected their professions");
        return -4;
    }
#endif // !DEBUG_M3
    return 1;
}


static void assign_type(int player_index, enum EPlayerType player_type)
{
    players[player_index].type = player_type;
    players[player_index].is_ready = 0;
    char buf[64];
    sprintf(buf, "Player %i become %i", player_index, (int)player_type);
    match3_announce(buf);
}

static void end_selection(int player_index)
{
    players[player_index].is_ready = 1;
    char buf[64];
    sprintf(buf, "Player %i is ready", player_index);
    match3_announce(buf);
    if (is_valid_assignment() == 1)
    {
        Match3_GameSource_finish_phase(M3GP_SELECT);
    }
}

static void highlight_me(int player_index)
{
    player_highlight.player_index = player_index;
    player_highlight.highlight_start = miliseconds_from_start();
}


void Select_phase_press_button(int player_index, enum EM3_BUTTONS button)
{
    switch (button)
    {
    case M3B_A:
        highlight_me(player_index);
        break;
    case M3B_B:
        assign_type(player_index, PT_Swapper);
        break;
    case M3B_X:
        assign_type(player_index, PT_Catcher);
        break;
    case M3B_Y:
        assign_type(player_index, PT_Pitcher);
        break;
    case M3B_DUP:
        break;
    case M3B_DRIGHT:
        break;
    case M3B_DDOWN:
        break;
    case M3B_DLEFT:
        break;
    case M3B_START:
        end_selection(player_index);
        break;
    case M3B_N_BUTTONS:
    default:
        printf("Switch not complete in Select phase press button");
        assert(0);
        break;
    }
}

/** End phase handlers **/

void End_phase_press_button(int player_index, enum EM3_BUTTONS button)
{
    switch (button)
    {
    case M3B_A:
        break;
    case M3B_B:
        break;
    case M3B_X:
        break;
    case M3B_Y:
        break;
    case M3B_DUP:
        break;
    case M3B_DRIGHT:
        break;
    case M3B_DDOWN:
        break;
    case M3B_DLEFT:
        break;
    case M3B_START:
        end_selection(player_index);
        break;
    case M3B_N_BUTTONS:
    default:
        printf("Switch not complete in Select phase press button");
        assert(0);
        break;
    }
}


typedef void(*action_map[PT_N_PlayerTypes])(int, enum EM3_BUTTONS);
action_map game_phase_action_maps[M3GP_N_PHASES];

void Match3_Player_press_button(int player, enum EM3_BUTTONS button)
{
    assert(players[player].type < PT_N_PlayerTypes);
    game_phase_action_maps[match3_game_source.game_phase][players[player].type](player, button);
}

void Match3_Players_init(void)
{
    double d = (double)match3_game_source.basic_source.n_leds / ((double)match3_game_source.n_players + 1.0);
    for (int i = 0; i < match3_game_source.n_players; ++i)
    {
        players[i].position = (int)d * (i + 1);
        players[i].last_move = 0;
        players[i].type = PT_Universal;
        players[i].is_ready = 0;
    }
    game_phase_action_maps[M3GP_SELECT][PT_Pitcher] = Select_phase_press_button;
    game_phase_action_maps[M3GP_SELECT][PT_Catcher] = Select_phase_press_button;
    game_phase_action_maps[M3GP_SELECT][PT_Swapper] = Select_phase_press_button;
    game_phase_action_maps[M3GP_SELECT][PT_Universal] = Select_phase_press_button;

    game_phase_action_maps[M3GP_PLAY][PT_Pitcher] = Pitcher_press_button;
    game_phase_action_maps[M3GP_PLAY][PT_Catcher] = Catcher_press_button;
    game_phase_action_maps[M3GP_PLAY][PT_Swapper] = Swapper_press_button;
    game_phase_action_maps[M3GP_PLAY][PT_Universal] = Universal_press_button;

    game_phase_action_maps[M3GP_END][PT_Pitcher] = End_phase_press_button;
    game_phase_action_maps[M3GP_END][PT_Catcher] = End_phase_press_button;
    game_phase_action_maps[M3GP_END][PT_Swapper] = End_phase_press_button;
    game_phase_action_maps[M3GP_END][PT_Universal] = End_phase_press_button;
}




