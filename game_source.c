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
#include "controller.h"
#include "colours.h"
#include "moving_object.h"

#define GAME_DEBUG

enum StencilFlags
{
    SF_Player,
    SF_PlayerProjectile,
    SF_Enemy,
    SF_EnemyProjectile
};


typedef struct PulseObject
{
    int moving_object_index;
    ws2811_led_t source_color[MAX_OBJECT_LENGTH];
} pulse_object_t;

struct
{
    moving_object_t ship;
    int health;
} player_object;


static moving_object_t objects[MAX_N_OBJECTS];
static int n_objects = 0;



//handlers of the button events, the lower half is on_release
static void(*button_handlers[2 * C_MAX_XBTN])();


//TODO  -- this is all wrong, we must separate facing and movement direction
void ButtonHandler_move_player_left()
{
    int dir = SGN(player_object.ship.target, player_object.ship.position);
    //if dir == 1, we need to turn the ship around, in this case it's enough that position > 1
    //if dir == -1, we are already moving left and we need postion > 1 + length
    // e.g. dir==1, pos = 2, length = 3 =>   . . P * * will become . . . * * P . => position is 5
    if (player_object.ship.position > ((dir > 0) * player_object.ship.length + 1.))
    {
        player_object.ship.position += (dir > 0) * player_object.ship.length;
        player_object.ship.target = (uint32_t)player_object.ship.position - 1;
        player_object.ship.speed = config.player_ship_speed;
    }
}

void ButtonHandler_move_player_right()
{
    int dir = SGN(player_object.ship.target, player_object.ship.position);
    //if dir == 1, we are already moving right and we need postion < n_leds - length - 1
    //if dir == -1, we need to turn the ship around, in this case it's enough that position < n_leds - 1
    if (player_object.ship.position > ((dir > 0) * player_object.ship.length + 1.))
    {
        player_object.ship.position += (dir > 0) * player_object.ship.length;
        player_object.ship.target = (uint32_t)player_object.ship.position - 1;
        player_object.ship.speed = config.player_ship_speed;
    }
}

static void Input_init()
{
    button_handlers[C_MAX_XBTN + XBTN_LB] = ButtonHandler_move_player_left;
}


static void Input_check()
{
    enum EButtons button;
    enum EState state;
    int i = Controller_get_button(&button, &state);
#ifdef GAME_DEBUG
    if (i != 0) printf("controller button: %s, state: %i\n", Controller_get_button_name(button), state);
#endif // GAME_DEBUG
    if (i != 0 && button == XBTN_A && state == BT_pressed)
    {
        
    }

}



//====== Stencil and Collisions =======

static void MovingObject_stencil_test()
{
    //canvas[object->position].stencil = stencil_index;

}

/*! Iterates over all MovingObjects and paint their
* stencil ids into the stencil itself, When a collision
* is detected, appropriate handler function is called
* from table Stencil_handlers
*/
void MovinObject_process_stencil()
{
    
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
    Canvas_clear();
    unit_tests();
/*    if(i != 0) printf("controller button: %s, state: %i\n", Controller_get_button_name(button), state);
    if (i != 0 && button == XBTN_A && state == BT_pressed)
    {
        objects[n_objects].color[0] = game_source.basic_source.gradient.colors[0];
        objects[n_objects].position = 0;
        objects[n_objects].target = 199;
        objects[n_objects].speed = 50.f;
        objects[n_objects].zdepth = 1;
        objects[n_objects].length = 1;
        objects[n_objects].on_arrival = MovingObject_arrive_delete;
        n_objects++;
    }*/
    for (int led = 0; led < game_source.basic_source.n_leds; ++led)
    {
        ledstrip->channel[0].leds[led] = 0;
    }
    for (int p = 0; p < n_objects; ++p)
    {
        MovingObject_process(&objects[p], 1, ledstrip->channel[0].leds, 1);
    }
    return 1;
}

PlayerObject_init()
{
    MovingObject_init_stopped(&player_object.ship, config.player_start_position, config.player_ship_size, 1, config.color_index_player);
    player_object.health = config.player_health_levels;
}

/*! Init all game objects and modes */
Game_source_init_objects()
{
    config.player_start_position = 180;
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

    PlayerObject_init();
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
    Controller_init();
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
