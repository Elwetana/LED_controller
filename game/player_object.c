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
#include "moving_object.h"
#include "stencil_handler.h"
#include "pulse_object.h"
#include "game_object.h"
#include "game_source.h"
#include "player_object.h"

const int C_PLAYER_OBJ_INDEX = MAX_N_OBJECTS - 1;

void PlayerObject_init()
{
    assert(config.player_health_levels + 1 == config.player_ship_size);
    MovingObject_init_stopped(C_PLAYER_OBJ_INDEX, config.player_start_position, MO_BACKWARD, config.player_ship_size, 1);
    MovingObject_init_movement(C_PLAYER_OBJ_INDEX, 0, 0, MovingObject_stop);
    PulseObject_init_steady(C_PLAYER_OBJ_INDEX, config.color_index_G, config.player_ship_size);
    PulseObject_set_color(C_PLAYER_OBJ_INDEX, 0, 0, config.color_index_player, config.player_ship_size - 1);
    GameObject_init(C_PLAYER_OBJ_INDEX, config.player_health_levels, SF_Player);
}

int PlayerObject_get_health()
{
    return GameObject_get_health(C_PLAYER_OBJ_INDEX);
}


void PlayerObject_take_hit(int i)
{
    assert(i == C_PLAYER_OBJ_INDEX);
    (void)i;

    void (*callback)(int);
    int health = GameObject_take_hit(C_PLAYER_OBJ_INDEX);
    if (health <= 0)
    {
        //game over
        callback = GameSource_set_mode_player_lost;
        health = 0;
    }
    else
    {
        callback = NULL;
    }
    PulseObject_init(C_PLAYER_OBJ_INDEX, 1, PM_ONCE, 3, 500, 0, 0, 1, callback);
    //player body with health = 3 and size = 6:
    // R R G G G H
    // 0 1 2 3 4 5
    for (int i = 0; i < ((int)config.player_health_levels - health); ++i)
    {
        PulseObject_set_color(C_PLAYER_OBJ_INDEX, 
            game_source.basic_source.gradient.colors[config.color_index_G],
            game_source.basic_source.gradient.colors[config.color_index_R],
            game_source.basic_source.gradient.colors[config.color_index_R], i);
    }
}
