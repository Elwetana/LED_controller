#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <assert.h>

#ifdef __linux__
#include <unistd.h>
#include <linux/input.h>
#include <fcntl.h>
#else
#include <windows.h>
#include "fakeinput.h"
#include <Xinput.h>
#pragma comment(lib, "XInput.lib")
#endif

#include "controller.h"

static int input[C_MAX_CONTROLLERS];
static int n_players;
static int dpad_pressed = 200;
static int left_stick_pressed = XBTN_LST_L;
static uint64_t down_timeout = 100 * 1e6; //in ns
static char* button_names[] = {
    "DPAD LEFT", "DPAD RIGHT", "DPAD UP", "DPAD DOWN", //300 - 303
    "BUTTON A", "BUTTON B", "UNKNOWN 306", "BUTTON X", "BUTTON Y", //304-308
    "ERROR 309", "LEFT SHOULDER", "RIGHT SHOULDER", "UNKNOWN 312", //309-312
    "UNKNOWN 313", "BACK", "START", "XBOX", "LEFT THUMB", "RIGHT THUMB" //313-318
};
static uint64_t button_states[C_MAX_CONTROLLERS][C_MAX_XBTN];

#ifndef __linux__
static WORD current_state[C_MAX_CONTROLLERS];
static SHORT current_stick[5][C_MAX_CONTROLLERS];
static WORD last_state[C_MAX_CONTROLLERS];
static WORD processed[C_MAX_CONTROLLERS];
static enum EButtons xMap[] = { 
    DPAD_U,     DPAD_D,     DPAD_L,     DPAD_R, 
    XBTN_Start, XBTN_Back,  XBTN_L3,    XBTN_R3, 
    XBTN_LB,    XBTN_RB,    XBTN_ERROR, XBTN_ERROR, 
    XBTN_A,     XBTN_B,     XBTN_X,     XBTN_Y 
};
/*
XINPUT_GAMEPAD_DPAD_UP	    0x0001
XINPUT_GAMEPAD_DPAD_DOWN	0x0002
XINPUT_GAMEPAD_DPAD_LEFT	0x0004
XINPUT_GAMEPAD_DPAD_RIGHT	0x0008
XINPUT_GAMEPAD_START	    0x0010
XINPUT_GAMEPAD_BACK	        0x0020
XINPUT_GAMEPAD_LEFT_THUMB	0x0040
XINPUT_GAMEPAD_RIGHT_THUMB	0x0080
XINPUT_GAMEPAD_LEFT_SHOULDER	0x0100
XINPUT_GAMEPAD_RIGHT_SHOULDER	0x0200
XINPUT_GAMEPAD_A	        0x1000
XINPUT_GAMEPAD_B	        0x2000
XINPUT_GAMEPAD_X	        0x4000
XINPUT_GAMEPAD_Y	        0x8000
*/
#endif

char* Controller_get_button_name(enum EButtons button)
{
    return button_names[button];
}

#ifndef __linux__
int Controller_get_button_windows(enum EButtons* button, enum EState* state, DWORD dwUserIndex)
{
    XINPUT_STATE xstate;
    if (processed[dwUserIndex] == 0 && current_stick[4][dwUserIndex] == 0)
    {
        DWORD dwResult = XInputGetState(dwUserIndex, &xstate);
        if (dwResult == ERROR_SUCCESS)
        {
            current_state[dwUserIndex] = xstate.Gamepad.wButtons;
            current_stick[0][dwUserIndex] = xstate.Gamepad.sThumbLX;
            current_stick[1][dwUserIndex] = xstate.Gamepad.sThumbLY;
            current_stick[2][dwUserIndex] = xstate.Gamepad.sThumbRX;
            current_stick[3][dwUserIndex] = xstate.Gamepad.sThumbRY;
        }
        else
            return -1;
    }
    WORD diff = current_state[dwUserIndex] ^ last_state[dwUserIndex];
    if (diff > 0)
    {
        for (int i = 0; i < 16; ++i)
        {
            WORD bit = 1 << i;
            if ((diff & bit) && !(processed[dwUserIndex] & bit))
            {
                processed[dwUserIndex] |= bit;
                *button = xMap[i];
                *state = (bit & current_state[dwUserIndex]) ? BT_pressed : BT_released;
                return 1;
            }
        }
    }
    if (current_stick[0][dwUserIndex] > 16383 || current_stick[0][dwUserIndex] <- 16383)
    {
        *state = BT_pressed;
        *button = (current_stick[0][dwUserIndex] > 16383) ? XBTN_LST_R : XBTN_LST_L;
        current_stick[0][dwUserIndex] = 0;
        current_stick[4][dwUserIndex] = 1;
        return 1;
    }

    last_state[dwUserIndex] = current_state[dwUserIndex];
    processed[dwUserIndex] = 0;
    current_stick[4][dwUserIndex] = 0;
    return 0;
}

