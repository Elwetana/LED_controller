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
#include "controller.h"
#include "sound_player.h"
#include "ddr_game.h"
#include "oscillators.h"
#include "nonplaying_states.h"

void Ready_player_hit(int player_index, enum ERAD_COLOURS colour)
{
    (void)player_index;
    (void)colour;
}

void Ready_player_move(int player_index, signed char dir)
{
    (void)player_index;
    (void)dir;
}

void Ready_clear()
{
    GameMode_clear();
    GameMode_lock_current_player();
    GameMode_set_state(0);
}

/*!
 * @brief All ready states have the following structure:
 *  - state 1: Message "Press start to begin"
 *  - state 2: Waiting for players to press Start button
    - state 3: Players can press Start button on their controllers, this will highlight their position and play a message with their number
 *  - state 4: When everyone pressed Start, the message "Get ready. Go" is played
 *  - Ready mode ends and the actual game will start
 * @param ledstrip 
 * @param get_intervals 
 * @return 
*/
int Ready_update_leds(ws2811_t* ledstrip, void (*get_intervals)(int, int*, int*))
{
    static const double effect_freq = 2.0; //Hz
    int cur_player = GameMode_get_current_player();
    int state = GameMode_get_state();
    if (state == 0)
    {
        SoundPlayer_play(SE_PressStart);
        GameMode_set_state(1);
    }
    if (state == 1 || state == 3)       //press start to begin or name of the player is playing
    {
        int n = SoundPlayer_play(SE_N_EFFECTS);
        if (n == -1)
        {
            GameMode_clear_current_player();
            GameMode_set_state(2);
        }
    }
    if(state == 2 && cur_player > -1)   //this is new player highlight
    {
        SoundPlayer_play(SE_Player1 + cur_player);
        //we could start updating leds, but it will wait till next frame
        RadGameSource_set_start();
        GameMode_set_state(3);
    }
    if (state == 3)                     //we have player highlight in progress
    {
        //update leds
        double sinft = fabs(sin(2 * M_PI * effect_freq * RadGameSource_time_from_start_seconds()));
        int colour = multiply_rgb_color(0xFFFFFF, sinft);
        int left_led, right_led;
        get_intervals(cur_player, &left_led, &right_led);
        for (int led = left_led; led <= right_led; ++led)
        {
            ledstrip->channel[0].leds[led] = colour;
        }
    }
    if (state == 2 && cur_player == -1) //we could have all players ready and their effects finished
    {
        int all_ready = GameMode_get_ready_players(); //this is a bit mask, n_players bits must be set
        if (all_ready == ((1 << rad_game_source.n_players) - 1))
        {
            SoundPlayer_play(SE_GetReady);
            GameMode_set_state(4);
        }
    }
    if(state == 4)                      //let's go is playing
    {
        int n = SoundPlayer_play(SE_N_EFFECTS);
        if (n == -1)
        {
            printf("Everyone is ready to play\n");
            RadGameLevel_ready_finished();
        }
    }
    return 1;
}

void RGM_DDR_Ready_clear()
{
    Ready_clear();
}

int RGM_DDR_Ready_update_leds(ws2811_t* ledstrip)
{
    RGM_DDR_render_ready(ledstrip);
    return Ready_update_leds(ledstrip, RGM_DDR_get_ready_interval);
}

void RGM_Osc_Ready_clear()
{
    RGM_Oscillators_clear();
    Ready_clear();
}

int RGM_Oscillators_Ready_update_leds(ws2811_t* ledstrip)
{
    RGM_Oscillators_render_ready(ledstrip);
    return Ready_update_leds(ledstrip, RGM_Oscillators_get_ready_interval);
}


void RGM_Show_Score_clear()
{
    GameMode_clear();
}


static int show_score_on_leds(ws2811_t* ledstrip)
{
    static const double score_speed = 0.5; //LEDs/s
    static const int bullet_colors_offset = 20;
    int right_led = trunc(score_speed * RadGameSource_time_from_start_seconds());

    int short_score = GameMode_get_score() / 100;
    int offset[6];
    offset[5] = right_led;
    int magnitude = 1;
    for (int i = 0; i < 5; ++i)
    {
        offset[4 - i] = offset[5 - i] - (short_score % (magnitude * 10)) / magnitude;
        magnitude *= 10;
    }
    for (int led = 0; led < (offset[0] > 0 ? offset[0] : 0); ++led)
    {
        ledstrip->channel[0].leds[led] = 0x0;
    }
    for (int i = 0; i < 5; ++i)
    {
        int colour = i > 0 ? rad_game_source.basic_source.gradient.colors[bullet_colors_offset + 4 - i] : 0x888888;
        for (int led = offset[i] > 0 ? offset[i] : 0; led < (offset[i + 1] > 0 ? offset[i + 1] : 0); led++)
        {
            led %= rad_game_source.basic_source.n_leds;
            ledstrip->channel[0].leds[led] = colour;
        }
    }
    for (int led = right_led % rad_game_source.basic_source.n_leds; led < rad_game_source.basic_source.n_leds; ++led)
    {
        ledstrip->channel[0].leds[led] = 0x0;
    }
    return right_led;
}

/*!
 * @brief State values
 *  - 0 start
 *  - 1 playing "win/lose"
 *  - 2 playing "code" (only when won)
 *  - 3 waiting for timeout or for everyone to press Start
 * @param ledstrip 
 * @return 
*/
int RGM_Show_Score_update_leds(ws2811_t* ledstrip)
{
    int right_led = show_score_on_leds(ledstrip);
    int n;
    enum ESoundEffects eff;
    switch(GameMode_get_state())
    {
    case 0:
        eff = (enum ESoundEffects)(SE_Lose + GameMode_get_last_result());
        SoundPlayer_play(eff);
        GameMode_set_state(1);
        break;
    case 1:
        n = SoundPlayer_play(SE_N_EFFECTS);
        if (n == -1)
        {
            if (GameMode_get_last_result() == 0)
            {
                GameMode_set_state(3);
            }
            else
            {
                SoundPlayer_start(GameMode_get_code_wav());
                GameMode_set_state(2);
            }
        }
        break;
    case 2:
        n = SoundPlayer_play(SE_N_EFFECTS);
        if (n == -1)
        {
            GameMode_set_state(3);
        }
        break;
    default:
        break;
    }
    GameMode_clear_current_player();
    int all_ready = GameMode_get_ready_players(); //this is a bit mask, n_players bits must be set
    if (all_ready == ((1 << rad_game_source.n_players) - 1) && GameMode_get_state() == 3)
    {
        printf("Everyone has enough watching the score\n");
        RadGameLevel_score_finished();
    }
    if (right_led / rad_game_source.basic_source.n_leds > 2)
    {
        printf("Score was displayed long enough\n");
        RadGameLevel_score_finished();
    }
    return 1;
}
