#define _CRT_SECURE_NO_WARNINGS

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#ifdef __linux__
#include "ws2811.h"
#include <unistd.h>
#include <linux/input.h>
#include <fcntl.h>
#else
#include "fakeled.h"
#endif // __linux__

#include "common_source.h"
#include "game_source.h"
#include "controller.h"
#include "colours.h"

#define MAX_N_PROJECTILES 100

typedef enum EDirection
{
    D_FORWARD, //to higher numbers
    D_BACKWARD //to lower numbers
} dir_t;

typedef struct Projectile
{
    int position;
    float offset;
    float speed;
    dir_t direction;
    int zdepth;
    ws2811_led_t color;
} projectile_t;

typedef struct CanvasPixel
{
    //ws2811_led_t color;
    int zbuffer;
    int stencil;
} pixel_t;

static pixel_t* canvas;
static projectile_t projectiles[MAX_N_PROJECTILES];
static int n_projectiles = 0;

/*
* Returns 0 when the projectile leaves the canvas, 1 otherwise
*/
static int Projecile_process(projectile_t* projectile, int stencil_index, ws2811_led_t* leds)
{
    /* speed = 3.5, time = 1, position = 7, offset = 0.3
    *     -> distance = 3.5, result position = 10.8, leds: (7) 0.7 - (8) 1 - (9) 1 - (10) 1 - (11) 0.8
    * speed = 0.5, time = 1, position = 7, offset = 0.3
    *     -> distance = 0.5, result position = 7.8, leds: (7) 0.7 - (8) 0.8
    * speed = 0.5, time = 1, position = 7, offset = 8
    *     -> distance = 0.5, result position = 8.3, leds: (7) 0.2 - (8) 1 - (9) 0.3
    * speed = 1, time = 1, position = 7, offset = 0
    *     -> distance = 1, result position = 8, leds: (7) 1 - (8) 1 - (9) 0
    */
    double time_seconds = (game_source.basic_source.time_delta / (long)1e3) / (double)1e6;
    double distance = projectile->speed * time_seconds;
    
    //always color the starting led by the starting presence (i.e 1 - offset)
    ws2811_led_t col = multiply_rgb_color(projectile->color, 1 - projectile->offset);
    //TODO: z-test
    leds[projectile->position] = col;
    canvas[projectile->position].zbuffer = projectile->zdepth;
    canvas[projectile->position].stencil = stencil_index;

    //color the leds between start and end
    while ((projectile->offset + distance) >= 1) //we have visited more than one LED
    {
        projectile->position += (projectile->direction == D_FORWARD) ? +1 : -1;
        distance--;
        if (projectile->position >= 0 && projectile->position < game_source.basic_source.n_leds)
        {
            leds[projectile->position] = projectile->color;
            canvas[projectile->position].zbuffer = projectile->zdepth;
            canvas[projectile->position].stencil = stencil_index;
        }
        else
        {
            return 0;
        }
    }

    //color the end
    projectile->offset += distance;
    int next_led = projectile->position + (projectile->direction == D_FORWARD) ? +1 : -1;
    if (next_led >= 0 && next_led < game_source.basic_source.n_leds)
    {
        col = multiply_rgb_color(projectile->color, projectile->offset);
        leds[projectile->position] = col;
        canvas[projectile->position].zbuffer = projectile->zdepth;
        canvas[projectile->position].stencil = stencil_index;
    }
    return 1;
}


//returns 1 if leds were updated, 0 if update is not necessary
int GameSource_update_leds(int frame, ws2811_t* ledstrip)
{
    (void)frame;
    enum EButtons button;
    enum EState state;
    int i = Controller_get_button(&button, &state);
    if(i != 0) printf("controller button: %s, state: %i\n", Controller_get_button_name(button), state);
    if (i != 0 && button == XBTN_A && state = BT_pressed)
    {
        projectiles[n_projectiles].color = game_source.basic_source.gradient.colors[0];
        projectiles[n_projectiles].position = 0;
        projectiles[n_projectiles].offset = 0;
        projectiles[n_projectiles].direction = D_FORWARD;
        projectiles[n_projectiles].speed = 50.f;
        projectiles[n_projectiles].zdepth = 1;
        n_projectiles++;
    }
    for (int led = 0; led < game_source.basic_source.n_leds; ++led)
    {
        ledstrip->channel[0].leds[led] = 0;
    }
    for (int p = 0; p < n_projectiles; ++p)
    {
        Projecile_process(&projectiles[p], 1, ledstrip->channel[0].leds);
    }
    return 1;
}

//msg = color?xxxxxx
void GameSource_process_message(const char* msg)
{
    char* sep = strchr(msg, '?');
    if (sep == NULL)
    {
        printf("GameSource: message does not contain target %s\n", msg);
        return;
    }
    if ((sep - msg) >= 32)
    {
        printf("GameSource: target is too long or poorly formatted: %s\n", msg);
        return;
    }
    if ((strlen(sep + 1) >= 64))
    {
        printf("GameSource: message too long or poorly formatted: %s\n", msg);
        return;
    }
    char target[32];
    char payload[64];
    strncpy(target, msg, sep - msg);
    strncpy(payload, sep + 1, 64);
    target[sep - msg] = 0x0;
    if (!strncasecmp(target, "color", 5))
    {
        int col;
        col = (int)strtol(payload, NULL, 16);
        game_source.basic_source.gradient.colors[0] = col;
        game_source.first_update = 0;
        printf("Switched colour in GameSource to: %s = %x\n", payload, col);
    }
    else
        printf("GameSource: Unknown target: %s, payload was: %s\n", target, payload);

}

void GameSource_init(int n_leds, int time_speed, uint64_t current_time)
{
    BasicSource_init(&game_source.basic_source, n_leds, time_speed, source_config.colors[GAME_SOURCE], current_time);
    game_source.first_update = 0;
    canvas = malloc(sizeof(pixel_t) * n_leds);
    Controller_init();
}

void GameSource_destruct()
{
    free(canvas);
}

void GameSource_construct()
{
    BasicSource_construct(&game_source.basic_source);
    game_source.basic_source.update = GameSource_update_leds;
    game_source.basic_source.init = GameSource_init;
    game_source.basic_source.destruct = GameSource_destruct;
    game_source.basic_source.process_message = GameSource_process_message;
}

GameSource game_source = {
    .basic_source.construct = GameSource_construct,
    .first_update = 0 
};
