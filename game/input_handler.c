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

#include "controller.h"
#include "colours.h"
#include "common_source.h"
#include "moving_object.h"
#include "pulse_object.h"
#include "game_source_priv.h"
#include "game_source.h"

#define GAME_DEBUG

//handlers of the button events, the lower half is on_release
static void(*button_handlers[2 * C_MAX_XBTN])();

/*!
 * @brief Set facing to left
 * If we are already facing left, do nothing, otherwise we must change the position
*/
static void ButtonHandler_face_player_backward()
{
    MovingObject_set_facing(&player_object->body, MO_BACKWARD);
}

static void ButtonHandler_face_player_forward()
{
    MovingObject_set_facing(&player_object->body, MO_FORWARD);
}

//Actually, if facing forward, it would be able to move to position 0, but it seems like a sensible precaution to just disallow it
static void ButtonHandler_move_player_left()
{
    if (player_object->body.position < player_object->body.length)
        return;
    printf("pos %f\n", player_object->body.position);
    player_object->body.target = (uint32_t)player_object->body.position - 1;
    player_object->body.speed = config.player_ship_speed;
    printf("moving left\n");
}

static void ButtonHandler_move_player_right()
{
    if (player_object->body.position > game_source.basic_source.n_leds - (double)player_object->body.length)
        return;
    player_object->body.target = (uint32_t)player_object->body.position + 1;
    player_object->body.speed = config.player_ship_speed;
}


static void ButtonHandler_debug_pulse()
{
    PulseObject_init_player_lost_health();
}

static void ButtonHandler_debug_heal()
{
    player_object->health++;
}

void InputHandler_init()
{
    Controller_init(); //TODO: this needs/should be called only once
    for (int i = 0; i < 2 * C_MAX_XBTN; ++i) button_handlers[i] = NULL;
    button_handlers[C_MAX_XBTN + XBTN_LB] = ButtonHandler_face_player_backward;
    button_handlers[C_MAX_XBTN + XBTN_RB] = ButtonHandler_face_player_forward;
    button_handlers[C_MAX_XBTN + DPAD_L] = ButtonHandler_move_player_left;
    button_handlers[C_MAX_XBTN + DPAD_R] = ButtonHandler_move_player_right;
    button_handlers[C_MAX_XBTN + XBTN_X] = ButtonHandler_debug_pulse;
    button_handlers[C_MAX_XBTN + XBTN_A] = ButtonHandler_debug_heal;
}


int InputHandler_process_input()
{
    enum EButtons button;
    enum EState state;
    int i = Controller_get_button(&button, &state);
#ifdef GAME_DEBUG
    if (i != 0) printf("controller button: %s, state: %i\n", Controller_get_button_name(button), state);
#endif // GAME_DEBUG
    if (i == 0)
    {
        return 1;
    }
    int button_index = state * C_MAX_XBTN + button;
    if (button_handlers[button_index])
    {
        button_handlers[button_index]();
    }
    return 1;
}
