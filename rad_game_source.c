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

//#define GAME_DEBUG

//oscillator frequency in Hz
static double rad_freq;

//! array of oscillators
// first is amplitude, second and third are C and S, such that C*C + S*S == 1
// ociallator equation is y = A * (C * sin(f t) + S * cos(f t))
static double* oscillators[3];

//! array of players
static int players[C_MAX_CONTROLLERS];

//!should we start playing a new effect?
static enum ESoundEffects new_effect;

static long time_offset = 0; //< in ms
static const double S_coeff_threshold = 0.1;

void Player_move_left(int player_index)
{
    players[player_index]--;
}

void Player_move_right(int player_index)
{
    players[player_index]++;
}

/*!
* Player strikes at time t0. This creates oscillation with equation:
*   y = sin(2 pi f * (t - t0) + pi/2) = sin(2 pi f t + pi/2 - 2 pi f t0) = cos(pi/2 - 2 pi f t0) sin (2 pi f t) + sin(pi/2 - 2 pi f t0) cos (2 pi f t)
*/
void Player_strike(int player_index)
{
    uint64_t phase_ns = rad_game_source.basic_source.current_time - rad_game_source.start_time;
    double phase_seconds = (phase_ns / (long)1e3) / (double)1e6;
    double impulse_C = cos(M_PI / 2.0 - 2.0 * M_PI * rad_freq * phase_seconds);
    double impulse_S = sin(M_PI / 2.0 - 2.0 * M_PI * rad_freq * phase_seconds);
    int player_pos = players[player_index];
    int good_strikes = 0;
    for (int led = player_pos - 5; led < player_pos + 5; led++)
    {
        double unnormal_C = oscillators[0][led] * oscillators[1][led] + impulse_C * 1.0 / (1 + (led - player_pos) * (led - player_pos));
        double unnormal_S = oscillators[0][led] * oscillators[2][led] + impulse_S * 1.0 / (1 + (led - player_pos) * (led - player_pos));
        double len = sqrt(unnormal_C * unnormal_C + unnormal_S * unnormal_S);
        oscillators[0][led] = len;
        oscillators[1][led] = unnormal_C / len;
        oscillators[2][led] = unnormal_S / len;
        if (oscillators[2][led] < S_coeff_threshold) good_strikes++;

        //printf("len %f\n", len);
    }
    if (good_strikes > 5)
    {
        new_effect = SE_Reward;
    }
}

void Player_freq_inc(int player_index)
{
    (void)player_index;
    rad_freq += 0.01;
    printf("Frequence increased to %f\n", rad_freq);
}

void Player_freq_dec(int player_index)
{
    (void)player_index;
    rad_freq -= 0.01;
    printf("Frequence lowered to %f\n", rad_freq);
}

void Player_time_offset_inc(int player_index)
{
    (void)player_index;
    time_offset += 50;
    rad_game_source.start_time += 50 * 1e6;
    printf("Time offset increased to %li ms\n", time_offset);
}

void Player_time_offset_dec(int player_index)
{
    (void)player_index;
    time_offset -= 50;
    rad_game_source.start_time -= 50 * 1e6;
    printf("Time offset decreased to %li ms\n", time_offset);
}

/*int check_oscillators(int led, int** color)
{

}*/

int RadGameSource_update_leds(int frame, ws2811_t* ledstrip)
{
    (void)frame;
    new_effect = SE_N_EFFECTS;
    RadInputHandler_process_input();
    long time_pos = SoundPlayer_play(new_effect);
    if (time_pos == -1)
    {
        //TODO start a new song?
        SoundPlayer_destruct();
        SoundPlayer_init(44100, 2, 20000, "sound/GodRestYeMerryGentlemen.wav");
    }

    double time_seconds = ((rad_game_source.basic_source.current_time - rad_game_source.start_time)  / (long)1e3) / (double)1e6;
    double sinft = sin(2 * M_PI * rad_freq * time_seconds);
    double cosft = cos(2 * M_PI * rad_freq * time_seconds);
    //printf("Time %f\n", time_seconds);

    int in_sync = 0;
    for (int led = 0; led < rad_game_source.basic_source.n_leds; ++led)
    {
        double A = oscillators[0][led];
        double C = oscillators[1][led];
        double S = oscillators[2][led];
        if (A > 1.0) A = 1.0;
        double y = A * (C * sinft + S * cosft);
        double absy = (y < 0) ? -y : y;
        int x = (int)(0xFF * absy);
        int color = (y < 0) ? x << 16 : x;
        ledstrip->channel[0].leds[led] = color;

        if (A > S_coeff_threshold)
        {
            if (S < S_coeff_threshold)
            {
                in_sync++;
            }
            else
            {
                oscillators[0][led] *= 0.5;
            }
        }
        else
        {
            oscillators[0][led] = 0.0;
            oscillators[1][led] = 0.0;
            oscillators[2][led] = 0.0;
        }

       //if (led == 1) printf("c %x\n", color);
    }
    for (int i = 0; i < rad_game_source.n_players; i++)
    {
        ledstrip->channel[0].leds[players[i]] = 0xFFFFFF;
    }
    if (frame % 500 == 0)
    {
        printf("Leds in sync: %i\n", in_sync);
    }
 
    return 1;
}

void InitPlayers()
{
    if (rad_game_source.n_players == 0)
    {
        printf("No players detected\n");
        return;
    }
    int l = rad_game_source.basic_source.n_leds / rad_game_source.n_players;
    for (int i = 0; i < rad_game_source.n_players; i++)
    {
        players[i] = l / 2 + i * l;
    }
}

void InitOscillators()
{
    for (int led = 0; led < rad_game_source.basic_source.n_leds; ++led)
    {
        if (led > 5 && led < rad_game_source.basic_source.n_leds - 5)
        {
            for (int i = 0; i < 3; ++i)
            {
                oscillators[i][led] = 0.0;
            }
        }
        else
        {
            oscillators[0][led] = 1.0;
            oscillators[1][led] = 1.0;
            oscillators[2][led] = 0.0;
        }
    }
}

void RadGameSource_init(int n_leds, int time_speed, uint64_t current_time)
{
    BasicSource_init(&rad_game_source.basic_source, n_leds, time_speed, source_config.colors[CHASER_SOURCE], current_time); //TODO change colors

    const double BPM = 72.02;
    rad_freq = BPM / 60.0;

    rad_game_source.start_time = current_time;
    for(int i = 0; i < 3; ++i)
        oscillators[i] = malloc(sizeof(double) * n_leds);
    RadInputHandler_init();
    rad_game_source.n_players = Controller_get_n_players();
    printf("Players detected: %i\n", rad_game_source.n_players);
    InitOscillators();
    InitPlayers();
    SoundPlayer_init(44100, 2, 20000, "sound/GodRestYeMerryGentlemen.wav");
}

void RadGameSource_destruct()
{
    for (int i = 0; i < 3; ++i)
        free(oscillators[i]);
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
