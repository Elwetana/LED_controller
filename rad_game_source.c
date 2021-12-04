#define _CRT_SECURE_NO_WARNINGS
/*
 * TODO
 * [ ] score for DDR
 * [ ] state machine
 * [ ] finish Oscillator gameplay
 * [x] sound_player -- effect without song
 * [ ] start_time should be reset in *_clear() functions -- or elsewhere, when we are starting a new song
 * [ ] ready states -- show players where they are
 * 
 */


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

#include "colours.h"
#include "common_source.h"
#include "rad_game_source.h"
#include "rad_input_handler.h"
#include "sound_player.h"
#include "controller.h"
#include "oscillators.h"
#include "ddr_game.h"

//#define GAME_DEBUG


/* ***************** Songs *******************/

RadGameSongs rad_game_songs =
{
    .time_offset = 0
};

void start_current_song()
{
    double bpm = rad_game_songs.songs[rad_game_songs.current_song].bpms[0];
    rad_game_songs.freq = bpm / 60.0;
    SoundPlayer_start(rad_game_songs.songs[rad_game_songs.current_song].filename);
}

static struct
{
    enum ERadGameModes cur_mode;
    int score;	        	//!< only used in show_score mode
    unsigned char player;	//!< only used in ready modes -- lower four bits are bit mask of players that pressed start, upper four bits show if message display is in progress -- it's the player's index (if nothing is in progress, all upper four bits are set)
    uint64_t effect_start;
} rad_game_mode = 
{
    .player = 0xF0,
    .effect_start = 0
};

struct RadGameLevel
{
    int song_index;
    enum ERadGameModes game_mode;
    int target_score;
};

static struct {
    struct RadGameLevel* levels;
    int n_levels;
} rad_game_levels;


/* ***************** Moving Object *******************/
 
void RadMovingObject_render(RadMovingObject* mo, int color, ws2811_t* ledstrip)
{
    if (mo->moving_dir == 0)
    {
        ledstrip->channel[0].leds[(int)mo->position] = color;
    }
    else
    {
        double offset = (mo->position - trunc(mo->position)); //always positive
        int left_led = trunc(mo->position);
        ledstrip->channel[0].leds[left_led] = multiply_rgb_color(color, 1 - offset);
        ledstrip->channel[0].leds[left_led + 1] = multiply_rgb_color(color, offset);
    }
}


/* ***************** Player input *******************/

enum ERAD_COLOURS
{
    DC_RED,
    DC_GREEN,
    DC_BLUE,
    DC_YELLOW
};

static void (*Player_hit_color)(int, enum ERAD_COLOURS);
static void (*Player_move)(int, signed char);
static void (*Player_start)(int) = 0x0;
void Player_hit_red(int player_index)
{
    Player_hit_color(player_index, DC_RED);
}

void Player_hit_green(int player_index)
{
    Player_hit_color(player_index, DC_GREEN);
}

void Player_hit_blue(int player_index)
{
    Player_hit_color(player_index, DC_BLUE);
}

void Player_hit_yellow(int player_index)
{
    Player_hit_color(player_index, DC_YELLOW);
}

void Player_move_left(int player_index)
{
    Player_move(player_index, -1);
}

void Player_move_right(int player_index)
{
    Player_move(player_index, +1);
}

void Player_start_pressed(int player_index)
{
    if(Player_start) Player_start(player_index);
}

void Player_freq_inc(int player_index)
{
    (void)player_index;
    rad_game_songs.freq += 0.01;
    printf("Frequence increased to %f\n", rad_game_songs.freq);
}

void Player_freq_dec(int player_index)
{
    (void)player_index;
    rad_game_songs.freq -= 0.01;
    printf("Frequence lowered to %f\n", rad_game_songs.freq);
}

void Player_time_offset_inc(int player_index)
{
    (void)player_index;
    rad_game_songs.time_offset += 50;
    rad_game_source.start_time += 50 * 1000000;
    printf("Time offset increased to %li ms\n", rad_game_songs.time_offset);
}

