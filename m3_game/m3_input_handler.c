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
#include "m3_game_source.h"
#include "m3_players.h"
#include "m3_input_handler.h"
#include "controller.h"

static void(*button_handlers[3 * C_MAX_XBTN])(int, enum EM3_BUTTONS);
enum EM3_BUTTONS button_names[C_MAX_XBTN] = { M3B_N_BUTTONS };

static void Player_move_left(int player)
{
    Match3_Player_move(player, -1);
}

static void Player_move_right(int player)
{
    Match3_Player_move(player, +1);
}

void Match3_InputHandler_init()
{
    button_names[XBTN_A] = M3B_A;
    button_names[XBTN_B] = M3B_B;
    button_names[XBTN_X] = M3B_X;
    button_names[XBTN_Y] = M3B_Y;
    button_names[DPAD_U] = M3B_DUP;
    button_names[DPAD_R] = M3B_DRIGHT;
    button_names[DPAD_D] = M3B_DDOWN;
    button_names[DPAD_L] = M3B_DLEFT;


    button_handlers[C_MAX_XBTN + XBTN_LST_L] = Player_move_left;
    button_handlers[C_MAX_XBTN + XBTN_LST_R] = Player_move_right;
    button_handlers[C_MAX_XBTN + DPAD_U] = Match3_Player_press_button;
    button_handlers[C_MAX_XBTN + DPAD_R] = Match3_Player_press_button;
    button_handlers[C_MAX_XBTN + DPAD_L] = Match3_Player_press_button;
    button_handlers[C_MAX_XBTN + XBTN_A] = Match3_Player_press_button;
    button_handlers[C_MAX_XBTN + XBTN_B] = Match3_Player_press_button;
    button_handlers[C_MAX_XBTN + XBTN_X] = Match3_Player_press_button;
    button_handlers[C_MAX_XBTN + XBTN_Y] = Match3_Player_press_button;
    /*
    button_handlers[C_MAX_XBTN + XBTN_L3] = Player_freq_dec;
    button_handlers[C_MAX_XBTN + XBTN_R3] = Player_freq_inc;
    button_handlers[C_MAX_XBTN + XBTN_LB] = Player_time_offset_dec;
    button_handlers[C_MAX_XBTN + XBTN_RB] = Player_time_offset_inc;
    button_handlers[C_MAX_XBTN + XBTN_Start] = Player_start_pressed;
    */

    Controller_init();
}

int Match3_InputHandler_process_input()
{
    enum EButtons button;
    enum EState state;
    for (int player = 0; player < match3_game_source.n_players; player++)
    {
        int i = Controller_get_button(match3_game_source.basic_source.current_time, &button, &state, player);
        while (i > 0)
        {
#ifdef GAME_DEBUG
            printf("Player: %i, controller button: %s, state: %i\n", player, Controller_get_button_name(button), state);
#endif // GAME_DEBUG
            if (state == BT_down && (button == XBTN_LST_L || button == XBTN_LST_R)) state = BT_pressed;
            int button_index = state * C_MAX_XBTN + button;
            if (button_handlers[button_index])
            {
                assert(button_names[button] != M3B_N_BUTTONS);
                button_handlers[button_index](player, button_names[button]);
            }
            i = Controller_get_button(match3_game_source.basic_source.current_time, &button, &state, player);
        }
    }
    return 1;
}
