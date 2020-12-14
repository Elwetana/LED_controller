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
#include "game_source.h"
#include "game_object.h"
#include "moving_object.h"
#include "pulse_object.h"
#include "player_object.h"

//#define GAME_DEBUG

//handlers of the button events, the lower half is on_release
static void(*button_handlers[2 * C_MAX_XBTN])();

/*!
 * @brief Set facing to left
 * If we are already facing left, do nothing, otherwise we must change the position
*/
static void ButtonHandler_face_player_backward()
{
    MovingObject_set_facing(C_PLAYER_OBJ_INDEX, MO_BACKWARD);
}

static void ButtonHandler_face_player_forward()
{
    MovingObject_set_facing(C_PLAYER_OBJ_INDEX, MO_FORWARD);
}

static void ButtonHandler_restart_game()
{
    GameObjects_init();
}

static void ButtonHandler_next_level()
{
    GameObjects_next_level();
}

static void ButtonHandler_debug_pulse()
{
    PlayerObject_take_hit(C_PLAYER_OBJ_INDEX);
}

static void ButtonHandler_debug_heal()
{
    GameObject_heal(C_PLAYER_OBJ_INDEX);
}

static void ButtonHandler_debug_projectile()
{
    GameObject_debug_projectile();
}

static void ButtonHandler_debug_game_over()
{
    GameObjects_set_mode_player_lost();
}

void InputHandler_init(enum GameModes game_mode)
{
    Controller_init(); //TODO: this needs/should be called only once

    for (int i = 0; i < 2 * C_MAX_XBTN; ++i) button_handlers[i] = NULL;

    switch (game_mode)
    {
    case GM_LEVEL1:
        button_handlers[C_MAX_XBTN + XBTN_LB] = ButtonHandler_face_player_backward;
        button_handlers[C_MAX_XBTN + XBTN_RB] = ButtonHandler_face_player_forward;
        button_handlers[C_MAX_XBTN + DPAD_L] = PlayerObject_move_left;
        button_handlers[C_MAX_XBTN + DPAD_R] = PlayerObject_move_right;
        button_handlers[C_MAX_XBTN + DPAD_U] = PlayerObject_hide_above;
        button_handlers[C_MAX_XBTN + DPAD_D] = PlayerObject_hide_below;
        button_handlers[C_MAX_XBTN + XBTN_A] = GameObject_debug_win;
        //button_handlers[C_MAX_XBTN + XBTN_X] = ButtonHandler_debug_heal;
        break;
    case GM_PLAYER_LOST:
        button_handlers[C_MAX_XBTN + XBTN_Start] = ButtonHandler_restart_game;
        break;
    case GM_LEVEL1_WON:
        button_handlers[C_MAX_XBTN + XBTN_X] = ButtonHandler_next_level;
        button_handlers[C_MAX_XBTN + XBTN_Y] = ButtonHandler_next_level;
        button_handlers[C_MAX_XBTN + XBTN_A] = ButtonHandler_next_level;
        button_handlers[C_MAX_XBTN + XBTN_B] = ButtonHandler_next_level;
    }
    button_handlers[C_MAX_XBTN + XBTN_Back] = ButtonHandler_debug_game_over;
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

