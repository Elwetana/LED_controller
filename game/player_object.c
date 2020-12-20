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

#define MAX_PLAYER_BULLETS 3

const int C_PLAYER_OBJ_INDEX = MAX_N_OBJECTS - 1;

static struct
{
    int level; //0: normal, 1: above, -1: below
    uint64_t level_change_time;
    int bullets[MAX_PLAYER_BULLETS];
    int shields[2];
} player_object;


void PlayerObject_init(enum GameModes current_mode)
{
    switch(current_mode)
    {
    case GM_LEVEL1:
    case GM_LEVEL2:
    case GM_LEVEL3:
    case GM_LEVEL_BOSS:
        assert(config.player_health_levels + 1 == config.player_ship_size);
        MovingObject_init_stopped(C_PLAYER_OBJ_INDEX, config.player_start_position, MO_BACKWARD, config.player_ship_size, ZI_Player);
        MovingObject_init_movement(C_PLAYER_OBJ_INDEX, 0, 0, MovingObject_stop);
        PulseObject_init_steady(C_PLAYER_OBJ_INDEX, config.color_index_health, config.player_ship_size);
        PulseObject_set_color(C_PLAYER_OBJ_INDEX, config.color_index_player, config.color_index_player, config.color_index_player, config.player_ship_size - 1);
        GameObject_init(C_PLAYER_OBJ_INDEX, config.player_health_levels, SF_Player);
        break;
    case GM_LEVEL1_WON:
    case GM_LEVEL2_WON:
    case GM_LEVEL3_WON:
    case GM_LEVEL_BOSS_WON:
    case GM_LEVEL_BOSS_DEFEATED:
    case GM_PLAYER_LOST:
        GameObject_delete_object(C_PLAYER_OBJ_INDEX);
        break;
    }
}

void PlayerObject_update()
{
    enum GameModes current_mode = GameObjects_get_current_mode();
    uint64_t max_hidden_time;
    switch (current_mode)
    {
    case GM_LEVEL1:
    case GM_LEVEL2:
    case GM_LEVEL3:
        max_hidden_time = 2 * 1e9; //2s
        if (player_object.level != 0 && game_source.basic_source.current_time - player_object.level_change_time > max_hidden_time)
        {
            player_object.level = 0;
            PulseObject_set_color(C_PLAYER_OBJ_INDEX, config.color_index_player, config.color_index_player, config.color_index_player, config.player_ship_size - 1);
            //printf("Dropping back\n");
        }
        break;
    case GM_LEVEL_BOSS:
        max_hidden_time = 2 * 1e9; //2s
        if (player_object.level != 0 && game_source.basic_source.current_time - player_object.level_change_time > max_hidden_time)
        {
            player_object.level = 0;
            PulseObject_set_color(C_PLAYER_OBJ_INDEX, config.color_index_player, config.color_index_player, config.color_index_player, config.player_ship_size - 1);
            GameObject_delete_object(player_object.shields[0]);
            GameObject_delete_object(player_object.shields[1]);
            //printf("Dropping back\n");
        }
        break;
    case GM_LEVEL1_WON:
    case GM_LEVEL2_WON:
    case GM_LEVEL3_WON:
    case GM_LEVEL_BOSS_DEFEATED:
    case GM_LEVEL_BOSS_WON:
    case GM_PLAYER_LOST:
        break;
    }
}

int PlayerObject_get_health()
{
    return GameObject_get_health(C_PLAYER_OBJ_INDEX);
}

void PlayerObject_move_left()
{
    if (player_object.level == 99) return;
    double pos = MovingObject_get_position(C_PLAYER_OBJ_INDEX);
    if (pos < 1. || player_object.level != 0)
        return;
    MovingObject_init_movement(C_PLAYER_OBJ_INDEX, config.player_ship_speed, (uint32_t)pos - 1, MovingObject_stop);
    //printf("moving\n");
}

void PlayerObject_move_right()
{
    if (player_object.level == 99) return;
    double pos = MovingObject_get_position(C_PLAYER_OBJ_INDEX);
    if (pos > game_source.basic_source.n_leds - (int)config.player_ship_size - 2 || player_object.level != 0)
        return;
    MovingObject_init_movement(C_PLAYER_OBJ_INDEX, config.player_ship_speed, (uint32_t)pos + 1, MovingObject_stop);
}

static int is_player_bullet(int pb_index)
{
    return !(player_object.bullets[pb_index] == 0 ||
        GameObject_is_deleted(player_object.bullets[pb_index]) ||
        GameObject_get_stencil_flag(player_object.bullets[pb_index]) == SF_EnemyProjectile);
}

