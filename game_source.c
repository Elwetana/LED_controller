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
#include "input_handler.h"
#include "game_source_priv.h"
#include "game_source.h"

#define GAME_DEBUG

#define MAX_N_OBJECTS     256
const int C_BKGRND_OBJ_INDEX =   0;
const int C_OBJECT_OBJ_INDEX =  32; //ships and asteroids
const int C_PROJCT_OBJ_INDEX = 128; //projectiles
const int C_PLAYER_OBJ_INDEX = MAX_N_OBJECTS - 1;

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
int (*stencil_handlers[SF_N_FLAGS * SF_N_FLAGS])(game_object_t*, game_object_t*);

game_object_t game_objects[MAX_N_OBJECTS];



//====== Stencil and Collisions =======

int StencilHandler_impossible(game_object_t* obj1, game_object_t* obj2)
{
    (void)obj1;
    (void)obj2;
    assert(0); //this is a handler that should never be called
    return -1;
}

int StencilHandler_player_is_hit(game_object_t* projectile, game_object_t* player)
{
    assert(player == &player_object);
    //find where is the collision
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

/*! The sequence of actions during one loop:
*   - process inputs - this may include timers?
*   - process collisions using stencil buffer -- this may also trigger events
*   - process colors for blinking objects
*   - move and render all objects
* 
* \param frame      current frame - not used
* \param ledstrip   pointer to rendering device
* \returns          1 when render is required, i.e. always
*/
int GameSource_update_leds(int frame, ws2811_t* ledstrip)
{
    (void)frame;
    //unit_tests();
    Canvas_clear(ledstrip->channel[0].leds);
    InputHandler_process_input();
    Stencil_check_movement();
    /*
    for (int p = 0; p < n_objects; ++p)
    {
        //MovingObject_update(&objects[p], 1, ledstrip->channel[0].leds, 1);
    }*/
    return 1;
}

PlayerObject_init()
{
    player_object = &game_objects[C_PLAYER_OBJ_INDEX];
    MovingObject_init_stopped(&player_object->body, config.player_start_position, MO_BACKWARD, config.player_ship_size, 1, config.color_index_player);
    player_object->health = config.player_health_levels;
}

GameObjects_init()
{
    for (int i = 0; i < MAX_N_OBJECTS; ++i)
        game_objects[i].body.deleted = 1;
    PlayerObject_init();
}

/*! Init all game objects and modes */
Game_source_init_objects()
{
    //placeholder -- config will be read from file
    config.player_start_position = 180;
    config.player_ship_speed = 1;
    config.player_ship_size = 5;
    config.color_index_R = 0;
    config.color_index_G = 1;
    config.color_index_B = 2;
    config.color_index_C = 3;
    config.color_index_M = 4;
    config.color_index_Y = 5;
    config.color_index_W = 6;
    config.color_index_player = 7;
    config.player_health_levels = 6; //i.e 7 - 12 is index of player health levels

    InputHandler_init();
    GameObjects_init();
    Stencil_init();
}

//msg = color?xxxxxx
void GameSource_process_message(const char* msg)
{
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
    if (!strncasecmp(target, "color", 5))
    {
        int col;
        col = (int)strtol(payload, NULL, 16);
        game_source.basic_source.gradient.colors[0] = col;
        game_source.first_update = 0;
        printf("Switched colour in GameSource to: %s = %x\n", payload, col);
    }
    else
        printf("GameSource: Unknown target: %s, payload was: %s\n", target, payload);

}

void GameSource_init(int n_leds, int time_speed, uint64_t current_time)
{
    BasicSource_init(&game_source.basic_source, n_leds, time_speed, source_config.colors[GAME_SOURCE], current_time);
    game_source.first_update = 0;
    canvas = malloc(sizeof(pixel_t) * n_leds);
    Game_source_init_objects();
}

void GameSource_destruct()
{
    free(canvas);
}

void GameSource_construct()
{
    BasicSource_construct(&game_source.basic_source);
    game_source.basic_source.update = GameSource_update_leds;
    game_source.basic_source.init = GameSource_init;
    game_source.basic_source.destruct = GameSource_destruct;
    game_source.basic_source.process_message = GameSource_process_message;
}

GameSource game_source = {
    .basic_source.construct = GameSource_construct,
    .first_update = 0 
};
