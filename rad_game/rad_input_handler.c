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
#include "rad_game_source.h"
#include "rad_input_handler.h"
#include "controller.h"

//#define GAME_DEBUG
static void(*button_handlers[3 * C_MAX_XBTN])(int);

void RadInputHandler_init()
{
    button_handlers[C_MAX_XBTN + DPAD_L] = Player_move_left;
    button_handlers[C_MAX_XBTN + DPAD_R] = Player_move_right;
    button_handlers[C_MAX_XBTN + XBTN_LST_L] = Player_move_left;
    button_handlers[C_MAX_XBTN + XBTN_LST_R] = Player_move_right;
    button_handlers[C_MAX_XBTN + XBTN_B] = Player_hit_red;
    button_handlers[C_MAX_XBTN + XBTN_A] = Player_hit_green;
    button_handlers[C_MAX_XBTN + XBTN_X] = Player_hit_blue;
    button_handlers[C_MAX_XBTN + XBTN_Y] = Player_hit_yellow;
    button_handlers[C_MAX_XBTN + XBTN_L3] = Player_freq_dec;
    button_handlers[C_MAX_XBTN + XBTN_R3] = Player_freq_inc;
    button_handlers[C_MAX_XBTN + XBTN_LB] = Player_time_offset_dec;
    button_handlers[C_MAX_XBTN + XBTN_RB] = Player_time_offset_inc;
    button_handlers[C_MAX_XBTN + XBTN_Start] = Player_start_pressed;

    Controller_init();
}

int RadInputHandler_process_input()
{
    enum EButtons button;
    enum EState state;
    for (int player = 0; player < rad_game_source.n_players; player++)
    {
        int i = Controller_get_button(rad_game_source.basic_source.current_time, &button, &state, player);
        while (i > 0)
        {
#ifdef GAME_DEBUG
            printf("Player: %i, controller button: %s, state: %i\n", player, Controller_get_button_name(button), state);
#endif // GAME_DEBUG
            if (state == BT_down && (button == XBTN_LST_L || button == XBTN_LST_R)) state = BT_pressed;
            int button_index = state * C_MAX_XBTN + button;
            if (button_handlers[button_index])
            {
                button_handlers[button_index](player);
            }
            i = Controller_get_button(rad_game_source.basic_source.current_time, &button, &state, player);
        }
    }
    return 1;
}
