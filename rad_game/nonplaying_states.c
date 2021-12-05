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

int Ready_update_leds(ws2811_t* ledstrip, void (*get_intervals)(int, int*, int*))
{
    static const double effect_freq = 2.0; //Hz
    int cur_player = GameMode_get_current_player();
    uint64_t start_time = GameMode_get_effect_start();
    if (start_time == 0 && cur_player != 0xf)
    {
        //we have to start a new effect
        GameMode_set_effect_start(rad_game_source.basic_source.current_time);
        SoundPlayer_play(C_PlayerOneIndex + cur_player);
        //we could start updatint leds, but it will wait till next frame
    }
    if (start_time > 0 && cur_player != 0xf)
    {
        //we have effect in progress
        int n = SoundPlayer_play(SE_N_EFFECTS);
        if (n == -1)
        {
            //we are finished
            GameMode_clear_current_player();
        }
        double time_delta = (rad_game_source.basic_source.current_time - start_time) / (long)1e3 / (double)(1e6);
        double sinft = fabs(sin(2 * M_PI * effect_freq * time_delta));
        int colour = multiply_rgb_color(0xFFFFFF, sinft);
        int left_led, right_led;
        get_intervals(cur_player, &left_led, &right_led);
        for (int led = left_led; led <= right_led; ++led)
        {
            ledstrip->channel[0].leds[led] = colour;
        }
    }
    if (start_time == 0 && cur_player == 0xf)
    {
        //we could have all players ready and their effect finished
        int all_ready = GameMode_get_ready_players(); //this is a bit mask, n_players bits must be set
        if (all_ready == ((1 << rad_game_source.n_players) - 1))
        {
            printf("Everyone is ready to play DDR mode\n");
        }
    }
    return 1;
}



