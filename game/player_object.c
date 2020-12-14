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
#include "game_object.h"
#include "moving_object.h"
#include "stencil_handler.h"
#include "pulse_object.h"
#include "game_source.h"
#include "player_object.h"

const int C_PLAYER_OBJ_INDEX = MAX_N_OBJECTS - 1;

static struct
{
    int level; //0: normal, 1: above, -1: below
    uint64_t level_change_time;
} player_object;

void PlayerObject_init(enum GameModes current_mode)
{
    switch(current_mode)
    {
    case GM_LEVEL1:
    case GM_LEVEL2:
        assert(config.player_health_levels + 1 == config.player_ship_size);
        MovingObject_init_stopped(C_PLAYER_OBJ_INDEX, config.player_start_position, MO_BACKWARD, config.player_ship_size, 1);
        MovingObject_init_movement(C_PLAYER_OBJ_INDEX, 0, 0, MovingObject_stop);
        PulseObject_init_steady(C_PLAYER_OBJ_INDEX, config.color_index_health, config.player_ship_size);
        PulseObject_set_color(C_PLAYER_OBJ_INDEX, config.color_index_player, config.color_index_player, config.color_index_player, config.player_ship_size - 1);
        GameObject_init(C_PLAYER_OBJ_INDEX, config.player_health_levels, SF_Player);
        break;
    case GM_LEVEL1_WON:
    case GM_LEVEL2_WON:
    case GM_PLAYER_LOST:
        GameObject_delete_object(C_PLAYER_OBJ_INDEX);
        break;
    }
}

void PlayerObject_update()
{
    uint64_t max_hidden_time = 2 * 1e9; //2s
    if (player_object.level != 0 && game_source.basic_source.current_time - player_object.level_change_time > max_hidden_time)
    {
        player_object.level = 0;
        PulseObject_set_color(C_PLAYER_OBJ_INDEX, config.color_index_player, config.color_index_player, config.color_index_player, config.player_ship_size - 1);
        //printf("Dropping back\n");
    }
}

int PlayerObject_get_health()
{
    return GameObject_get_health(C_PLAYER_OBJ_INDEX);
}

int PlayerObject_is_hit(int bullet)
{
    int mark = GameObject_get_mark(bullet);
    int level = ((mark & 2) > 0) + ((mark & 4) > 0) + ((mark & 8) > 0); //this will be 1, 2 or 3
    assert(level > 0);
    level -= 2;
    return player_object.level == level;
}

void PlayerObject_move_left()
{
    double pos = MovingObject_get_position(C_PLAYER_OBJ_INDEX);
    if (pos < 1. || player_object.level != 0)
        return;
    MovingObject_init_movement(C_PLAYER_OBJ_INDEX, config.player_ship_speed, (uint32_t)pos - 1, MovingObject_stop);
    //printf("moving\n");
}

void PlayerObject_move_right()
{
    double pos = MovingObject_get_position(C_PLAYER_OBJ_INDEX);
    if (pos > game_source.basic_source.n_leds - config.player_ship_size - 2 || player_object.level != 0)
        return;
    MovingObject_init_movement(C_PLAYER_OBJ_INDEX, config.player_ship_speed, (uint32_t)pos + 1, MovingObject_stop);
}


static void set_level(int level)
{
    if (player_object.level == 0)
    {
        player_object.level = level;
        player_object.level_change_time = game_source.basic_source.current_time;
        //printf("hiding\n");
    }
    PulseObject_set_color(C_PLAYER_OBJ_INDEX, config.color_index_player, config.color_index_player, config.color_index_player + level, config.player_ship_size - 1);
}

void PlayerObject_hide_above()
{
    set_level(1);
}
void PlayerObject_hide_below()
{
    set_level(-1);
}


void PlayerObject_take_hit(int pi)
{
    assert(pi == C_PLAYER_OBJ_INDEX);
    (void)pi;

    void (*callback)(int);
    int health = GameObject_take_hit(C_PLAYER_OBJ_INDEX);
    if (health <= 0)
    {
        //game over
        callback = GameObjects_set_mode_player_lost;
        health = 0;
    }
    else
    {
        callback = NULL;
    }
    PulseObject_init(C_PLAYER_OBJ_INDEX, 1, PM_ONCE, 5, 500, 0, 0, 1, callback);
    //player body with health = 3 and size = 6:
    // R R G G G H
    // 0 1 2 3 4 5
    int dmg = (int)config.player_health_levels - health;
    for (int i = 0; i < dmg; ++i)
    {
        PulseObject_set_color(C_PLAYER_OBJ_INDEX, config.color_index_health, config.color_index_health+1, config.color_index_health+1, i);
    }
    for (int i = dmg; i < (int)config.player_health_levels; ++i)
    {
        PulseObject_set_color(C_PLAYER_OBJ_INDEX, config.color_index_health, config.color_index_health, config.color_index_health, i);
    }
}
