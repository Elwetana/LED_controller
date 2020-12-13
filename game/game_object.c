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
#include "callbacks.h"
#include "game_object.h"
#include "game_source.h"


const int C_BKGRND_OBJ_INDEX = 0;
const int C_OBJECT_OBJ_INDEX = 32; //ships and asteroids
const int C_PROJCT_OBJ_INDEX = 128; //projectiles


typedef struct GameObject
{
    enum StencilFlags stencil_flag;
    int health;
    int deleted;
    int mark;
} game_object_t;

static game_object_t game_objects[MAX_N_OBJECTS];
static enum GameModes current_mode;

static void GameObject_spawn_enemy_projectile()
{
    int i = C_PROJCT_OBJ_INDEX;
    while (i < MAX_N_OBJECTS && !game_objects[i].deleted)
    {
        i++;
    }
    if (i >= MAX_N_OBJECTS)
    {
        printf("Failed to create projectile\n");
        return;
    }
    MovingObject_init_stopped(i, 5, MO_FORWARD, 1, 2);
    PulseObject_init_steady(i, config.color_index_R, 1);
    MovingObject_init_movement(i, config.enemy_speed, 190, GameObject_delete_object);
    GameObject_init(i, 1, SF_EnemyProjectile);
}

void GameObject_debug_projectile()
{
    GameObject_spawn_enemy_projectile();
}

/*!
 * @brief Determine if event with rate `r` happened or not
 * see https://en.wikipedia.org/wiki/Poisson_distribution
 * We calculate the probablity of 0 events happening, i.e. k = 0. This probability is exp(-r * t).
 * We then get uniform roll from <0,1) and if it is higher than the calculated probability, the event will happen
 * @param r average number of events that happen during 1 second, i.e time rate
 * @return  1 when event happened, 0 otherwise
 */
static int roll_dice_poisson(double r)
{
    double time_seconds = (game_source.basic_source.time_delta / (long)1e3) / (double)1e6;
    double prob = exp(-r * time_seconds) + 1;
    return (random_01() > prob);
}

static void GameObject_update_objects()
{
    if (roll_dice_poisson(config.enemy_spawn_chance))
    {
        GameObject_spawn_enemy_projectile();
    }
}

void GameObject_delete_object(int gi)
{
    game_objects[gi].deleted = 1;
}

void GameObject_init(int gi, int health, int stencil_flag)
{
    game_objects[gi].deleted = 0;
    game_objects[gi].health = health;
    game_objects[gi].stencil_flag = stencil_flag;
    game_objects[gi].mark = 0;
}

int GameObject_take_hit(int gi)
{
    return --game_objects[gi].health;
}

int GameObject_heal(int gi)
{
    return ++game_objects[gi].health;
}

int GameObject_get_health(int gi)
{
    return game_objects[gi].health;
}

void GameObject_mark(int gi, int mark)
{
    game_objects[gi].mark |= mark;
}

int GameObject_get_mark(int gi)
{
    return game_objects[gi].mark;
}

//******** GAME STATE FUNCTIONS ********

static void game_over_init()
{
    int l = game_source.basic_source.n_leds / 2;
    GameObject_init(0, 0, SF_Background);
    MovingObject_init_stopped(0, 0, MO_FORWARD, l, 0);
    PulseObject_init(0, 1, PM_REPEAT, 2, 250, 0, M_PI / l, 1, NULL);
    PulseObject_set_color_all(0, config.color_index_game_over, config.color_index_W, 0, l);
}


static void GameObjects_init_objects()
{
    switch (current_mode)
    {
    case GM_LEVEL1:
        //spawn stargate
        break;
    case GM_LEVEL1_WON:
        break;
    case GM_PLAYER_LOST:
        //spawn game over
        break;
    }
}

void GameObjects_init()
{
    for (int i = 0; i < MAX_N_OBJECTS; ++i)
        game_objects[i].deleted = 1;
    current_mode = GM_LEVEL1;

    PlayerObject_init(current_mode);
    InputHandler_init(current_mode);
    Stencil_init(current_mode);
    GameObjects_init_objects();
}

enum GameModes GameObjects_get_current_mode()
{
    return current_mode;
}

void GameObjects_set_mode_player_lost()
{
    current_mode = GM_PLAYER_LOST;
    GameObjects_init();
    printf("player lost\n");
}


void GameObjects_next_level()
{
    printf("advancing to next level\n");
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
int GameObjects_update_leds(int frame, ws2811_t* ledstrip)
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
    GameObject_update_objects();
    //calculate movement, check collisions, adjust movement, start effects
    for (int gi = 0; gi < MAX_N_OBJECTS; ++gi)
    {
        if (game_objects[gi].deleted)
        {
            continue;
        }
        MovingObject_calculate_move_results(gi);
        Stencil_stencil_test(gi, game_objects[gi].stencil_flag);
    }
    //apply movement results, render scene
    for (int gi = 0; gi < MAX_N_OBJECTS; ++gi)
    {
        if (game_objects[gi].deleted)
        {
            continue;
        }
        PulseObject_update(gi);
        MovingObject_render(gi, ledstrip->channel[0].leds, 1);
        MovingObject_update(gi);
    }
    return 1;
}
