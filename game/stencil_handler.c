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
#include "game_source_priv.h"
#include "game_source.h"

enum StencilFlags
{
    SF_Background,
    SF_Player,
    SF_PlayerProjectile,
    SF_Enemy,
    SF_EnemyProjectile,
    SF_N_FLAGS
};

/*!
 * @brief Handlers are indexed in_stencil_object_index * SF_N_FLAGS + object_being_checked_index
 * All handlers take pointers in this order and
 * @return  1 - use the new index
 *          2 - use the new index and erase the old index from here to end
 *          0 - keep the use already written index
 */
static int (*stencil_handlers[SF_N_FLAGS * SF_N_FLAGS])(game_object_t*, game_object_t*);



static int StencilHandler_impossible(game_object_t* obj1, game_object_t* obj2)
{
    (void)obj1;
    (void)obj2;
    assert(0); //this is a handler that should never be called
    return -1;
}

static int StencilHandler_player_is_hit(game_object_t* projectile, game_object_t* player)
{
    assert(player == player_object);
    struct MoveResults* mr1 = &projectile->mr;
    struct MoveResults* mr2 = &player->mr;
    //find where is the collision
    //   s1 . . . . . e1
    //        e1 . . e2
    int l1 = (mr1->dir > 0) ? mr1->trail_start : mr1->body_end;
    int r1 = (mr1->dir < 0) ? mr1->trail_start : mr1->body_end;
    int l2 = (mr2->dir > 0) ? mr2->trail_start : mr2->body_end;
    int r2 = (mr2->dir < 0) ? mr2->trail_start : mr2->body_end;
    int len1 = (mr1->body_end - mr1->body_start) * mr1->dir;
    //if projectile is moving right, so the hit is from left
    int hit_end = (projectile->mr.dir > 0) ? l2 : r2;
    assert(l1 <= hit_end && hit_end <= r1); //left end of target must be in projectile path
    mr1->body_end = hit_end;;
    mr1->body_start = hit_end - mr1->dir * len1;
    mr1->end_position = mr1->body_start;
    mr1->target_reached = 1;

    //set some animations on projectile and player
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

static void MovingObject_stencil_test(int object_index)
{
    struct MoveResults* mr = &game_objects[object_index].mr;
    int led_start = mr->trail_start;
    int led_end = mr->body_start + mr->dir * (game_objects[object_index].body.length - 1);
    assert(led_start >= 0 && led_start < game_source.basic_source.n_leds - 1);
    assert(led_end >= 0 && led_end < game_source.basic_source.n_leds - 1);
    if (mr->body_offset > 0) led_end++;
    int other_index = -1;
    int result_stencil = -1;
    int result_index = -1;
    for (int led = led_start; mr->dir * (led_end - led) > 0; led += mr->dir)
    {
        if (canvas[led].object_index == -1)
        {
            canvas[led].stencil = game_objects[object_index].stencil_flag;
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
        int handler_index = canvas[led].stencil * SF_N_FLAGS + game_objects[object_index].stencil_flag;
        int res;
        if (!stencil_handlers[handler_index]) //we don't have a handler, default option is to replace the old stencil
        {
            res = 1;
        }
        else
        {
            res = stencil_handlers[handler_index](&game_objects[other_index], &game_objects[object_index]);
        }
        switch (res)
        {
        case 2:
            Stencil_erase_object(led, mr->dir);
            result_stencil = game_objects[object_index].stencil_flag;
            result_index = object_index;
            break;
        case 1:
            result_stencil = game_objects[object_index].stencil_flag;
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

/*! Iterates over all GameObjects and paint their
* stencil ids into the stencil itself, When a collision
* is detected, appropriate handler function is called
* from table Stencil_handlers
*/
void Stencil_check_movement()
{
    for (int object_index = 0; object_index < MAX_N_OBJECTS; ++object_index)
    {
        if (game_objects[object_index].body.deleted)
        {
            continue;
        }
        MovingObject_get_move_results(&game_objects[object_index].body, &game_objects[object_index].mr);
        MovingObject_stencil_test(object_index);
    }
}

void Stencil_init()
{
    for (int i = 0; i < SF_N_FLAGS * SF_N_FLAGS; ++i)
    {
        stencil_handlers[i] = NULL;
    }

}

