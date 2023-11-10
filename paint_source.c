#define _CRT_SECURE_NO_WARNINGS

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <math.h>
#include <assert.h>
#ifdef __linux__
#include "ws2811.h"
#include <unistd.h>
#include <linux/input.h>
#include <fcntl.h>
#include <errno.h>
#else
#include "fakeled.h"
#endif // __linux__

#include "colours.h"
#include "common_source.h"
#include "source_manager.h"
#include "sound_player.h"
#include "controller.h"
#include "base64.h"

#include "paint_source.h"
#include "spng.h"

typedef struct paint_SBrush
{
    int position;
    int size;
    hsl_t colour;
    ws2811_led_t rgb;
} paint_Brush_t;

static paint_Brush_t brush;

int paint_canvas[200];

static void save_canvas_to_png(void)
{
}


void Paint_BrushMove(int direction)
{
    brush.size += direction;
}

void Paint_HueChange(int direction)
{
    brush.colour.h += (float)direction / 128.0f;
}

void Paint_SaturationChange(int direction)
{

}

void Paint_LightnessChange(int direction)
{

}


int PaintSource_update_leds(int frame, ws2811_t* ledstrip)
{
    paint_source.cur_frame = frame;

    return 1;
}

//! @brief Process messages from HTTP server
//! All messages have to have format <command>?<parameter>. Available commands are:
//!     win?<next_level> (0 means current level + 1)
//!     lose?<next_level> (0 means retry current level)
//!     clue?0 (starts the final (clue) level)
//! @param msg 
void PaintSource_process_message(const char* msg)
{
    char* sep = strchr(msg, '?');
    if (sep == NULL)
    {
        printf("Match3GameSource: message does not contain target %s\n", msg);
        return;
    }
    if ((sep - msg) >= 32)
    {
        printf("Match3GameSource: target is too long or poorly formatted: %s\n", msg);
        return;
    }
    if ((strlen(sep + 1) >= 64))
    {
        printf("Match3GameSource: message too long or poorly formatted: %s\n", msg);
        return;
    }
    char target[32];
    char payload[64];
    strncpy(target, msg, sep - msg);
    strncpy(payload, sep + 1, 64);
    target[sep - msg] = 0x0;
    payload[63] = 0x0;
    char* checkPtr;
    errno = 0;
    long val = strtol(payload, &checkPtr, 10);


    char decoded[200];
    Base64decode(decoded, payload);
    /*
    if (checkPtr == payload || errno != 0 || !checkPtr || *checkPtr != '\0')
    {
        printf("Message parameter %s could not be converted to number\n", payload);
        return;
    }
    if (!strncasecmp(target, "win", 3))
    {
        if (val == 0)
        {
            val = current_level + 1;
        }
        current_level = val - 1;
        match3_game_source.level_phase = M3LP_LEVEL_WON;
        printf("Message WIN, next level will be: %i\n", current_level + 1);
    }
    else if (!strncasecmp(target, "lose", 4))
    {
        if (val == 0)
        {
            val = current_level;
        }
        current_level = val;
        match3_game_source.level_phase = M3LP_LEVEL_LOST;
        printf("Message LOST, next level will be: %i\n", current_level);
    }
    else if (!strncasecmp(target, "clue", 4))
    {
        current_level = MATCH3_N_LEVELS - 1;
        match3_game_source.level_phase = M3LP_LEVEL_WON;
        printf("Message CLUE\n");
    }
    else
        printf("Match3GameSource: Unknown command: %s, parameter was: %s\n", target, payload);
    */   
}

void PaintSource_init(int n_leds, int time_speed, uint64_t current_time)
{
    BasicSource_init(&paint_source.basic_source, n_leds, time_speed, source_config.colors[PAINT_SOURCE], current_time);
    //SoundPlayer_init(20000);
    //Match3_InputHandler_init(); // inits the controller
    //match3_game_source.start_time = current_time;
    //match3_game_source.n_players = Controller_get_n_players();
}

void PaintSource_destruct(void)
{
}

void PaintSource_construct(void)
{
    BasicSource_construct(&paint_source.basic_source);
    paint_source.basic_source.update = PaintSource_update_leds;
    paint_source.basic_source.init = PaintSource_init;
    paint_source.basic_source.destruct = PaintSource_destruct;
    paint_source.basic_source.process_message = PaintSource_process_message;
}

paint_PaintSource_t paint_source = {
    .basic_source.construct = PaintSource_construct
};


