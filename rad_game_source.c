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
#include "sound_player.h"
#include "controller.h"
#include "colours.h"

//#define GAME_DEBUG

#pragma region Songs

/* ***************** Songs *******************
 * This is the same for all modes            */

struct RadGameSong
{
    char* filename;
    double* bpms;
    long* bpm_switch; //!< time in ms from the start of the song
    int n_bpms;
};

static struct
{
    struct RadGameSong* songs;
    double freq; //!< frequency (in Hz) of the current song
    int n_songs;
    int current_song;
} rad_game_songs;

void start_current_song()
{
    double bpm = rad_game_songs.songs[rad_game_songs.current_song].bpms[0];
    rad_game_songs.freq = bpm / 60.0;
    SoundPlayer_init(44100, 2, 20000, rad_game_songs.songs[rad_game_songs.current_song].filename);
}

#pragma endregion

#pragma region MovingObject

static struct RadMovingObject
{
    double position;
    double speed; //!< in leds/s
    int custom_data; //!< user defined
    signed char moving_dir; //< 0 when not moving, +1 when moving right, -1 when moving left
};

void RadMovingObject_render(struct RadMovingObject* mo, int color, ws2811_t* ledstrip)
{
    if (mo->moving_dir == 0)
    {
        ledstrip->channel[0].leds[(int)mo->position] = color;
    }
    else
    {
        double offset = (mo->position - trunc(mo->position)); //always positive
        int left_led = trunc(mo->position);
        ledstrip->channel[0].leds[left_led] = lerp_rgb(color, 0, offset);
        ledstrip->channel[0].leds[left_led + 1] = lerp_rgb(0, color, offset);
    }
}

#pragma endregion

#pragma region PlayerInput

enum ERAD_COLOURS
{
    DC_RED,
    DC_GREEN,
    DC_BLUE,
    DC_YELLOW
};

void (*Player_hit_color)(int, enum ERAD_COLOURS);
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

#pragma endregion

#pragma region Oscillators

/* ***************** Oscillators *************
 * Make the whole chaing blink game mode     */

static struct
{
    //! array of oscillators
    // first is amplitude, second and third are C and S, such that C*C + S*S == 1
    // ociallator equation is y = A * (C * sin(f t) + S * cos(f t))
    double* phases[3];
    enum ESoundEffects new_effect;  //!< should we start playing a new effect?
    const double S_coeff_threshold;
    const double out_of_sync_decay; //!< how much will out of sync oscillator decay per each update (does not depend on actual time)
    const int grad_length;
    hsl_t grad_colors[19];
    const double grad_speed; //leds per beat
    const int end_zone_width;
} oscillators =
{
    .S_coeff_threshold = 0.1,
    .out_of_sync_decay = 0.95,
    .grad_length = 19,
    .grad_speed = 0.5,
    .end_zone_width = 10
};

void Player_strike(int player_index, enum ERAD_COLOURS colour);

void Oscillators_init()
{
    for (int led = 0; led < rad_game_source.basic_source.n_leds; ++led)
    {
        if (led >= oscillators.end_zone_width && led < rad_game_source.basic_source.n_leds - oscillators.end_zone_width)
        {
            for (int i = 0; i < 3; ++i)
            {
                oscillators.phases[i][led] = 0.0;
            }
        }
        else
        {
            oscillators.phases[0][led] = 1.0;
            oscillators.phases[1][led] = 1.0;
            oscillators.phases[2][led] = 0.0;
        }
    }
    for (int i = 0; i < oscillators.grad_length; ++i)
    {
        rgb2hsl(rad_game_source.basic_source.gradient.colors[i], &oscillators.grad_colors[i]);
    }
    Player_hit_color = Player_strike;
}