void PlayerObject_cloak()
{
    for (int pb_index = 0; pb_index < MAX_PLAYER_BULLETS; ++pb_index)
    {
        if (is_player_bullet(pb_index))
        {
            GameObject_delete_object(pb_index);
        }
    }
    player_object.level = 99;
    player_object.level_change_time = game_source.basic_source.current_time;
    PulseObject_set_color(C_PLAYER_OBJ_INDEX, config.color_index_player, config.color_index_player, config.color_index_W, config.player_ship_size - 1);

    double pos = MovingObject_get_position(C_PLAYER_OBJ_INDEX);
    int len = MovingObject_get_length(C_PLAYER_OBJ_INDEX);
    int shield_len = 3;

    player_object.shields[0] = GameObject_new_background_index();
    if (player_object.shields[0] == -1) return;
    GameObject_init(player_object.shields[0], 1, SF_Background);
    MovingObject_init_stopped(player_object.shields[0], pos - shield_len - 1, MO_BACKWARD, shield_len, ZI_Player);
    PulseObject_init_steady(player_object.shields[0], config.color_index_W, shield_len);

    player_object.shields[1] = GameObject_new_background_index();
    if (player_object.shields[1] == -1) return;
    GameObject_init(player_object.shields[1], 1, SF_Background);
    MovingObject_init_stopped(player_object.shields[1], pos + len + 1, MO_FORWARD, shield_len, ZI_Player);
    PulseObject_init_steady(player_object.shields[1], config.color_index_W, shield_len);
}

static void set_level(int level)
{
    if (player_object.level != 0)
    {
        return; //cannot change level when hid
    }
    player_object.level = level;
    player_object.level_change_time = game_source.basic_source.current_time;
    //printf("hiding\n");
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

static void PlayerObject_fire_bullet(int color)
{
    enum MovingObjectFacing f = MovingObject_get_facing(C_PLAYER_OBJ_INDEX);
    double pos = MovingObject_get_position(C_PLAYER_OBJ_INDEX) + ((f == MO_BACKWARD) ? 0 : config.player_ship_size);
    if (player_object.level != 0)
    {
        int i = GameObject_new_projectile_index();
        if (i == -1) return;
        GameObject_init(i, 1, SF_Background);
        MovingObject_init_stopped(i, pos, f, 1, ZI_always_visible);
        PulseObject_init(i, 1, PM_FADE, 6, 100, 0, 0, 1, GameObject_delete_object);
        PulseObject_set_color_all(i, config.color_index_Y, config.color_index_M, config.color_index_K, 1);
        return;
    }

    int pb_index = 0;
    int pb_min_index = -1;
    uint64_t pb_min_time = UINT64_MAX;
    while (pb_index < MAX_PLAYER_BULLETS)
    {
        if (!is_player_bullet(pb_index)) //we found an empty slot
            break;
        uint64_t pb_time = GameObject_get_time(player_object.bullets[pb_index]);
        if (pb_time < pb_min_time)
        {
            pb_min_time = pb_time;
            pb_min_index = pb_index;
        }
        pb_index++;
    }

    if (pb_index == MAX_PLAYER_BULLETS) //there was no empty slot, we will delete the oldest bullet
    {
        pb_index = pb_min_index;
        GameObject_delete_object(player_object.bullets[pb_index]);
    }

    int i = GameObject_new_projectile_index();
    if (i == -1) return;

    player_object.bullets[pb_index] = i;
    GameObject_init(i, 1, SF_PlayerProjectile);
    GameObject_mark(i, 2 << color);
    MovingObject_init_stopped(i, pos, f, 1, ZI_Projectile);
    int color_index = (int[]){ config.color_index_R, config.color_index_G, config.color_index_B } [color];
    PulseObject_init_steady(i, color_index, 1);
    int target = (f == MO_BACKWARD) ? 3 : game_source.basic_source.n_leds - 3;
    MovingObject_init_movement(i, config.enemy_speed, target, GameObject_delete_object);
    //printf("firing bullet %i\n", i);
}

void PlayerObject_fire_bullet_red()
{
    PlayerObject_fire_bullet(0);
}

void PlayerObject_fire_bullet_green()
{
    PlayerObject_fire_bullet(1);
}

void PlayerObject_fire_bullet_blue()
{
    PlayerObject_fire_bullet(2);
}

int PlayerObject_is_hit(int bullet)
{
    int mark = GameObject_get_mark(bullet);
    //printf("mark %i, player %i\n", mark, player_object.level);
    if (GameObjects_get_current_mode() == GM_LEVEL_BOSS)
    {
        //on boss level all bullets hit, only special attack does not hit when the player is cloaked
        if (player_object.level == 99 && mark & 2 && mark & 4 && mark & 8)
            return 0;
        return 1;
    }
    int level = 1 << (2 - player_object.level); //this is shift by 1, 2 or 3, i.e. 2, 4 or 8
    return mark & level;
}

void PlayerObject_take_hit(int pi)
{
    assert(pi == C_PLAYER_OBJ_INDEX);
    (void)pi;

    void (*callback)(int);
    int health = 4;// GameObject_take_hit(C_PLAYER_OBJ_INDEX);
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
