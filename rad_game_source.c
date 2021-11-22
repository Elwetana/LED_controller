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

struct RadGameLevels
{
    char* filename;
    double* bpms;
    long* bpm_switch; //!< time in ms from the start of the song
    int n_bpms;
};

static struct RadGameLevels* rad_game_levels;
static int n_levels;
static int current_level;

//oscillator frequency in Hz
static double rad_freq;

//! array of oscillators
// first is amplitude, second and third are C and S, such that C*C + S*S == 1
// ociallator equation is y = A * (C * sin(f t) + S * cos(f t))
static double* oscillators[3];
static const double out_of_sync_decay = 0.9; //!< how much will out of sync oscillator decay per each update (does not depend on actual time)

static const int grad_length = 19;
static hsl_t grad_colors[19];
static const double grad_speed = 0.5; //leds per beat

struct Player
{
    double position;
    signed char moving_dir; //< 0 when not moving, +1 when moving right, -1 when moving left
};

//! array of players
static struct Player players[C_MAX_CONTROLLERS];

static const double player_speed = 2.5;                     //!< player speed in LEDs/s
static const long player_pulse_width = (long)1e8;           //!< the length of pulse in ns
static const long long player_period = (long long)(3e9);    //!< how period (in ns) after the player's lead will blink

//!should we start playing a new effect?
static enum ESoundEffects new_effect;

static long time_offset = 0; //< in ms
static const double S_coeff_threshold = 0.1;
static const int end_zone_width = 10;
static const int single_strike_width = 1;

void Player_move_left(int player_index)
{
    if (!players[player_index].moving_dir && players[player_index].position > end_zone_width)
    {
        players[player_index].moving_dir = -1;
        players[player_index].position -= 0.0001;
    }
}

