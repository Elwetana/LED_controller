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
#include "m3_players.h"

enum EPlayerType
{
    PT_Pitcher,
    PT_Catcher,
    PT_Swapper,
    PT_N_PlayerTypes
};

//! Players
typedef struct TPlayer {
    double position;
    double last_move;
    double last_catch;
    enum EPlayerType type;
} player_t;
player_t players[C_MAX_CONTROLLERS];


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
    case PT_N_PlayerTypes:
        assert(0);
        break;
    default:
        assert(0);
        break;
    }
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
    m3_print_info(player_pos);
}

//printf(ANSI_COLOR_RED     "This text is RED!"     ANSI_COLOR_RESET "\n");

void Pitcher_press_button(int player_index, enum EM3_BUTTONS button)
{
    ASSERT_M3(players[player_index].type == PT_Pitcher);
    switch (button)
    {
    case M3B_A:
        Player_fire();
        break;
    case M3B_B:
    case M3B_X:
    case M3B_Y:
    case M3B_DUP:
        Player_reload(1);
        break;
    case M3B_DRIGHT:
        break;
    case M3B_DDOWN:
        Player_reload(-1);
        break;
    case M3B_DLEFT:
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
    ASSERT_M3(players[player_index].type == PT_Catcher);
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
    case M3B_N_BUTTONS:
    default:
        printf("Switch not complete in Catcher press button");
        assert(0);
        break;
    }
}

void Swapper_press_button(int player_index, enum EM3_BUTTONS button)
{
    ASSERT_M3(players[player_index].type == PT_Swapper);
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
    case M3B_N_BUTTONS:
        break;
    default:
        break;
    }
}

void(*game_phase_action_map[PT_N_PlayerTypes])(int, enum EM3_BUTTONS) = {
    Pitcher_press_button,
    Catcher_press_button,
    Swapper_press_button
};


void Match3_Player_press_button(int player, enum EM3_BUTTONS button)
{
    enum EPlayerType player_type = players[player].type;
    assert(player_type < PT_N_PlayerTypes);
    game_phase_action_map[player_type](player, button);
}

void Match3_Players_init()
{
    double d = (double)match3_game_source.basic_source.n_leds / ((double)match3_game_source.n_players + 1.0);
    for (int i = 0; i < match3_game_source.n_players; ++i)
    {
        players[i].position = (int)d * (i + 1);
        players[i].last_move = 0;
        players[i].type = PT_N_PlayerTypes;
    }
}