int Controller_check_present_windows(int dwUserIndex)
{
    enum EButtons button;
    enum EState state;
    return Controller_get_button_windows(&button, &state, dwUserIndex);
}
#endif

int Controller_get_n_players(void)
{
    return n_players;
}

void Controller_init(void)
{
    for (int i = 0; i < C_MAX_CONTROLLERS; i++)
    {
#ifdef __linux__
        char input_path[18];
        sprintf(input_path, "/dev/input/event%i", i);
        input[i] = open(input_path, O_RDONLY | O_NONBLOCK);
        if (input[i] == -1)
        {
            n_players = i;
            return;
        }
#else
        int isPresent = Controller_check_present_windows(i);
        if (isPresent == -1)
        {
            n_players = i;
            return;
        }
        current_state[i] = 0;
        last_state[i] = 0;
        processed[i] = 0;
#endif
        for (int button = 0; button < C_MAX_XBTN; ++button)
        {
            button_states[i][button] = 0;
        }
    }
    n_players = C_MAX_CONTROLLERS;
}

void process_d_pad(enum EButtons* button, enum EState* state, int value, enum EButtons neg_value, enum EButtons pos_value)
{
    if (value == 0)
    {
        *button = dpad_pressed;
        *state = BT_released;
    }
    else 
    {
        *button = (value == 1) ? pos_value : neg_value;
        *state = BT_pressed;
        dpad_pressed = *button;
    }
}

int Controller_get_button(uint64_t t, enum EButtons* button, enum EState* state, int controller_index)
{
    assert(controller_index < n_players);
#ifndef __linux__
    return Controller_get_button_windows(button, state, controller_index);
#endif
    struct input_event ie;
    int len = read(input[controller_index], &ie, sizeof(struct input_event));
    while (len > 0)
    {
        if (ie.type == EV_KEY)
        {
            *button = ie.code - C_BTN_OFFSET;
            *state = ie.value;
            goto update;
        }
        if (ie.type == EV_ABS)
        {
            if (ie.code == ABS_HAT0X)
            {
                process_d_pad(button, state, ie.value, DPAD_L, DPAD_R);
                goto update;
            }
            if (ie.code == ABS_HAT0Y)
            {
                process_d_pad(button, state, ie.value, DPAD_U, DPAD_D);
                goto update;
            }
            else if(ie.code == ABS_X)
            {
                if (ie.value > 16383)
                {
                    *state = BT_pressed;
                    *button = XBTN_LST_R;
                    left_stick_pressed = XBTN_LST_R;
                    goto update;
                }
                if (ie.value < -16383)
                {
                    *state = BT_pressed;
                    *button = XBTN_LST_L;
                    left_stick_pressed = XBTN_LST_L;
                    goto update;
                }
                *state = BT_released;
                *button = left_stick_pressed;
                goto update;
            }
        }
        len = read(input[controller_index], &ie, sizeof(struct input_event));
    }
    for(int i = 0; i < C_MAX_XBTN; ++i)
    {
        if (button_states[controller_index][i] != 0 && (t - button_states[controller_index][i]) > down_timeout)
        {
            button_states[controller_index][i] = t;
            *button = i;
            *state = BT_down;
            return 1;
        }
    }
    return 0;
update: 
    button_states[controller_index][*button] = (*state == BT_pressed) ? t : 0;
    return 1;
}
