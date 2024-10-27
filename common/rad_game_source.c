#define _CRT_SECURE_NO_WARNINGS
/*
 * TODO
 * [x] score for DDR
 * [x] state machine
 * [x] finish Oscillator gameplay
 * [x] sound_player -- effect without song
 * [x] start_time should be reset in *_clear() functions -- or elsewhere, when we are starting a new song
 * [x] ready states -- show players where they are
 * [x] display playing field outline in ready state
 * [x] show score state
 * [x] message processing
 * [ ] multiple wifi config
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
#include "source_manager.h"
#include "rad_game_source.h"
#include "rad_input_handler.h"
#include "sound_player.h"
#include "controller.h"
#include "oscillators.h"
#include "ddr_game.h"
#include "nonplaying_states.h"

//#define GAME_DEBUG



double RadGameSource_time_from_start_seconds()
{
    return ((rad_game_source.basic_source.current_time - rad_game_source.start_time) / 1000l) / (double)1e6;
}

void RadGameSource_set_start()
{
    rad_game_source.start_time = rad_game_source.basic_source.current_time;
}

/****************** Game mode *******************/
/*
The flow of the game is:

    1. Level (see level flow below)
    2. If Level is passed, continue, otherwise repeat it
    3. Start next level and go to 2 if there is one
    4. If there is no next level, players won

The single level flow is:
    1. "Get ready" announcement
    2. Wait for players to press Start button on their controllers - done
    3. "Let's go" announcement
    4. Playing state -- core gameplay
    5. Result announcement: "Level fail" or "Level cleared"
    6. Disclose level code (passphrase)
    7. Show score - done
    8. Wait until players either press Start buttons or wait for timeout

Players Won state:
    1. play cryptic message

*/

static struct
{
    enum ERadGameModes cur_mode;
    long score;	        	    //!< only used in show_score mode
    int player;	                //!< only used in ready modes, active player, -1 if there is not active player
    unsigned int ready_players; //!< bit mask of players that pressed Start
    long state;                 //!< custom data for non-playing modes
} rad_game_mode =
{
    .player = -1
};

struct RadGameLevel
{
    int song_index;
    enum ERadGameModes game_mode;
    long target_score;
    char code[32];
    char code_wav[32];
};

static struct {
    struct RadGameLevel* levels;
    int n_levels;
    int cur_level;
    int last_level_result; //0 loss, 1 win
} rad_game_levels =
{
    .cur_level = -1,
    .last_level_result = 1
};

/*!
 * @brief Every mode has to override the following functions:
 *      _clear() -- this is called once, when the mode is switched into
 *      _player_hit, _player_move, _player_start -- how to handle player input
 *      _update_leds -- the main update/render loop
 * @param game_mode Mode to switch into
*/
static void RadGameMode_switch_mode(enum ERadGameModes game_mode);
static void start_current_song();

void RadGameLevel_ready_finished()
{
    rad_game_songs.current_song = rad_game_levels.levels[rad_game_levels.cur_level].song_index;
    RadGameMode_switch_mode(rad_game_levels.levels[rad_game_levels.cur_level].game_mode);
    start_current_song();
}

/*!
 * @brief Called when one of the "proper" game modes is finished
 *      It will then display the score, play You win/You lose jingle
 * @param result 0 for failure, 1 for success
 * @param score 
*/
void RadGameLevel_level_finished(long points)
{
    if (points < 0)
    {
        points = 100;
        printf("ERROR -- negative score");
    }
    int result = rad_game_levels.levels[rad_game_levels.cur_level].target_score <= points ? 1 : 0;
    printf("The level result was %i, players scored %li points of %li required\n", result, points, rad_game_levels.levels[rad_game_levels.cur_level].target_score);
    rad_game_levels.last_level_result = result;
    rad_game_mode.score = points;
    RadGameMode_switch_mode(RGM_Show_Score);
}