int Oscillators_render(ws2811_t* ledstrip, double unhide_pattern)
{
    double time_seconds = ((rad_game_source.basic_source.current_time - rad_game_source.start_time) / (long)1e3) / (double)1e6;
    double sinft = sin(2 * M_PI * rad_game_songs.freq * time_seconds);
    double cosft = cos(2 * M_PI * rad_game_songs.freq * time_seconds);
    //printf("Time %f\n", time_seconds);

    int pattern_length = (int)((2 * oscillators.grad_length - 2) * unhide_pattern);
    if (pattern_length < 2) pattern_length = 2;
    int beats_count = trunc(time_seconds * rad_game_songs.freq);
    double grad_shift = fmod(beats_count * oscillators.grad_speed, pattern_length); // from 0 to pattern_length-1
    double offset = grad_shift - trunc(grad_shift); //from 0 to 1

    int in_sync = 0;
    for (int led = 0; led < rad_game_source.basic_source.n_leds; ++led)
    {
        double A = oscillators.phases[0][led];
        double C = oscillators.phases[1][led];
        double S = oscillators.phases[2][led];
        if (A > 1.0) A = 1.0;
        double y = A * (C * sinft + S * cosft);
        if (y < 0) y = 0;   //negative half-wave will not be shown at all

        int grad_pos = (led + (int)grad_shift) % pattern_length;    //from 0 to pattern_length-1, we have to take in account that the second half of the gradient is the reverse of the first one
        hsl_t col_hsl;
        if (grad_pos < oscillators.grad_length - 1)
            lerp_hsl(&oscillators.grad_colors[grad_pos], &oscillators.grad_colors[grad_pos + 1], offset, &col_hsl);
        else
            lerp_hsl(&oscillators.grad_colors[pattern_length - grad_pos], &oscillators.grad_colors[pattern_length - grad_pos - 1], 1 - offset, &col_hsl);
        ws2811_led_t c = hsl2rgb(&col_hsl);
        ledstrip->channel[0].leds[led] = multiply_rgb_color(c, y);

        /*int x = (int)(0xFF * y);
        int color = (y < 0) ? x << 16 : x;
        ledstrip->channel[0].leds[led] = color;*/

        if (A > oscillators.S_coeff_threshold)
        {
            if (S * S < oscillators.S_coeff_threshold && C > oscillators.S_coeff_threshold)
            {
                in_sync++;
            }
            else
            {
                oscillators.phases[0][led] *= oscillators.out_of_sync_decay;
            }
        }
        else
        {
            oscillators.phases[0][led] = 0.0;
            oscillators.phases[1][led] = 0.0;
            oscillators.phases[2][led] = 0.0;
        }

        //if (led == 1) printf("c %x\n", color);
    }
    return in_sync;
}

#pragma endregion

#pragma region OscPlayer

static struct
{
    struct RadMovingObject pos[C_MAX_CONTROLLERS]; //!< array of players
    long time_offset;                     //< in ms
    const double player_speed;            //!< player speed in LEDs/s
    const long player_pulse_width;        //!< the length of pulse in ns
    const long long player_period;        //!< how period (in ns) after the player's lead will blink
    const int single_strike_width;
}
    osc_players = 
{
    .time_offset = 0,
    .player_speed = 2.5,
    .player_pulse_width = (long)1e8,
    .player_period = (long long)(3e9),
    .single_strike_width = 1
};

//! array of players
//static struct Player players[C_MAX_CONTROLLERS];

void Player_move_left(int player_index)
{
    if (!osc_players.pos[player_index].moving_dir && osc_players.pos[player_index].position > oscillators.end_zone_width)
    {
        osc_players.pos[player_index].moving_dir = -1;
        osc_players.pos[player_index].position -= 0.0001;
    }
}

void Player_move_right(int player_index)
{
    if (!osc_players.pos[player_index].moving_dir && osc_players.pos[player_index].position < rad_game_source.basic_source.n_leds - oscillators.end_zone_width)
    {
        osc_players.pos[player_index].moving_dir = +1;
    }
}

