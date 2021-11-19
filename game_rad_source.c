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
#include "game_rad_source.h"
#include "controller.h"

//#define GAME_DEBUG

//oscillator frequency in hz
#define RAD_FREQ 1.0


//this should go to rad_input_handler when it exists
static void(*button_handlers[2 * C_MAX_XBTN])(int);

//array of oscillators
// first is amplitude, second and third are C and S, such that C*C + S*S == 1
// ociallator equation is y = A * (C * sin(f t) + S * cos(f t))
static double* oscillators[3];

int players[C_MAX_CONTROLLERS];

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
    uint64_t phase_ns = game_rad_source.basic_source.current_time - game_rad_source.start_time;
    double phase_seconds = (phase_ns / (long)1e3) / (double)1e6;
    double impulse_C = cos(M_PI / 2.0 - 2.0 * M_PI * RAD_FREQ * phase_seconds);
    double impulse_S = sin(M_PI / 2.0 - 2.0 * M_PI * RAD_FREQ * phase_seconds);
    int player_pos = players[player_index];
    for (int led = player_pos - 5; led < player_pos + 5; led++)
    {
        double unnormal_C = oscillators[0][led] * oscillators[1][led] + impulse_C * 1.0 / (1 + (led - player_pos) * (led - player_pos));
        double unnormal_S = oscillators[0][led] * oscillators[2][led] + impulse_S * 1.0 / (1 + (led - player_pos) * (led - player_pos));
        double len = sqrt(unnormal_C * unnormal_C + unnormal_S * unnormal_S);
        oscillators[0][led] = len;
        oscillators[1][led] = unnormal_C / len;
        oscillators[2][led] = unnormal_S / len;

        //printf("len %f\n", len);
    }
}

void RadInputHandler_init()
{
    button_handlers[C_MAX_XBTN + DPAD_L] = Player_move_left;
    button_handlers[C_MAX_XBTN + DPAD_R] = Player_move_right;
    button_handlers[C_MAX_XBTN + XBTN_LST_L] = Player_move_left;
    button_handlers[C_MAX_XBTN + XBTN_LST_R] = Player_move_right;
    button_handlers[C_MAX_XBTN + XBTN_A] = Player_strike;

    Controller_init();
}

int RadInputHandler_process_input()
{
    enum EButtons button;
    enum EState state;
    for (int player = 0; player < game_rad_source.n_players; player++)
    {
        int i = Controller_get_button(game_rad_source.basic_source.current_time, &button, &state, player);
        while (i > 0)
        {
#ifdef GAME_DEBUG
            printf("Player: %i, controller button: %s, state: %i\n", player, Controller_get_button_name(button), state);
#endif // GAME_DEBUG
            if (state == BT_down) state = BT_pressed;
            int button_index = state * C_MAX_XBTN + button;
            if (button_handlers[button_index])
            {
                button_handlers[button_index](player);
            }
            i = Controller_get_button(game_rad_source.basic_source.current_time, &button, &state, player);
        }
    }
    return 1;
}

int GameRadSource_update_leds(int frame, ws2811_t* ledstrip)
{
    RadInputHandler_process_input();

    double time_seconds = ((game_rad_source.basic_source.current_time -game_rad_source.start_time)  / (long)1e3) / (double)1e6;
    double sinft = sin(2 * M_PI * RAD_FREQ * time_seconds);
    double cosft = cos(2 * M_PI * RAD_FREQ * time_seconds);
    //printf("Time %f\n", time_seconds);

    for (int led = 0; led < game_rad_source.basic_source.n_leds; ++led)
    {
        double A = oscillators[0][led];
        if (A > 1.0) A = 1.0;
        double y = A * (oscillators[1][led] * sinft + oscillators[2][led] * cosft);
        double absy = (y < 0) ? -y : y;
        int x = (int)(0xFF * absy);
        int color = (y < 0) ? x << 16 : x;
        ledstrip->channel[0].leds[led] = color;
        //if (led == 1) printf("c %x\n", color);
    }
    for (int i = 0; i < game_rad_source.n_players; i++)
    {
        ledstrip->channel[0].leds[players[i]] = 0xFFFFFF;
    }

    return 1;
}

void InitPlayers()
{
    int l = game_rad_source.basic_source.n_leds / game_rad_source.n_players;
    for (int i = 0; i < game_rad_source.n_players; i++)
    {
        players[i] = l / 2 + i * l;
    }
}

void InitOscillators()
{
    for (int led = 0; led < game_rad_source.basic_source.n_leds; ++led)
    {
        if (led > 5 && led < game_rad_source.basic_source.n_leds - 5)
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

void GameRadSource_init(int n_leds, int time_speed, uint64_t current_time)
{
    BasicSource_init(&game_rad_source.basic_source, n_leds, time_speed, source_config.colors[CHASER_SOURCE], current_time); //TODO change colors
    game_rad_source.start_time = current_time;
    for(int i = 0; i < 3; ++i)
        oscillators[i] = malloc(sizeof(double) * n_leds);
    RadInputHandler_init();
    game_rad_source.n_players = Controller_get_n_players();
    InitOscillators();
    InitPlayers();
}

void GameRadSource_destruct()
{
    for (int i = 0; i < 3; ++i)
        free(oscillators[i]);
}

void GameRadSource_construct()
{
    BasicSource_construct(&game_rad_source.basic_source);
    game_rad_source.basic_source.init = GameRadSource_init;
    game_rad_source.basic_source.update = GameRadSource_update_leds;
    game_rad_source.basic_source.destruct = GameRadSource_destruct;
    //game_source.basic_source.process_message = GameSource_process_message;
}

GameRadSource game_rad_source = {
    .basic_source.construct = GameRadSource_construct,
    //.heads = { 19, 246, 0, 38, 76, 114, 152, 190, 227, 265, 303, 341, 379, 417 }
    .first_update = 0,
    .start_time = 0,
    .n_players = 0
};