void RadGameLevel_score_finished()
{
    int next_level = rad_game_levels.last_level_result == 0 ? rad_game_levels.cur_level : rad_game_levels.cur_level + 1;
    if (next_level == rad_game_levels.n_levels)
    {
        printf("Players won\n");
        RadGameMode_switch_mode(RGM_Game_Won);
        return;
    }
    rad_game_levels.cur_level = next_level;
    if (rad_game_levels.levels[next_level].game_mode == RGM_DDR)
    {
        RadGameMode_switch_mode(RGM_DDR_Ready);
        return;
    }
    else if (rad_game_levels.levels[next_level].game_mode == RGM_Oscillators)
    {
        RadGameMode_switch_mode(RGM_Osc_Ready);
        return;
    }
    else
    {
        printf("Invalid game mode in level %i\n", next_level);
        exit(-1);
    }
}

/****************** Songs *******************/

RadGameSongs rad_game_songs =
{
    .time_offset = 0
};

int RadGameSong_update_freq(long t)
{
    t += rad_game_songs.time_offset;
    if ((rad_game_songs.current_bpm_index < rad_game_songs.songs[rad_game_songs.current_song].n_bpms - 1) &&
        (rad_game_songs.songs[rad_game_songs.current_song].bpm_switch[rad_game_songs.current_bpm_index + 1] < t))
    {
        printf("Song frequency changed\n");
        rad_game_songs.current_bpm_index++;
        rad_game_songs.current_beat += (rad_game_songs.freq * rad_game_songs.songs[rad_game_songs.current_song].bpm_switch[rad_game_songs.current_bpm_index] - 
            rad_game_songs.freq * rad_game_songs.last_update) / 1e6;
        rad_game_songs.freq = rad_game_songs.songs[rad_game_songs.current_song].bpms[rad_game_songs.current_bpm_index] / 60.0;
        rad_game_songs.current_beat += (rad_game_songs.freq * t - 
            rad_game_songs.freq * rad_game_songs.songs[rad_game_songs.current_song].bpm_switch[rad_game_songs.current_bpm_index]) / 1e6;
        rad_game_songs.last_update = t;
        return 1;
    }
    rad_game_songs.current_beat += (rad_game_songs.freq * t - rad_game_songs.freq * rad_game_songs.last_update) / 1e6;
    rad_game_songs.last_update = t;
    return 0;
}

static void start_current_song()
{
    double bpm = rad_game_songs.songs[rad_game_songs.current_song].bpms[0];
    rad_game_songs.freq = bpm / 60.0;
    rad_game_songs.current_bpm_index = 0;
    rad_game_songs.current_beat = 0;
    rad_game_songs.last_update = 0;
    rad_game_songs.time_offset = rad_game_songs.songs[rad_game_songs.current_song].delay * 1000;
    SoundPlayer_start(rad_game_songs.songs[rad_game_songs.current_song].filename);
}

void RadGameSong_start_random()
{
    rad_game_songs.current_song = (int)(random_01() * rad_game_songs.n_songs);
    start_current_song();
}


/****************** Moving Object *******************/
 
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


/****************** Player input *******************/




/****************** Common ready states stuff *******************/

void GameMode_player_pressed_start(int player_index)
{
    if (rad_game_mode.player == -1)
    {
        rad_game_mode.ready_players |= 1 << player_index;
        rad_game_mode.player = player_index;
    }
}

void GameMode_clear()
{
    rad_game_mode.player = -1;
    rad_game_mode.ready_players = 0;
    rad_game_mode.state = 0;
    RadGameSource_set_start();
}
int GameMode_get_current_player()
{
    return rad_game_mode.player;
}
void GameMode_clear_current_player()
{
    rad_game_mode.player = -1;
}
void GameMode_lock_current_player()
{
    rad_game_mode.player = -2;
}
int GameMode_get_ready_players()
{
    return rad_game_mode.ready_players;
}
long GameMode_get_score()
{
    return rad_game_mode.score;
}

long GameMode_get_state()
{
    return rad_game_mode.state;
}

