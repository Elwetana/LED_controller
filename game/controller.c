#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>

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

static int input;
static int dpad_pressed = 200;
static char* button_names[] = {
    "DPAD LEFT", "DPAD RIGHT", "DPAD UP", "DPAD DOWN", //300 - 303
    "BUTTON A", "BUTTON B", "UNKNOWN 306", "BUTTON X", "BUTTON Y", //304-308
    "ERROR 309", "LEFT SHOULDER", "RIGHT SHOULDER", "UNKNOWN 312", //309-312
    "UNKNOWN 313", "BACK", "START", "XBOX", "LEFT THUMB", "RIGHT THUMB" //313-318
};

#ifndef __linux__
static WORD current_state;
static WORD last_state;
static WORD processed;
static enum EButtons xMap[] = { DPAD_U, DPAD_D, DPAD_L, DPAD_R, XBTN_Start, XBTN_Back, XBTN_L3, XBTN_R3, XBTN_LB, XBTN_RB, XBTN_ERROR, XBTN_ERROR, XBTN_A, XBTN_B, XBTN_X, XBTN_Y };
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

void Controller_init()
{
#ifdef __linux__
    input = open("/dev/input/event0", O_RDONLY | O_NONBLOCK);
#else
    current_state = 0;
    last_state = 0;
    processed = 0;
#endif
}

char* Controller_get_button_name(enum EButtons button)
{
    return button_names[button];
}

#ifndef __linux__
int Controller_get_button_windows(enum EButtons* button, enum EState* state)
{
    XINPUT_STATE xstate;
    DWORD dwUserIndex = 0;
    if (processed == 0)
    {
        DWORD res = XInputGetState(dwUserIndex, &xstate);
        current_state = xstate.Gamepad.wButtons;
    }
    WORD diff = current_state ^ last_state;
    if (diff > 0)
    {
        for (int i = 0; i < 16; ++i)
        {
            WORD bit = 1 << i;
            if ((diff & bit) && !(processed & bit))
            {
                processed |= bit;
                *button = xMap[i];
                *state = (bit & current_state) ? BT_pressed : BT_released;
                return 1;
            }
        }
    }
    last_state = current_state;
    processed = 0;
    return 0;
}
#endif

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

int Controller_get_button(enum EButtons* button, enum EState* state)
{
#ifndef __linux__
    return Controller_get_button_windows(button, state);
#endif
    struct input_event ie;
    int len = read(input, &ie, sizeof(struct input_event));
    while (len > 0)
    {
        if (ie.type == EV_KEY)
        {
            *button = ie.code - C_BTN_OFFSET;
            *state = ie.value;
            return 1;
        }
        if (ie.type == EV_ABS)
        {
            if (ie.code == ABS_HAT0X)
            {
                process_d_pad(button, state, ie.value, DPAD_L, DPAD_R);
                return 1;
            }
            if (ie.code == ABS_HAT0Y)
            {
                process_d_pad(button, state, ie.value, DPAD_U, DPAD_D);
                return 1;
            }
        }
        len = read(input, &ie, sizeof(struct input_event));
    }
    return 0;
}