void Player_time_offset_dec(int player_index)
{
    (void)player_index;
    rad_game_songs.time_offset -= 50;
    rad_game_source.start_time -= 50 * 1000000;
    printf("Time offset decreased to %li ms\n", rad_game_songs.time_offset);
}


/****************** Common ready states stuff *******************/

void Ready_player_start(int player_index)
{
    int lock = rad_game_mode.player & 0xF0;
    if (lock == 0xF0)
    {
        rad_game_mode.player |= 1 << player_index;
        rad_game_mode.player &= 0x0F;
        rad_game_mode.player |= player_index << 4;
    }
}

void Ready_player_hit(int player_index, enum ERAD_COLOURS colour)
{
    (void)player_index;
    (void)colour;
}

void Ready_player_move(int player_index)
{
    (void)player_index;
}

int Ready_get_current_player()
{
    return (rad_game_mode.player & 0xF0) >> 4;
}
void Ready_clear_current_player()
{
    rad_game_mode.player |= 0xF0;
    rad_game_mode.effect_start = 0;
}
int Ready_get_ready_players()
{
    return rad_game_mode.player & 0x0F;
}
uint64_t Ready_get_effect_start()
{
    return rad_game_mode.effect_start;
}
void Ready_set_effect_start(uint64_t t)
{
    rad_game_mode.effect_start = t;
}


/****************** Actual RadGameSource update *******************/

int (*current_mode_update_leds)(ws2811_t* ledstrip);
int RadGameSource_update_leds(int frame, ws2811_t* ledstrip)
{
    rad_game_source.cur_frame = frame;
    RadInputHandler_process_input();
    return current_mode_update_leds(ledstrip);
}

static void set_update_functions()
{

    switch (rad_game_mode.cur_mode)
    {
    case RGM_Oscillators:
        Player_hit_color = RGM_Oscillators_player_hit;
        Player_move = RGM_Oscillators_player_move;
        Player_start = 0x0;
        current_mode_update_leds = RGM_Oscillators_update_leds;
        break;
    case RGM_DDR:
        Player_hit_color = RGM_DDR_player_hit;
        Player_move = RGM_DDR_player_move;
        Player_start = 0x0;
        current_mode_update_leds = RGM_DDR_update_leds;
        break;
    case RGM_DDR_Ready:
        Player_hit_color = Ready_player_hit;
        Player_move = Ready_player_move;
        Player_start = Ready_player_start;
        current_mode_update_leds = RGM_DDR_Ready_update_leds;
        break;
    case RGM_N_MODES:
        exit(-1);
    }
}

//****************************** INIT, DESTRUCT, PROCESS_MESSAGE, READ_CONFIG *********************************************
//
// This is interface implememntation stuff
//

static void skip_comments_in_config(char* buf, FILE* config)
{
    fgets(buf, 1024, config);
    while (strnlen(buf, 2) > 0 && (buf[0] == ';' || buf[0] == '#'))
    {
        fgets(buf, 1024, config);
    }
}

static void read_songs_config(FILE* config, int n_songs)
{
    rad_game_songs.n_songs = n_songs;
    if (rad_game_songs.n_songs <= 0)
    {
        printf("Error reading R&D game config -- no songs\n");
        exit(-6);
    }
    char buf[1024];
    rad_game_songs.songs = malloc(sizeof(struct RadGameSong) * rad_game_songs.n_songs);
    for (int song = 0; song < rad_game_songs.n_songs; ++song)
    {
        skip_comments_in_config(buf, config);
        char fn[64];
        int n = sscanf(buf, "%s", fn);
        if (n != 1) { printf("Error reading filename in R&D game config for level %i\n", song); exit(10); }
        fn[63] = 0x0;
        int l = strnlen(fn, 64);
        rad_game_songs.songs[song].filename = (char*)malloc((size_t)6 + l + 1);
        strncpy(rad_game_songs.songs[song].filename, "sound/", 7);
        strncat(rad_game_songs.songs[song].filename, fn, (size_t)l + 1);
        skip_comments_in_config(buf, config);
        n = sscanf(buf, "%i", &rad_game_songs.songs[song].n_bpms);
        if (n != 1) { printf("Error reading n_bmps in R&D game config for level %i\n", song); exit(10); }
        rad_game_songs.songs[song].bpms = (double*)malloc(sizeof(double) * rad_game_songs.songs[song].n_bpms);
        rad_game_songs.songs[song].bpm_switch = (long*)malloc(sizeof(long) * rad_game_songs.songs[song].n_bpms);
        for (int bpm = 0; bpm < rad_game_songs.songs[song].n_bpms; bpm++)
        {
            skip_comments_in_config(buf, config);
            n = sscanf(buf, "%lf %li", &rad_game_songs.songs[song].bpms[bpm], &rad_game_songs.songs[song].bpm_switch[bpm]);
            if (n != 2) 
            { 
                printf("Error reading n_bmps in R&D game config for level %i\n", song); 
                exit(-10); 
            }
        }
    }
}