void GameMode_set_state(long s)
{
    rad_game_mode.state = s;
}

int GameMode_get_last_result()
{
    return rad_game_levels.last_level_result;
}

char* GameMode_get_code_wav()
{
    return rad_game_levels.levels[rad_game_levels.cur_level].code_wav;
}

/****************** Actual RadGameSource update *******************/

int (*current_mode_update_leds)(ws2811_t* ledstrip);
int RadGameSource_update_leds(int frame, ws2811_t* ledstrip)
{
    rad_game_source.cur_frame = frame;
    RadInputHandler_process_input();
    return current_mode_update_leds(ledstrip);
}

static void RadGameMode_switch_mode(enum ERadGameModes game_mode)
{
    rad_game_mode.cur_mode = game_mode;
    rad_game_source.start_time = rad_game_source.basic_source.current_time;
    switch (game_mode)
    {
    case RGM_Oscillators:
        RGM_Oscillators_clear();
        rad_game_source.Player_hit_color = RGM_Oscillators_player_hit;
        rad_game_source.Player_move = RGM_Oscillators_player_move;
        rad_game_source.Player_start = 0x0;
        current_mode_update_leds = RGM_Oscillators_update_leds;
        break;
    case RGM_DDR:
        RGM_DDR_clear();
        rad_game_source.Player_hit_color = RGM_DDR_player_hit;
        rad_game_source.Player_move = RGM_DDR_player_move;
        rad_game_source.Player_start = 0x0;
        current_mode_update_leds = RGM_DDR_update_leds;
        break;
    case RGM_DDR_Ready:
        RGM_DDR_Ready_clear();
        rad_game_source.Player_hit_color = Ready_player_hit;
        rad_game_source.Player_move = Ready_player_move;
        rad_game_source.Player_start = GameMode_player_pressed_start;
        current_mode_update_leds = RGM_DDR_Ready_update_leds;
        break;
    case RGM_Osc_Ready:
        RGM_Osc_Ready_clear();
        rad_game_source.Player_hit_color = Ready_player_hit;
        rad_game_source.Player_move = RGM_Oscillators_player_move;
        rad_game_source.Player_start = GameMode_player_pressed_start;
        current_mode_update_leds = RGM_Oscillators_Ready_update_leds;
        break;
    case RGM_Show_Score:
        RGM_Show_Score_clear();
        rad_game_source.Player_hit_color = Ready_player_hit;
        rad_game_source.Player_move = Ready_player_move;
        rad_game_source.Player_start = GameMode_player_pressed_start;
        current_mode_update_leds = RGM_Show_Score_update_leds;
        break;
    case RGM_Game_Won:
        RGM_GameWon_clear();
        rad_game_source.Player_hit_color = Ready_player_hit;
        rad_game_source.Player_move = Ready_player_move;
        rad_game_source.Player_start = 0x0;
        current_mode_update_leds = RGM_GameWon_update_leds;
        break;
    case RGM_N_MODES:
        printf("Unknown game mode\n");
        exit(-1);
    }
}

//****************************** INIT, DESTRUCT, PROCESS_MESSAGE, READ_CONFIG *********************************************
//
// This is interface implememntation stuff
//

