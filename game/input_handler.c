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
static void(*button_handlers[3 * C_MAX_XBTN])(); //there are three possible states of a button

static void ButtonHandler_next_level()
{
    GameObjects_next_level();
}

static void ButtonHandler_debug_game_over()
{
    GameObjects_set_mode_player_lost(C_PLAYER_OBJ_INDEX);
}

static void ButtonHandler_start_from_level1()
{
    GameObjects_set_level_by_message("Alpha");
}

void InputHandler_init(enum GameModes game_mode)
{
    Controller_init(); //TODO: this needs/should be called only once

    for (int i = 0; i < 3 * C_MAX_XBTN; ++i) button_handlers[i] = NULL;

    switch (game_mode)
    {
    case GM_LEVEL1:
    case GM_LEVEL2:
    case GM_LEVEL3:
        button_handlers[C_MAX_XBTN + XBTN_LB] = PlayerObject_face_backward;
        button_handlers[C_MAX_XBTN + XBTN_RB] = PlayerObject_face_forward;
        button_handlers[C_MAX_XBTN + DPAD_L] = PlayerObject_move_left;
        button_handlers[C_MAX_XBTN + DPAD_R] = PlayerObject_move_right;
        button_handlers[C_MAX_XBTN + DPAD_U] = PlayerObject_hide_above;
        button_handlers[C_MAX_XBTN + DPAD_D] = PlayerObject_hide_below;
        button_handlers[C_MAX_XBTN + XBTN_LST_L] = PlayerObject_move_left;
        button_handlers[C_MAX_XBTN + XBTN_LST_R] = PlayerObject_move_right;
        button_handlers[2 * C_MAX_XBTN + XBTN_LST_L] = PlayerObject_move_left;
        button_handlers[2 * C_MAX_XBTN + XBTN_LST_R] = PlayerObject_move_right;
        //button_handlers[C_MAX_XBTN + XBTN_A] = GameObject_debug_win;
        //button_handlers[C_MAX_XBTN + XBTN_X] = ButtonHandler_debug_heal;
        button_handlers[C_MAX_XBTN + XBTN_Back] = ButtonHandler_debug_game_over;
        break;
    case GM_LEVEL_BOSS:
        button_handlers[C_MAX_XBTN + XBTN_LB] = PlayerObject_face_backward;
        button_handlers[C_MAX_XBTN + XBTN_RB] = PlayerObject_face_forward;
        button_handlers[C_MAX_XBTN + DPAD_L] = PlayerObject_move_left;
        button_handlers[C_MAX_XBTN + DPAD_R] = PlayerObject_move_right;
        button_handlers[C_MAX_XBTN + XBTN_LST_L] = PlayerObject_move_left;
        button_handlers[C_MAX_XBTN + XBTN_LST_R] = PlayerObject_move_right;
        button_handlers[2 * C_MAX_XBTN + XBTN_LST_L] = PlayerObject_move_left;
        button_handlers[2 * C_MAX_XBTN + XBTN_LST_R] = PlayerObject_move_right;
        button_handlers[C_MAX_XBTN + XBTN_A] = PlayerObject_fire_bullet_green;
        button_handlers[C_MAX_XBTN + XBTN_B] = PlayerObject_fire_bullet_red;
        button_handlers[C_MAX_XBTN + XBTN_X] = PlayerObject_fire_bullet_blue;
        button_handlers[C_MAX_XBTN + XBTN_Y] = PlayerObject_cloak; //*/GameObject_debug_win;//GameObject_debug_boss_special;
        button_handlers[C_MAX_XBTN + XBTN_Back] = ButtonHandler_debug_game_over;
        break;
    case GM_PLAYER_LOST:
        button_handlers[C_MAX_XBTN + XBTN_Start] = GameObjects_restart_lost_level;
        break;
    case GM_LEVEL1_WON:
    case GM_LEVEL2_WON:
    case GM_LEVEL3_WON:
        button_handlers[C_MAX_XBTN + XBTN_X] = ButtonHandler_next_level;
        button_handlers[C_MAX_XBTN + XBTN_Y] = ButtonHandler_next_level;
        button_handlers[C_MAX_XBTN + XBTN_A] = ButtonHandler_next_level;
        button_handlers[C_MAX_XBTN + XBTN_B] = ButtonHandler_next_level;
    case GM_LEVEL_BOSS_DEFEATED:
    case GM_LEVEL_BOSS_WON:
        break;
    }
    button_handlers[C_MAX_XBTN + XBTN_Xbox] = ButtonHandler_start_from_level1;
}


int InputHandler_process_input()
{
    enum EButtons button;
    enum EState state;
    int i = Controller_get_button(game_source.basic_source.current_time, &button, &state, 0);
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

