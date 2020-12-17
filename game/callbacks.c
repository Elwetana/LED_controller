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


//********* Arrival methods ***********
void OnArrival_delete(int i)
{
    GameObject_delete_object(i);
}

void OnArrival_stop_and_explode(int i)
{
    //moving_objects[i].speed = 0.;
    MovingObject_stop(i);
    int length = MovingObject_get_length(i);
    PulseObject_init(i, 1, PM_ONCE, 5, 500, 0, 0, 10, GameObject_delete_object);
    PulseObject_set_color_all(i, config.color_index_R, config.color_index_W, config.color_index_K, length);
}

void OnArrival_blink_and_continue(int i)
{
    MovingObject_pause(i);
    int length = MovingObject_get_length(i);
    PulseObject_init(i, 1, PM_ONCE, 5, 65, 0, 0, 1, OnEnd_resume);
    PulseObject_set_color_all(i, config.color_index_W, config.color_index_K, config.color_index_K, length);
}

void OnArrival_blink_and_die(int i)
{
    MovingObject_stop(i);
    int length = MovingObject_get_length(i);
    PulseObject_init(i, 1, PM_ONCE, 5, 65, 0, 0, 1, GameObject_delete_object);
    PulseObject_set_color_all(i, config.color_index_W, config.color_index_K, config.color_index_K, length);
}

void OnEnd_resume(int i)
{
    MovingObject_resume(i, GameObject_delete_object);
    GameObject_clear_mark(i, 1);
}
