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
#include "game_source.h"
#include "game_object.h"
#include "moving_object.h"
#include "stencil_handler.h"
#include "pulse_object.h"
#include "callbacks.h"
#include "player_object.h"

/*!
 * @brief Handlers are indexed in_stencil_object_index * SF_N_FLAGS + object_being_checked_index
 * All handlers take pointers in this order and
 * @return  1 - use the new index
 *          2 - use the new index and erase the old index from here to end
 *          0 - keep the use already written index
 */
static int (*stencil_handlers[SF_N_FLAGS * SF_N_FLAGS])(int, int);


static int StencilHandler_impossible(int obj1, int obj2)
{
    (void)obj1;
    (void)obj2;
    assert(0); //this is a handler that should never be called
    return -1;
}

static int StencilHandler_player_is_hit(int projectile, int player)
{
    assert(player == C_PLAYER_OBJ_INDEX);
    if (GameObject_get_mark(projectile) & 1) return 1; //this projectile was already processed
    //we notify the objects; objects have to handle all effects
    if (PlayerObject_is_hit(projectile))
    {
        GameObject_mark(projectile, 1);
        MovingObject_target_hit(projectile, player, OnArrival_stop_and_explode);
        PlayerObject_take_hit(player);
    }
    return 1;
}

static int StencilHandler_enemy_is_hit(int enemy, int projectile)
{
    if (GameObject_get_mark(projectile) & 1) return 1; //this projectile was already processed
    GameObjects_boss_hit(enemy);
    GameObject_mark(projectile, 1);
    return 0;
}

/* There can be three outcomes:
 *    i. bullet 1 continues, bullet 2 is destroyed
 *   ii. bullet 2 continues, bullet 1 is destroyed
 *  iii. both bullets are destroyed
 */
static int StencilHandler_bullet_collision(int bullet1, int bullet2)
{
    if (GameObject_get_mark(bullet1) & 1) return 1; //this projectile was already processed
    if (GameObject_get_mark(bullet2) & 1) return 1;

    int result = GameObject_resolve_projectile_collision(bullet1, bullet2);
    printf("bullet collision: %i vs %i: %i\n", bullet1, bullet2, result);
    if (result == 0)
        return 1;

    void(*bullet1_callback)(int) = (result == 3 || result == 2) ? OnArrival_blink_and_die : OnArrival_blink_and_continue;
    void(*bullet2_callback)(int) = (result == 3 || result == 1) ? OnArrival_blink_and_die : OnArrival_blink_and_continue;

    MovingObject_target_hit(bullet1, bullet2, bullet1_callback);
    MovingObject_target_hit(bullet2, bullet1, bullet2_callback);
    GameObject_mark(bullet1, 1);
    GameObject_mark(bullet2, 1);
    return 1;
}

static int StencilHandler_player_reached_stargate(int gate, int player)
{
    assert(player == C_PLAYER_OBJ_INDEX);
    assert(gate == 0);
    GameObjects_player_reached_gate();
    return 1;
}

static void Stencil_erase_object(int start_led, int dir)
{
    int stencil_index = canvas[start_led].stencil;
    int led = start_led;
    while (canvas[led].stencil == stencil_index)
    {
        canvas[led].stencil = -1;
        canvas[led].object_index = -1;
        led += dir;
    }
}

void Stencil_stencil_test(int object_index, int stencil_flag)
{
    assert(object_index < MAX_N_OBJECTS);
    if (object_index >= MAX_N_OBJECTS)
    {
        printf("Invalid index passed to stencil_test\n");
        return;
    }
    
    int led_start, led_end, dir;
    MovingObject_get_move_results(object_index, &led_start, &led_end, &dir);
    assert(led_start >= 0 && led_start < game_source.basic_source.n_leds);
    assert(led_end >= 0 && led_end < game_source.basic_source.n_leds);
    int other_index = -1;
    int result_stencil = -1;
    int result_index = -1;
    for (int led = led_start; led <= led_end; ++led)
    {
        if (canvas[led].object_index == -1)
        {
            canvas[led].stencil = stencil_flag;
            canvas[led].object_index = object_index;
            continue;
        }
        //we have a collision and we have to handle it
        if (canvas[led].object_index == other_index) //we've solved this collision here before
        {
            canvas[led].stencil = result_stencil;
            canvas[led].object_index = result_index;
            continue;
        }
        other_index = canvas[led].object_index;
        int handler_index = canvas[led].stencil * SF_N_FLAGS + stencil_flag;
        int res;
        if (!stencil_handlers[handler_index]) //we don't have a handler, default option is to replace the old stencil
        {
            res = 1;
        }
        else
        {
            res = stencil_handlers[handler_index](other_index, object_index);
        }
        switch (res)
        {
        case 2:
            Stencil_erase_object(led, dir);
            result_stencil = stencil_flag;
            result_index = object_index;
            break;
        case 1:
            result_stencil = stencil_flag;
            result_index = object_index;
            break;
        case 0:
            result_stencil = canvas[led].stencil;
            result_index = other_index;
            break;
        }
        canvas[led].stencil = result_stencil;
        canvas[led].object_index = result_index;
    }
}

void Stencil_init(enum GameModes current_mode)
{
    for (int i = 0; i < SF_N_FLAGS * SF_N_FLAGS; ++i)
    {
        stencil_handlers[i] = NULL;
    }
    switch (current_mode)
    {
    case GM_LEVEL1:
    case GM_LEVEL2:
    case GM_LEVEL3:
        stencil_handlers[SF_Player * SF_N_FLAGS + SF_Player] = StencilHandler_impossible;
        stencil_handlers[SF_EnemyProjectile * SF_N_FLAGS + SF_Player] = StencilHandler_player_is_hit;
        stencil_handlers[SF_Enemy * SF_N_FLAGS + SF_Player] = StencilHandler_player_reached_stargate;
        break;
    case GM_LEVEL_BOSS:
        stencil_handlers[SF_Player * SF_N_FLAGS + SF_Player] = StencilHandler_impossible;
        stencil_handlers[SF_EnemyProjectile * SF_N_FLAGS + SF_Player] = StencilHandler_player_is_hit;
        stencil_handlers[SF_EnemyProjectile * SF_N_FLAGS + SF_PlayerProjectile] = StencilHandler_bullet_collision;
        stencil_handlers[SF_PlayerProjectile * SF_N_FLAGS + SF_EnemyProjectile] = StencilHandler_bullet_collision;
        stencil_handlers[SF_Enemy * SF_N_FLAGS + SF_PlayerProjectile] = StencilHandler_enemy_is_hit;
    case GM_LEVEL1_WON:
    case GM_LEVEL2_WON:
    case GM_LEVEL3_WON:
    case GM_LEVEL_BOSS_WON:
    case GM_PLAYER_LOST:
        break;
    }


}