/*!
* Player strikes at time t0. This creates oscillation with equation:
*   y = sin(2 pi f * (t - t0) + pi/2) = sin(2 pi f t + pi/2 - 2 pi f t0) = cos(pi/2 - 2 pi f t0) sin (2 pi f t) + sin(pi/2 - 2 pi f t0) cos (2 pi f t)
*/
void Player_strike(int player_index, enum ERAD_COLOURS colour)
{
    uint64_t phase_ns = rad_game_source.basic_source.current_time - rad_game_source.start_time;
    double phase_seconds = (phase_ns / (long)1e3) / (double)1e6;
    double impulse_C = cos(M_PI / 2.0 - 2.0 * M_PI * rad_game_songs.freq * phase_seconds);
    double impulse_S = sin(M_PI / 2.0 - 2.0 * M_PI * rad_game_songs.freq * phase_seconds);
    int player_pos = round(osc_players.pos[player_index].position);
    int good_strikes = 0;
    for (int led = player_pos - osc_players.single_strike_width; led < player_pos + osc_players.single_strike_width + 1; led++)
    {
        double unnormal_C = oscillators.phases[0][led] * oscillators.phases[1][led] + impulse_C;
        double unnormal_S = oscillators.phases[0][led] * oscillators.phases[2][led] + impulse_S;
        double len = sqrt(unnormal_C * unnormal_C + unnormal_S * unnormal_S);
        oscillators.phases[0][led] = len;
        oscillators.phases[1][led] = unnormal_C / len;
        oscillators.phases[2][led] = unnormal_S / len;
        if (oscillators.phases[2][led] < oscillators.S_coeff_threshold) good_strikes++;

        //printf("len %f\n", len);
    }
    if (good_strikes > osc_players.single_strike_width)
    {
        oscillators.new_effect = SE_Reward;
    }
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
    osc_players.time_offset += 50;
    rad_game_source.start_time += 50 * 1e6;
    printf("Time offset increased to %li ms\n", osc_players.time_offset);
}

void Player_time_offset_dec(int player_index)
{
    (void)player_index;
    osc_players.time_offset -= 50;
    rad_game_source.start_time -= 50 * 1e6;
    printf("Time offset decreased to %li ms\n", osc_players.time_offset);
}

void Players_init()
{
    if (rad_game_source.n_players == 0)
    {
        printf("No players detected\n");
        return;
    }
    int l = rad_game_source.basic_source.n_leds / rad_game_source.n_players;
    for (int i = 0; i < rad_game_source.n_players; i++)
    {
        osc_players.pos[i].position = l / 2 + i * l;
        osc_players.pos[i].moving_dir = 0;
    }
}

void Players_update()
{
    for (int p = 0; p < rad_game_source.n_players; ++p)
    {
        if (osc_players.pos[p].moving_dir == 0)
            continue;
        double offset = (osc_players.pos[p].position - trunc(osc_players.pos[p].position)); //always positive
        double distance_moved = ((rad_game_source.basic_source.time_delta / (long)1e3) / (double)1e6) * osc_players.player_speed;
        offset += osc_players.pos[p].moving_dir * distance_moved;
        if (offset > 1.0)
        {
            osc_players.pos[p].position = trunc(osc_players.pos[p].position) + 1.0;
            osc_players.pos[p].moving_dir = 0;
        }
        else if (offset < 0.0)
        {
            osc_players.pos[p].position = trunc(osc_players.pos[p].position);
            osc_players.pos[p].moving_dir = 0;
        }
        else
        {
            osc_players.pos[p].position = trunc(osc_players.pos[p].position) + offset;
        }
    }
}

/*!
 * @brief All players have the same color (white) and they occassionally blink. The pause between blinking is
 * `player_period`, the length of blink is `player_pulse_width` and the number of blinks is the player's number
 * (+ 1, of course). When blinking, player led goes completely dark
 * If the player is moving, his color is interpolated between two leds
 * @param ledstrip 
*/
void Players_render(ws2811_t* ledstrip)
{
    for (int p = 0; p < rad_game_source.n_players; p++)
    {
        int color = 0xFFFFFF;
        uint64_t pulse_time = (rad_game_source.basic_source.current_time - rad_game_source.start_time) % osc_players.player_period;
        if ((pulse_time / 2 / osc_players.player_pulse_width <= (unsigned int)p) && //we are in the pulse, the question is which half
            ((pulse_time / osc_players.player_pulse_width) % 2 == 0))
        {
            color = 0x0;
        }
        RadMovingObject_render(&osc_players.pos[p], color, ledstrip);
    }
}

void RGM_Oscillators_init()
{

}

