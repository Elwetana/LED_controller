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
#include "oscillators.h"


/* ***************** Oscillators *************
 * Make the whole chain blink game mode     */

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

static void Oscillators_clear()
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
}

static void Oscillators_init()
{
    for (int i = 0; i < oscillators.grad_length; ++i)
    {
        rgb2hsl(rad_game_source.basic_source.gradient.colors[i], &oscillators.grad_colors[i]);
    }
    Oscillators_clear();
}

static double Oscillators_render_and_count(ws2811_t* ledstrip, double unhide_pattern)
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

    double in_sync = 0.0;
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
                in_sync += 1.0; //TODO depend on sync-ness
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

static struct
{
    RadMovingObject pos[C_MAX_CONTROLLERS]; //!< array of players
    int last_strike_beat[C_MAX_CONTROLLERS];//!< last beat 
    const double player_speed;              //!< player speed in LEDs/s
    const long player_pulse_width;          //!< the length of pulse in ns
    const long long player_period;          //!< how period (in ns) after the player's lead will blink
    const int single_strike_width;
}
osc_players =
{
    .player_speed = 2.5,
    .player_pulse_width = (long)1e8,
    .player_period = (long long)(3e9),
    .single_strike_width = 1
};

//! array of players
//static struct Player players[C_MAX_CONTROLLERS];

void RGM_Oscillators_player_move(int player_index, signed char dir)
{
    if (osc_players.pos[player_index].moving_dir == 0 &&
        osc_players.pos[player_index].position > oscillators.end_zone_width &&
        osc_players.pos[player_index].position < (double)rad_game_source.basic_source.n_leds - oscillators.end_zone_width)
    {
        osc_players.pos[player_index].moving_dir = dir;
        osc_players.pos[player_index].position += dir * 0.0001;
    }
}

/*!
* Player strikes at time t0. 
*
* The rules for processing the hit are following:
*   - we check if the hit is in sync
*       - if not we create an oscillation with equation:
*           y = sin(2 pi f * (t - t0) + pi/2) = sin(2 pi f t + pi/2 - 2 pi f t0) = cos(pi/2 - 2 pi f t0) sin (2 pi f t) + sin(pi/2 - 2 pi f t0) cos (2 pi f t)
*         that will decay quickly
*       - if yes, we create a more permanent oscillation that is in sync (we ignore the inaccuracy) in the leds surronding the player:
*           1 led on each side always
*           2-4 leds on each side when two-four players match the same beat and are within striking distance -- this is transitive, so if the striking distance is D, 
*               the distance between the furthest left and right players will be 3*D
*/
void RGM_Oscillators_player_hit(int player_index, enum ERAD_COLOURS colour)
{
    (void)colour;
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

static void Players_init()
{
    if (rad_game_source.n_players == 0)
    {
        printf("No players detected\n");
        return;
    }
    int l = rad_game_source.basic_source.n_leds / rad_game_source.n_players;
    for (int i = 0; i < rad_game_source.n_players; i++)
    {
        osc_players.pos[i].position = (l / 2) + i * l;
        osc_players.pos[i].moving_dir = 0;
    }
}

static void Players_update()
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
static void Players_render(ws2811_t* ledstrip)
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
    for (int i = 0; i < 3; ++i)
        oscillators.phases[i] = malloc(sizeof(double) * rad_game_source.basic_source.n_leds);
    Oscillators_init();
    Players_init();
}

void RGM_Oscillators_clear()
{
    Oscillators_clear();
    Players_init();
}

int RGM_Oscillators_update_leds(ws2811_t* ledstrip)
{
    static double completed = 0;
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
    double in_sync = Oscillators_render_and_count(ledstrip, completed);
    completed = (in_sync - 2.0 * oscillators.end_zone_width) / (rad_game_source.basic_source.n_leds - 2.0 * oscillators.end_zone_width);
    if (rad_game_source.cur_frame % 500 == 0) //TODO if in_sync = n_leds, players win
    {
        printf("Leds in sync: %f\n", in_sync);
    }
    Players_update();
    Players_render(ledstrip);
    oscillators.new_effect = SE_N_EFFECTS;

    return 1;
}

void RGM_Oscillators_destruct()
{
    for (int i = 0; i < 3; ++i)
    {
        free(oscillators.phases[i]);
    }
}

void RGM_Oscillators_render_ready(ws2811_t* ledstrip)
{
    for (int led = 0; led < rad_game_source.basic_source.n_leds; ++led)
    {
        int colour = 0x0;
        if (led < oscillators.end_zone_width || led >= rad_game_source.basic_source.n_leds - oscillators.end_zone_width)
        {
            colour = hsl2rgb(&oscillators.grad_colors[0]);
        }
        ledstrip->channel[0].leds[led] = colour;
    }
    Players_update();
    Players_render(ledstrip);
}

void RGM_Oscillators_get_ready_interval(int player_index, int* left_led, int* right_led)
{
    *left_led = round(osc_players.pos[player_index].position) - 3;
    *right_led = round(osc_players.pos[player_index].position) + 3;
}