void Player_move_right(int player_index)
{
    if (!players[player_index].moving_dir && players[player_index].position < rad_game_source.basic_source.n_leds - end_zone_width)
    {
        players[player_index].moving_dir = +1;
    }
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
    int player_pos = round(players[player_index].position);
    int good_strikes = 0;
    for (int led = player_pos - single_strike_width; led < player_pos + single_strike_width + 1; led++)
    {
        double unnormal_C = oscillators[0][led] * oscillators[1][led] + impulse_C;
        double unnormal_S = oscillators[0][led] * oscillators[2][led] + impulse_S;
        double len = sqrt(unnormal_C * unnormal_C + unnormal_S * unnormal_S);
        oscillators[0][led] = len;
        oscillators[1][led] = unnormal_C / len;
        oscillators[2][led] = unnormal_S / len;
        if (oscillators[2][led] < S_coeff_threshold) good_strikes++;

        //printf("len %f\n", len);
    }
    if (good_strikes > single_strike_width)
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

void update_players()
{
    for (int p = 0; p < rad_game_source.n_players; ++p)
    {
        if (players[p].moving_dir == 0)
            continue;
        double offset = (players[p].position - trunc(players[p].position)); //always positive
        double distance_moved = ((rad_game_source.basic_source.time_delta / (long)1e3) / (double)1e6) * player_speed;
        offset += players[p].moving_dir * distance_moved;
        if (offset > 1.0)
        {
            players[p].position = trunc(players[p].position) + 1.0;
            players[p].moving_dir = 0;
        }
        else if (offset < 0.0)
        {
            players[p].position = trunc(players[p].position);
            players[p].moving_dir = 0;
        }
        else
        {
            players[p].position = trunc(players[p].position) + offset;
        }
    }
}

int render_oscillators(ws2811_t* ledstrip, double unhide_pattern)
{
    double time_seconds = ((rad_game_source.basic_source.current_time - rad_game_source.start_time) / (long)1e3) / (double)1e6;
    double sinft = sin(2 * M_PI * rad_freq * time_seconds);
    double cosft = cos(2 * M_PI * rad_freq * time_seconds);
    //printf("Time %f\n", time_seconds);

    int pattern_length = (int)((2 * grad_length - 2) * unhide_pattern);
    if (pattern_length < 2) pattern_length = 2;
    int beats_count = trunc(time_seconds * rad_freq);
    double grad_shift = fmod(beats_count * grad_speed, pattern_length); // from 0 to pattern_length-1
    double offset = grad_shift - trunc(grad_shift); //from 0 to 1

    int in_sync = 0;
    for (int led = 0; led < rad_game_source.basic_source.n_leds; ++led)
    {
        double A = oscillators[0][led];
        double C = oscillators[1][led];
        double S = oscillators[2][led];
        if (A > 1.0) A = 1.0;
        double y = A * (C * sinft + S * cosft);
        if (y < 0) y = 0;   //negative half-wave will not be shown at all
                
        int grad_pos = (led + (int)grad_shift) % pattern_length;    //from 0 to pattern_length-1, we have to take in account that the second half of the gradient is the reverse of the first one
        hsl_t col_hsl;
        if(grad_pos < grad_length - 1)
            lerp_hsl(&grad_colors[grad_pos], &grad_colors[grad_pos + 1], offset, &col_hsl);
        else
            lerp_hsl(&grad_colors[pattern_length - grad_pos], &grad_colors[pattern_length - grad_pos - 1], 1 - offset, &col_hsl);
        ws2811_led_t c = hsl2rgb(&col_hsl);
        ledstrip->channel[0].leds[led] = multiply_rgb_color(c, y);

        /*int x = (int)(0xFF * y);
        int color = (y < 0) ? x << 16 : x;
        ledstrip->channel[0].leds[led] = color;*/

        if (A > S_coeff_threshold)
        {
            if (S < S_coeff_threshold)
            {
                in_sync++;
            }
            else
            {
                oscillators[0][led] *= out_of_sync_decay;
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
    return in_sync;
}

void render_players(ws2811_t* ledstrip)
{
    for (int p = 0; p < rad_game_source.n_players; p++)
    {
        int color = 0xFFFFFF;
        uint64_t pulse_time = (rad_game_source.basic_source.current_time - rad_game_source.start_time) % player_period;
        if ((pulse_time / 2 / player_pulse_width <= (unsigned int)p) && //we are in the pulse, the question is which half
            ((pulse_time / player_pulse_width) % 2 == 0))
        {
            color = 0x0;
        }
        int pos = trunc(players[p].position);
        if (players[p].moving_dir == 0)
        {
            ledstrip->channel[0].leds[pos] = color;
        }
        else
        {
            double offset = (players[p].position - trunc(players[p].position)); //always positive
            int left_led = trunc(players[p].position);
            ledstrip->channel[0].leds[left_led] = lerp_rgb(color, 0, offset);
            ledstrip->channel[0].leds[left_led + 1] = lerp_rgb(0, color, offset);
        }

    }
}

void start_song_current_level()
{
    double bpm = rad_game_levels[current_level].bpms[0];
    rad_freq = bpm / 60.0;
    SoundPlayer_init(44100, 2, 20000, rad_game_levels[current_level].filename);
}
 

int RadGameSource_update_leds(int frame, ws2811_t* ledstrip)
{
    static double completed = 0;
    (void)frame;
    new_effect = SE_N_EFFECTS;
    RadInputHandler_process_input();
    long time_pos = SoundPlayer_play(new_effect);
    if (time_pos == -1)
    {
        SoundPlayer_destruct();
        if (n_levels == current_level++)
        {
            //TODO, game completed
            current_level = 0;
        }
        start_song_current_level();
    }
    //todo else -- check if bpm freq had not changed
    int in_sync = render_oscillators(ledstrip, completed);
    completed = (double)(in_sync - 2 * end_zone_width) / (rad_game_source.basic_source.n_leds - 2 * end_zone_width);
    if (frame % 500 == 0) //TODO if in_sync = n_leds, players win
    {
        printf("Leds in sync: %i\n", in_sync);
    }
    update_players();
    render_players(ledstrip);

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
        players[i].position = l / 2 + i * l;
        players[i].moving_dir = 0;
    }
}

void InitOscillators()
{
    for (int led = 0; led < rad_game_source.basic_source.n_leds; ++led)
    {
        if (led >= end_zone_width && led < rad_game_source.basic_source.n_leds - end_zone_width)
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
    for (int i = 0; i < grad_length; ++i)
    {
        rgb2hsl(rad_game_source.basic_source.gradient.colors[i], &grad_colors[i]);
    }
}


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
    int n = sscanf(buf, "%s %i", keyword, &n_levels);
    if (n != 2) {
        printf("Error reading R&D game config\n");
        exit(-5);
    }
    //todo check that the keyword is "Levels"
    rad_game_levels = malloc(sizeof(struct RadGameLevels) * n_levels);
    for(int level = 0; level < n_levels; ++level)
    {
        skip_comments_in_config(buf, config);
        char fn[64];
        n = sscanf(buf, "%s", fn);
        if (n != 1) { printf("Error reading filename in R&D game config for level %i\n", level); exit(10); }
        int l = strnlen(fn, 64);
        rad_game_levels[level].filename = malloc(6 + l + 1);
        strncpy(rad_game_levels[level].filename, "sound/", 7);
        strncat(rad_game_levels[level].filename, fn, l + 1);
        skip_comments_in_config(buf, config);
        n = sscanf(buf, "%i", &rad_game_levels[level].n_bpms);
        if (n != 1) { printf("Error reading n_bmps in R&D game config for level %i\n", level); exit(10); }
        rad_game_levels[level].bpms = malloc(sizeof(double) * rad_game_levels[level].n_bpms);
        rad_game_levels[level].bpm_switch = malloc(sizeof(long) * rad_game_levels[level].n_bpms);
        for (int bpm = 0; bpm < rad_game_levels[level].n_bpms; bpm++)
        {
            skip_comments_in_config(buf, config);
            n = sscanf(buf, "%lf %li", &rad_game_levels[level].bpms[bpm], &rad_game_levels[level].bpm_switch[bpm]);
            if (n != 2) { printf("Error reading n_bmps in R&D game config for level %i\n", level); exit(10); }
        }
    }
}


void RadGameSource_init(int n_leds, int time_speed, uint64_t current_time)
{
    BasicSource_init(&rad_game_source.basic_source, n_leds, time_speed, source_config.colors[RAD_GAME_SOURCE], current_time);

    rad_game_source.start_time = current_time;
    for(int i = 0; i < 3; ++i)
        oscillators[i] = malloc(sizeof(double) * n_leds);
    RadInputHandler_init();
    rad_game_source.n_players = Controller_get_n_players();
    printf("Players detected: %i\n", rad_game_source.n_players);
    InitOscillators();
    InitPlayers();
    read_rad_game_config();
    current_level = 0;
    start_song_current_level();
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