static void read_levels_config(FILE* config, int n_levels)
{
    rad_game_levels.n_levels = n_levels;
    rad_game_levels.levels = malloc(sizeof(struct RadGameLevel) * n_levels);
    char buf[1024];
    for (int level = 0; level < n_levels; ++level)
    {
        skip_comments_in_config(buf, config);
        int n = sscanf(buf, "%i %i %i", &rad_game_levels.levels[level].song_index, &rad_game_levels.levels[level].game_mode, &rad_game_levels.levels[level].target_score);
        if (n != 3)
        {
            printf("Invalid level definition\n");
            exit(-11);
        }
    }
}

static void read_rad_game_config()
{
    FILE* config = fopen("rad_game/config_rad", "r");
    if (config == NULL) {
        printf("R&D game config not found\n");
        exit(-4);
    }
    char buf[1024];
    skip_comments_in_config(buf, config);
    char keyword[16];
    int count;
    int n = sscanf(buf, "%s %i", keyword, &count);
    keyword[15] = 0;
    while (n == 2)
    {
        if (strncasecmp(keyword, "songs", 16) == 0)
        {
            read_songs_config(config, count);
        }
        if (strncasecmp(keyword, "levels", 16) == 0)
        {
            read_levels_config(config, count);
        }
        memset(buf, 0, 1024);
        skip_comments_in_config(buf, config);
        n = sscanf(buf, "%s %i", keyword, &count);
    }
}

void RadGameSource_init(int n_leds, int time_speed, uint64_t current_time)
{
    BasicSource_init(&rad_game_source.basic_source, n_leds, time_speed, source_config.colors[RAD_GAME_SOURCE], current_time);
    RadInputHandler_init();
    rad_game_source.start_time = current_time;
    rad_game_source.n_players = Controller_get_n_players();
    printf("Players detected: %i\n", rad_game_source.n_players);

    read_rad_game_config();
    /*
    rad_game_songs.current_song = 0;
    start_current_song();
    */
    SoundPlayer_init(20000); //TODO -- this is a parameter visible in led_main
    RGM_Oscillators_init();
    RGM_DDR_init();
    rad_game_mode.cur_mode = RGM_DDR_Ready;
    set_update_functions();
}

void RadGameSource_destruct()
{
    for (int i = 0; i < rad_game_songs.n_songs; ++i)
    {
        free(rad_game_songs.songs[i].filename);
        free(rad_game_songs.songs[i].bpms);
        free(rad_game_songs.songs[i].bpm_switch);
    }
    free(rad_game_songs.songs);
    free(rad_game_levels.levels);
    SoundPlayer_destruct();
}

void RadGameSource_construct()
{
    BasicSource_construct(&rad_game_source.basic_source);
    rad_game_source.basic_source.init = RadGameSource_init;
    rad_game_source.basic_source.update = RadGameSource_update_leds;
    rad_game_source.basic_source.destruct = RadGameSource_destruct;
    //game_source.basic_source.process_message = GameSource_process_message;
}

RadGameSource rad_game_source = {
    .basic_source.construct = RadGameSource_construct,
    //.heads = { 19, 246, 0, 38, 76, 114, 152, 190, 227, 265, 303, 341, 379, 417 }
    .start_time = 0,
    .n_players = 0
};

#pragma GCC diagnostic pop

