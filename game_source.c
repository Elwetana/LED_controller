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
#include "pulse_object.h"
#include "player_object.h"
#include "input_handler.h"
#include "stencil_handler.h"
#include "game_source_priv.h"
#include "game_source.h"

#define GAME_DEBUG

const int C_BKGRND_OBJ_INDEX = 0;
const int C_OBJECT_OBJ_INDEX = 32; //ships and asteroids
const int C_PROJCT_OBJ_INDEX = 128; //projectiles

void GameObject_spawn_enemy_projectile()
{
    int i = C_PROJCT_OBJ_INDEX;
    while (!game_objects[i].body.deleted && i < MAX_N_OBJECTS)
    {
        i++;
    }
    if (i >= MAX_N_OBJECTS)
    {
        printf("Failed to create projectile\n");
        return;
    }
    MovingObject_init_stopped(&game_objects[i].body, 5, MO_FORWARD, 1, 2);
    PulseObject_init_steady(&game_objects[i].pulse, config.color_index_R, 1);
    game_objects[i].body.speed = 40;
    game_objects[i].body.target = 190;
    game_objects[i].body.on_arrival = MovingObject_arrive_delete;
    game_objects[i].stencil_flag = SF_EnemyProjectile;
}


/*!
 * @brief Determine if event with rate `r` happened or not
 * see https://en.wikipedia.org/wiki/Poisson_distribution
 * We calculate the probablity of 0 events happening, i.e. k = 0. This probability is exp(-r * t).
 * We then get uniform roll from <0,1) and if it is higher than the calculated probability, the event will happen
 * @param r average number of events that happen during 1 second, i.e time rate
 * @return  1 when event happened, 0 otherwise
 */
int roll_dice_poisson(double r)
{
    double time_seconds = (game_source.basic_source.time_delta / (long)1e3) / (double)1e6;
    double prob = exp(-r * time_seconds);
    return (random_01() > prob);
}

void GameSource_update_objects()
{
    if (roll_dice_poisson(config.enemy_spawn_chance))
    {
        GameObject_spawn_enemy_projectile();
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
#ifdef GAME_DEBUG
    //printf("Frame: %i\n", frame);
    (void)frame;
#else
    (void)frame;
#endif // GAME_DEBUG

    //unit_tests();
    Canvas_clear(ledstrip->channel[0].leds);
    InputHandler_process_input();
    GameSource_update_objects();
    Stencil_check_movement();
    for (int p = 0; p < MAX_N_OBJECTS; ++p)
    {
        if (!game_objects[p].body.deleted)
        {
            PulseObject_update(&game_objects[p]);
            MovingObject_render(&game_objects[p].body, &game_objects[p].mr, ledstrip->channel[0].leds, 1);
            MovingObject_update(&game_objects[p].body, &game_objects[p].mr);
        }
    }
    return 1;
}

void GameSource_set_mode_player_lost()
{
    printf("player lost\n");
}

void GameObjects_init()
{
    for (int i = 0; i < MAX_N_OBJECTS; ++i)
        game_objects[i].body.deleted = 1;
    PlayerObject_init();
}

/*! Init all game objects and modes */
void Game_source_init_objects()
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
    config.color_index_K = 7;
    config.color_index_player = 8;
    config.player_health_levels = 6; //i.e 8 - 13 is index of player health levels
    config.enemy_spawn_chance = 0.2; //number of enemies to spawn per second on average

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
    payload[63] = 0x0;
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
