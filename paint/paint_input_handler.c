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
#include "paint_source.h"
#include "paint_input_handler.h"
#include "controller.h"

typedef struct {
    void(*handler)(int);
    int dir;
} button_handler_t;
button_handler_t button_handlers[3 * C_MAX_XBTN];


void Paint_InputHandler_init(void)
{
    button_handlers[C_MAX_XBTN + XBTN_LST_L] = (button_handler_t){ .handler = Paint_BrushMove, .dir = -1 };
    button_handlers[C_MAX_XBTN + XBTN_LST_R] = (button_handler_t){ .handler = Paint_BrushMove, .dir = +1 };
    button_handlers[C_MAX_XBTN + XBTN_RST_L] = (button_handler_t){ .handler = Paint_SaturationChange, .dir = -1 };
    button_handlers[C_MAX_XBTN + XBTN_RST_R] = (button_handler_t){ .handler = Paint_SaturationChange, .dir = +1 };
    button_handlers[C_MAX_XBTN + XBTN_RST_U] = (button_handler_t){ .handler = Paint_LightnessChange, .dir = -1 };
    button_handlers[C_MAX_XBTN + XBTN_RST_D] = (button_handler_t){ .handler = Paint_LightnessChange, .dir = +1 };

    Controller_init();
}

void Paint_InputHandler_drain_input(void)
{
    enum EButtons button;
    enum EState state;
    int i = Controller_get_button(paint_source.basic_source.current_time, &button, &state, 0);
    while (i > 0)
    {
        i = Controller_get_button(paint_source.basic_source.current_time, &button, &state, 0);
    }
    return;
}

int Paint_InputHandler_process_input(void)
{
    enum EButtons button;
    enum EState state;

    int i = Controller_get_button(paint_source.basic_source.current_time, &button, &state, 0);
    while (i > 0)
    {
#ifdef GAME_DEBUG
        printf("Player: %i, controller button: %s, state: %i\n", player, Controller_get_button_name(button), state);
#endif // GAME_DEBUG
        if (state == BT_down && (button == XBTN_LST_L || button == XBTN_LST_R)) 
            state = BT_pressed;
        int button_index = state * C_MAX_XBTN + button;
        if (button_handlers[button_index].handler)
        {
            button_handlers[button_index].handler(button_handlers[button_index].dir);
        }
        i = Controller_get_button(paint_source.basic_source.current_time, &button, &state, 0);
    }
    return 1;
}