int RGM_update_leds(int frame, ws2811_t* ledstrip)
{
    static double completed = 0;
    (void)frame;
    oscillators.new_effect = SE_N_EFFECTS;
    RadInputHandler_process_input();
    long time_pos = SoundPlayer_play(oscillators.new_effect);
    if (time_pos == -1)
    {
        SoundPlayer_destruct();
        if (rad_game_songs.n_songs == rad_game_songs.current_song++)
        {
            //TODO, game completed
            rad_game_songs.current_song = 0;
        }
        start_current_song();
    }
    //todo else -- check if bpm freq had not changed
    int in_sync = Oscillators_render(ledstrip, completed);
    completed = (double)(in_sync - 2 * oscillators.end_zone_width) / (rad_game_source.basic_source.n_leds - 2 * oscillators.end_zone_width);
    if (frame % 500 == 0) //TODO if in_sync = n_leds, players win
    {
        printf("Leds in sync: %i\n", in_sync);
    }
    Players_update();
    Players_render(ledstrip);

    return 1;
}

#pragma endregion

#pragma region DDR

#define C_MAX_DDR_BULLETS 8

struct DdrEmitor
{
    int emitor_pos; //!< start of the emitor
    int player_pos; //!< player position, this does not change
    int reward_pos; //!< reward strip start, this position also does not changes
    int emitor_len; //!< over the course of the game, emitors gets longer
    double reward_progress;
    struct RadMovingObject bullets[C_MAX_DDR_BULLETS];
    int n_bullets;
};

static struct 
{
    struct DdrEmitor data[C_MAX_CONTROLLERS];
    int last_beat;
    const int reward_len;
    const int player_col_index;
} 
ddr_emitors =
{
    .last_beat = 0,
    .reward_len = 8,
    .player_col_index = 19
};

void Player_hit_color_ddr(int player_index, enum EDDR_COLOURS colour)
{

}

void DDR_game_mode_init()
{
    srand(100);
    int field_len = rad_game_source.basic_source.n_leds / rad_game_source.n_players; //for 200 leds and 3 player = 66
    int offset = (rad_game_source.basic_source.n_leds % field_len) / 2;
    for (int em = 0; em < rad_game_source.n_players; ++em)
    {
        ddr_emitors.data[em].emitor_pos = offset + em * field_len;
        ddr_emitors.data[em].player_pos = offset + (em + 1) * field_len - ddr_emitors.reward_len - 2;
        ddr_emitors.data[em].reward_pos = offset + (em + 1) * field_len - ddr_emitors.reward_len;
        ddr_emitors.data[em].n_bullets = 0;
        ddr_emitors.data[em].reward_progress = 0.0;
    }
    Player_hit_color = Player_hit_color_ddr;
}



/*!
 * @brief will fire a bullet for all players, all of them with the same color
 * @param beats how many beats to reach the player
*/
void DdrEmitors_fire_bullet(int beats)
{
    int col = (int)(random_01() * 4); //enum EDDR_COLOURS
    for (int p = 0; p < rad_game_source.n_players; ++p)
    {
        if (ddr_emitors.data[p].n_bullets < C_MAX_DDR_BULLETS)
        {
            int dist = ddr_emitors.data[p].player_pos - ddr_emitors.data[p].emitor_pos - ddr_emitors.data[p].emitor_len - 1;
            double time_to_reach = (double)beats / rad_game_songs.freq;
            ddr_emitors.data[p].bullets[ddr_emitors.data[p].n_bullets++].position = ddr_emitors.data[p].emitor_pos + ddr_emitors.data[p].emitor_len + 1;
            ddr_emitors.data[p].bullets[ddr_emitors.data[p].n_bullets++].moving_dir = +1;
            ddr_emitors.data[p].bullets[ddr_emitors.data[p].n_bullets++].custom_data = col;
            ddr_emitors.data[p].bullets[ddr_emitors.data[p].n_bullets++].speed = (double)dist / time_to_reach;
        }
    }
}

void DdrEmitors_update()
{
    //check whether we want to emit a new bullet
    double time_seconds = ((rad_game_source.basic_source.current_time - rad_game_source.start_time) / (long)1e3) / (double)1e6;
    double beat = time_seconds * rad_game_songs.freq;
    //todo -- emit bullets in other phases of the beat?
    if ((int)beat > ddr_emitors.last_beat)
    {
        ddr_emitors.last_beat = (int)beat;
        if (random_01() > 0.1f)
        {
            DdrEmitors_fire_bullet(6);
        }
    }
}



