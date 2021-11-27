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

//#define GAME_DEBUG

/*! Init all game objects and modes */
void Game_source_init_objects()
{
    //placeholder -- config will be read from file
    config.player_start_position = 180;
    config.player_ship_speed = 2;
    config.player_ship_size = 6;
    config.player_health_levels = 5;
    config.enemy_spawn_chance = 0.1; //number of enemies to spawn per second on average
    config.enemy_speed = 40;
    config.decoration_speed = 8;

    config.color_index_R = 0;
    config.color_index_G = 1;
    config.color_index_B = 2;
    config.color_index_C = 3;
    config.color_index_M = 4;
    config.color_index_Y = 5;
    config.color_index_W = 6;
    config.color_index_K = 7;
    config.color_index_player = 9; //9 is normal level, 8 is below, 9 is above
    config.color_index_health = 11; //11: healthy, 12: sick
    config.color_index_game_over = 13;
    config.color_index_stargate = 14; //15 is unused, 16, 17: decoration colors
    config.color_index_boss_head = 18;
    //config.player_fire_cooldown = 500;
    config.boss_health = 20;
    config.wraparound_fire_pos = 40;

    GameObjects_init();
}

//msg = mode?xxxxxx
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
    payload[63] = 0x0;
    if (!strncasecmp(target, "mode", 5))
    {
        enum GameModes gm_before = GameObjects_get_current_mode();
        GameObjects_set_level_by_message(payload);
        printf("Sent code: %s, game mode before: %i\n", payload, gm_before);
    }
    else
        printf("GameSource: Unknown target: %s, payload was: %s\n", target, payload);

}

void GameSource_init(int n_leds, int time_speed, uint64_t current_time)
{
    BasicSource_init(&game_source.basic_source, n_leds, time_speed, source_config.colors[GAME_SOURCE], current_time);
    game_source.first_update = 0;
    canvas = malloc(sizeof(pixel_t) * n_leds);
    Game_source_init_objects();
}

void GameSource_destruct()
{
    free(canvas);
}

void GameSource_construct()
{
    BasicSource_construct(&game_source.basic_source);
    game_source.basic_source.update = GameObjects_update_leds;
    game_source.basic_source.init = GameSource_init;
    game_source.basic_source.destruct = GameSource_destruct;
    game_source.basic_source.process_message = GameSource_process_message;
}

GameSource game_source = {
    .basic_source.construct = GameSource_construct,
    .first_update = 0 
};