static char* skip_comments_in_config(char* buf, FILE* config)
{
    char* b = fgets(buf, 1024, config);
    while (strnlen(buf, 2) > 0 && (buf[0] == ';' || buf[0] == '#'))
    {
        b = fgets(buf, 1024, config);
    }
    return b;
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
        char* c = skip_comments_in_config(buf, config);
        if (c == NULL)
        {
            printf("Error reading song config -- probably not enough songs defined\n");
            exit(-17);
        }
        char fn[64];
        int n = sscanf(buf, "%s", fn);
        if (n != 1) { printf("Error reading filename in R&D game config for level %i\n", song); exit(10); }
        fn[63] = 0x0;
        size_t l = strnlen(fn, 64);
        rad_game_songs.songs[song].filename = (char*)malloc(l + 6 + 1);
        snprintf(rad_game_songs.songs[song].filename, l + 6 + 1, "sound/%s", fn);
        c = skip_comments_in_config(buf, config);
        if (c == NULL)
        {
            printf("Error reading song config -- probably missing BPM definition for song %s\n", fn);
            exit(-15);
        }
        n = sscanf(buf, "%i %li %i", &rad_game_songs.songs[song].n_bpms, &rad_game_songs.songs[song].delay, &rad_game_songs.songs[song].signature);
        if (n != 3) { printf("Error reading n_bmps in R&D game config for level %i\n", song); exit(10); }
        rad_game_songs.songs[song].bpms = (double*)malloc(sizeof(double) * rad_game_songs.songs[song].n_bpms);
        rad_game_songs.songs[song].bpm_switch = (long*)malloc(sizeof(long) * rad_game_songs.songs[song].n_bpms);
        for (int bpm = 0; bpm < rad_game_songs.songs[song].n_bpms; bpm++)
        {
            c = skip_comments_in_config(buf, config);
            if (c == NULL)
            {
                printf("Error reading song config -- probably not enough bpm defined for song %s\n", fn);
                exit(-16);
            }
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
        char* c = skip_comments_in_config(buf, config);
        if (c == NULL)
        {
            printf("Error reading level config -- probably not enough levels defined.\n");
            exit(-18);
        }
        int n = sscanf(buf, "%i %i %li %s \"%32[^\"]\"", 
            &rad_game_levels.levels[level].song_index, 
            (int*)&rad_game_levels.levels[level].game_mode, 
            &rad_game_levels.levels[level].target_score,
            rad_game_levels.levels[level].code,
            rad_game_levels.levels[level].code_wav);
        if (n != 5)
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
        else if (strncasecmp(keyword, "levels", 16) == 0)
        {
            read_levels_config(config, count);
        }
        else {
            printf("Invalid keyword in R&D game config: %s\n", keyword);
            exit(-20);
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
    rad_game_source.Player_start = 0x0;
    printf("Players detected: %i\n", rad_game_source.n_players);
    if (rad_game_source.n_players == 0)
    {
        SourceManager_switch_to_source(IP_SOURCE);
        return;
    }

    read_rad_game_config();
    /*
    rad_game_songs.current_song = 0;
    start_current_song();
    */
    SoundPlayer_init(20000); //TODO -- this is a parameter visible in led_main
    RGM_Oscillators_init();
    RGM_DDR_init();
    RadGameLevel_score_finished();    
}

//msg = mode?xxxxxx
void RadGameSource_process_message(const char* msg)
{
    char* sep = strchr(msg, '?');
    if (sep == NULL)
    {
        printf("RadGameSource: message does not contain target %s\n", msg);
        return;
    }
    if ((sep - msg) >= 32)
    {
        printf("RadGameSource: target is too long or poorly formatted: %s\n", msg);
        return;
    }
    if ((strlen(sep + 1) >= 64))
    {
        printf("RadGameSource: message too long or poorly formatted: %s\n", msg);
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
        for (int l = 0; l < rad_game_levels.n_levels; l++)
        {
            if (!strncasecmp(rad_game_levels.levels[l].code, payload, 32))
            {
                rad_game_levels.last_level_result = 1;
                rad_game_levels.cur_level = l;
                SoundPlayer_stop();
                RadGameLevel_score_finished();
                printf("RGM Received code: %s, starting level: %i\n", payload, l+1);
                return;
            }
        }
        printf("RGM Received code: %s, no matching level found\n", payload);

    }
    else
        printf("RadGameSource: Unknown target: %s, payload was: %s\n", target, payload);

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
    rad_game_source.basic_source.process_message = RadGameSource_process_message;
}

RadGameSource rad_game_source = {
    .basic_source.construct = RadGameSource_construct,
    //.heads = { 19, 246, 0, 38, 76, 114, 152, 190, 227, 265, 303, 341, 379, 417 }
    .start_time = 0,
    .n_players = 0
};