#pragma endregion 

int(*get_update_function())(int, ws2811_t)
{
    switch (rad_game_source.game_mode)
    {
    case RGM_Oscillators:
        return RGM_update_leds;
    default:
        break;
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
    int n = sscanf(buf, "%s %i", keyword, &rad_game_songs.n_songs);
    if (n != 2) {
        printf("Error reading R&D game config\n");
        exit(-5);
    }
    //todo check that the keyword is "Songs"
    rad_game_songs.songs = malloc(sizeof(struct RadGameSong) * rad_game_songs.n_songs);
    for(int song = 0; song < rad_game_songs.n_songs; ++song)
    {
        skip_comments_in_config(buf, config);
        char fn[64];
        n = sscanf(buf, "%s", fn);
        if (n != 1) { printf("Error reading filename in R&D game config for level %i\n", song); exit(10); }
        fn[63] = 0x0;
        int l = strnlen(fn, 64);
        rad_game_songs.songs[song].filename = (char*)malloc(6 + l + 1);
        strncpy(rad_game_songs.songs[song].filename, "sound/", 7);
        strncat(rad_game_songs.songs[song].filename, fn, l + 1);
        skip_comments_in_config(buf, config);
        n = sscanf(buf, "%i", &rad_game_songs.songs[song].n_bpms);
        if (n != 1) { printf("Error reading n_bmps in R&D game config for level %i\n", song); exit(10); }
        rad_game_songs.songs[song].bpms = (double*)malloc(sizeof(double) * rad_game_songs.songs[song].n_bpms);
        rad_game_songs.songs[song].bpm_switch = (long*)malloc(sizeof(long) * rad_game_songs.songs[song].n_bpms);
        for (int bpm = 0; bpm < rad_game_songs.songs[song].n_bpms; bpm++)
        {
            skip_comments_in_config(buf, config);
            n = sscanf(buf, "%lf %li", &rad_game_songs.songs[song].bpms[bpm], &rad_game_songs.songs[song].bpm_switch[bpm]);
            if (n != 2) { printf("Error reading n_bmps in R&D game config for level %i\n", song); exit(10); }
        }
    }
}

void RadGameSource_init(int n_leds, int time_speed, uint64_t current_time)
{
    BasicSource_init(&rad_game_source.basic_source, n_leds, time_speed, source_config.colors[RAD_GAME_SOURCE], current_time);

    rad_game_source.start_time = current_time;
    for(int i = 0; i < 3; ++i)
        oscillators.phases[i] = malloc(sizeof(double) * n_leds);
    RadInputHandler_init();
    rad_game_source.n_players = Controller_get_n_players();
    printf("Players detected: %i\n", rad_game_source.n_players);

    //TODO move following to osc_init()
    Oscillators_init();
    Players_init();
    read_rad_game_config();
    rad_game_songs.current_song = 0;
    start_current_song();
}

void RadGameSource_destruct()
{
    for (int i = 0; i < 3; ++i)
    {
        free(oscillators.phases[i]);
    }
    for (int i = 0; i < rad_game_songs.n_songs; ++i)
    {
        free(rad_game_songs.songs[i].filename);
        free(rad_game_songs.songs[i].bpms);
        free(rad_game_songs.songs[i].bpm_switch);
    }
    free(rad_game_songs.songs);
    SoundPlayer_destruct();
}

void RadGameSource_construct()
{
    BasicSource_construct(&rad_game_source.basic_source);
    rad_game_source.basic_source.init = RadGameSource_init;
    rad_game_source.basic_source.update = get_update_function();
    rad_game_source.basic_source.destruct = RadGameSource_destruct;
    //game_source.basic_source.process_message = GameSource_process_message;
}

RadGameSource rad_game_source = {
    .basic_source.construct = RadGameSource_construct,
    //.heads = { 19, 246, 0, 38, 76, 114, 152, 190, 227, 265, 303, 341, 379, 417 }
    .start_time = 0,
    .n_players = 0
};
