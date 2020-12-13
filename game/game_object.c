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
#include "game_source.h"
#include "game_object.h"
#include "moving_object.h"
#include "pulse_object.h"
#include "player_object.h"
#include "input_handler.h"
#include "stencil_handler.h"
#include "callbacks.h"


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
static enum GameModes current_mode = GM_LEVEL1;


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
    double prob = exp(-r * time_seconds);
    return (random_01() > prob);
}

static void update_objects_level1()
{
    if (roll_dice_poisson(config.enemy_spawn_chance))
    {
        GameObject_spawn_enemy_projectile();
    }
    double stargate_shrink_chance = 0.1; //one shrink on average every ten seconds
    if (roll_dice_poisson(stargate_shrink_chance))
    {
        double cur_position = MovingObject_get_position(0);
        double cur_length = MovingObject_get_length(0);
        MovingObject_init_stopped(0, cur_position + 1, MO_FORWARD, cur_length - 2, 9);
        printf("shrinking stargate\n");
    }
}

static void GameObject_update_objects()
{
    switch (current_mode)
    {
    case GM_LEVEL1:
        update_objects_level1();
        break;
    case GM_LEVEL1_WON:
        break;
    case GM_PLAYER_LOST:
        break;
    default:
        break;
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

void OnArrival_stargate_decoration(int i)
{
    int sg_start = MovingObject_get_position(0);
    int sg_width = MovingObject_get_length(0);
    int dec_length = MovingObject_get_length(i);
    if (i == 1 || i == 2)
    {
        MovingObject_init_stopped(i, sg_start, MO_FORWARD, dec_length, 8);
        MovingObject_init_movement(i, config.decoration_speed, sg_start + sg_width / 2 - dec_length, OnArrival_stargate_decoration);
    }
    else
    {
        MovingObject_init_stopped(i, sg_start + sg_width, MO_BACKWARD, dec_length, 8);
        MovingObject_init_movement(i, config.decoration_speed, sg_start + sg_width / 2, OnArrival_stargate_decoration);
    }
}

static void stargate_init()
/* Stargate is one long dark blue object that is used for collision detection with player 
* on it we have number of lighter objects running toward the center. Over time, stargate 
* grows narrower */
{
    int stargate_width = 40;
    int stargate_start = 5;
    GameObject_init(0, 1, SF_Enemy);
    MovingObject_init_stopped(0, stargate_start, MO_FORWARD, stargate_width, 9);
    PulseObject_init_steady(0, config.color_index_stargate, stargate_width);

    //decorations
    int dec_length = 2;
    //1 -> 0, 2 -> 1, 3 -> 3, 4 -> 4
    for (int dec = 1; dec < 5; ++dec)
    {
        int dir = (dec < 3) ? +1 : -1;
        GameObject_init(dec, 1, SF_Background);
        MovingObject_init_stopped(dec, stargate_start + (dec - dir - (dir < 0)) * stargate_width / 4,
            dir, dec_length, 8);
        MovingObject_init_movement(dec, config.decoration_speed, stargate_start + stargate_width / 2 - (dir > 0) * dec_length, OnArrival_stargate_decoration);
        PulseObject_init_steady(dec, 0, dec_length);
        for (int led = 0; led < dec_length; ++led)
        {
            PulseObject_set_color(dec, config.color_index_stargate + led + 2, config.color_index_stargate + led + 2, config.color_index_stargate + led, led);
            //PulseObject_set_color(dec, config.color_index_G, config.color_index_G, config.color_index_G, led);
        }
    }
}


static void game_over_init()
{
    int l = game_source.basic_source.n_leds / 2;
    printf("--%i-\n", l);
    GameObject_init(0, 0, SF_Background);
    MovingObject_init_stopped(0, 0, MO_FORWARD, l, 0);
    PulseObject_init(0, 1, PM_REPEAT, 2, 5000, 0, M_PI / l, 1, NULL);
    PulseObject_set_color_all(0, config.color_index_game_over, config.color_index_W, 0, l);
}


static void GameObjects_init_objects()
{
    switch (current_mode)
    {
    case GM_LEVEL1:
        //spawn stargate
        stargate_init();
        break;
    case GM_LEVEL1_WON:
        break;
    case GM_PLAYER_LOST:
        //spawn game over
        game_over_init();
        break;
    }
}

void GameObjects_init()
{
    for (int i = 0; i < MAX_N_OBJECTS; ++i)
        game_objects[i].deleted = 1;

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
