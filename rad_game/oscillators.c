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

//#define OSC_UNIT_TESTS

/* ***************** Oscillators *************
 * Make the whole chain blink game mode     */

static struct
{
    //! array of oscillators
    // first is amplitude, second and third are C and S, such that C*C + S*S == 1
    // ociallator equation is y = A * (C * sin(f t) + S * cos(f t))
    double* phases[3];
    long cur_beat;
    int led0_color_index;
    double points; //actual points are 9'999'900 * points / n_beats
    enum ESoundEffects new_effect;  //!< should we start playing a new effect?
    double decay;
    const double S_coeff_threshold;
    const double out_of_sync_decay; //!< how much will out of sync oscillator decay per each beat
    const int grad_length;
    //hsl_t* grad_colors;
    const double grad_speed; //leds per beat
    const int end_zone_width;
    const int beats_to_halve; //how many beats it takes for in-sync oscillator to decay to half amplitude
    const enum ERAD_COLOURS grad_steps[5];
} oscillators =
{
    .S_coeff_threshold = 0.1,
    .out_of_sync_decay = 0.66,
    .grad_length = 20,
    .grad_speed = 0.1,
    .end_zone_width = 10,
    .beats_to_halve = 10,
    .grad_steps = { DC_RED, DC_GREEN, DC_BLUE, DC_YELLOW, DC_RED }
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
            oscillators.phases[0][led] = 2.0;
            oscillators.phases[1][led] = 1.0;
            oscillators.phases[2][led] = 0.0;
        }
    }
    oscillators.new_effect = SE_N_EFFECTS;
    oscillators.cur_beat = -1;
    oscillators.points = 0;
    oscillators.led0_color_index = 0;
}

static void Oscillators_init()
{
    /*oscillators.grad_colors = malloc(sizeof(hsl_t) * oscillators.grad_length);
    for (int i = 0; i < oscillators.grad_length; ++i)
    {
        rgb2hsl(rad_game_source.basic_source.gradient.colors[i], &oscillators.grad_colors[i]);
    }*/
    Oscillators_clear();
    double ln05 = -0.6931471805599453; //ln(0.5)
    oscillators.decay = exp(ln05 / oscillators.beats_to_halve);
    printf("Decay %f\n", oscillators.decay);
}

static int Oscillators_update_and_count()
{
    long beat = (long)(rad_game_songs.freq * RadGameSource_time_from_start_seconds());
    
    int in_sync = -1;
    if (oscillators.cur_beat < beat)
    {
        in_sync = 0;
        for (int o = oscillators.end_zone_width; o < rad_game_source.basic_source.n_leds - 1 - oscillators.end_zone_width; ++o)
        {
            double A = oscillators.phases[0][o];
            double S = oscillators.phases[2][o];

            if (A > 1 && S == 0) in_sync++;
            oscillators.phases[0][o] *= (S == 0) ? oscillators.decay : oscillators.out_of_sync_decay;
            if (A < oscillators.S_coeff_threshold)
            {
                oscillators.phases[0][o] = 0.0;
                oscillators.phases[1][o] = 0.0;
                oscillators.phases[2][o] = 0.0;
            }
        }
        oscillators.cur_beat = beat;
        //printf("\nnew beat %li\n", beat);
    }
    return in_sync;
}

/*!
 * @brief 
 *   When do_gradient is true, the target is to get this picture:
 *      There is symetric gradient over the interval 2 * grad_length (let's call grad_length L)
 *          e.g. for L = 5:  A B C D E  E D C B A  
 *                           B C D E D  D E D C B
 *                           C D E D C  C D E D C
 *                           D E D C B
 * `                         and so on.
 * 
 *      Our gradient is L colours long, but usually we display only some subset of it, let'
 *      call this window W.
 *          e.g for W = 3:  A B C B A
 *                          B C A B C
 *                          C B A B C
 * @param led 
 * @param grad_shift 
 * @return 
*/
static int get_grad_color_index(int led, int grad_shift, int window_width)
{
    if (window_width == 1) //there is nothing to compute, with this window width we always see the first colour of gradient
        return 0;
    int l = (led) % (2 * oscillators.grad_length);
    l = (l < oscillators.grad_length) ? l : 2 * oscillators.grad_length - 1 - l;    //if L = 20 and l = 20, new l will be 19
    //now l is position within the L segment

    //for gradient insed L we want smooth blend, not doubling at the mirror point
    int w = (l + grad_shift) % (2 * window_width - 2);
    w = (w < window_width) ? w : 2 * window_width - 2 - w;      //if W = 5 we want 0 1 2 3 4 3 2 1; for w = 7 new w is 1
    return w;
}

static void Oscillators_render(ws2811_t* ledstrip, double unhide_pattern, int do_gradient)
{
    double time_seconds = RadGameSource_time_from_start_seconds();
    double sinft = sin(2 * M_PI * rad_game_songs.freq * time_seconds);
    double cosft = cos(2 * M_PI * rad_game_songs.freq * time_seconds);
    //printf("Time %f\n", time_seconds);

    int pattern_length =  (int)((double)oscillators.grad_length * unhide_pattern);
    if (pattern_length < 1) pattern_length = 1;
    double grad_shift = fmod(oscillators.cur_beat * oscillators.grad_speed, pattern_length); // from 0 to pattern_length-1

    oscillators.led0_color_index = get_grad_color_index(0, grad_shift, pattern_length);
    for (int led = 0; led < rad_game_source.basic_source.n_leds; ++led)
    {
        double A = oscillators.phases[0][led];
        double C = oscillators.phases[1][led];
        double S = oscillators.phases[2][led];
        double y = (C * sinft + S * cosft);
        if (y < 0)
        {
            y = 0; //negative half-wave will not be shown at all
        }
        y *= A / 2;
        int grad_pos = do_gradient ? get_grad_color_index(led, grad_shift, pattern_length) : oscillators.led0_color_index;
        ledstrip->channel[0].leds[led] = multiply_rgb_color_ratchet(rad_game_source.basic_source.gradient.colors[grad_pos], y);
    }
    //printf("\n");
}

static struct
{
    RadMovingObject pos[C_MAX_CONTROLLERS]; //!< array of players
    int last_strike_beat[C_MAX_CONTROLLERS];//!< last beat 
    const double player_speed;              //!< player speed in LEDs/s
    const long player_pulse_width;          //!< the length of pulse in ns
    const long long player_period;          //!< how period (in ns) after the player's lead will blink
    const int single_strike_width;
    const int resonance_distance;
}
osc_players =
{
    .player_speed = 2.5,
    .player_pulse_width = (long)1e8,
    .player_period = (long long)(3e9),
    .single_strike_width = 1,
    .resonance_distance = 9
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

int compare_player_pos(const void* p1, const void* p2)
{
    double pos1 = osc_players.pos[*(int*)p1].position;
    double pos2 = osc_players.pos[*(int*)p2].position;
    if (pos1 > pos2) return  1;
    if (pos1 < pos2) return -1;
    return 0;
}

void fill_affected_leds(int* same_beat_n, int* same_beat_players, int* affected_leds, int player_pos)
{
    qsort(same_beat_players, *same_beat_n, sizeof(int), compare_player_pos);
    //check if the players are within the striking distance of each other
    int pp = 0;
    while (pp < *same_beat_n && player_pos > round(osc_players.pos[same_beat_players[pp]].position)) pp++;
    //pp is now equal to the index of the first player that is to the right of us or equal to same_beat_n

    //now we have to check the players to the right of us and left of us if they are within resonance distance.
    //this is transitive, so if player immediately to the right is within resonance distance from, the next must
    //be withing distance from that player and so on
    
    //first right side
    int rp = pp;
    int rpos = player_pos;
    while ((rp < *same_beat_n) && (round(osc_players.pos[same_beat_players[rp]].position) - rpos < osc_players.resonance_distance))
    {
        rpos = round(osc_players.pos[same_beat_players[rp]].position);
        rp++;
    }
    //now rp is either equal to same_beat_n (all players on the right are withing distance), or it is smaller and then we can safely ignore them
    *same_beat_n = rp;
    
    //and now check the players to the left of the player
    int lp = pp - 1;
    int lpos = player_pos;
    while ((lp >= 0) && (lpos - round(osc_players.pos[same_beat_players[lp]].position) < osc_players.resonance_distance))
    {
        lpos = round(osc_players.pos[same_beat_players[lp]].position);
        lp--;
    }
    //now lp is either -1 or we need to move the whole same_beat_players array to the left by the amount (lp + 1)
    if (lp > -1) //this condition is strictly speaking not necessarily, array would not change if we remove it
    {
        for (int i = 0; i < *same_beat_n - lp - 1; ++i)
        {
            same_beat_players[i] = same_beat_players[i + lp + 1];
        }
        *same_beat_n -= (lp + 1);
    }
    //end of range checks

    //now we can fill the affected leds for all players that are left
    for (int sbp = 0; sbp < *same_beat_n; sbp++)
    {
        int p = same_beat_players[sbp]; //the player
        int p_pos = round(osc_players.pos[p].position);
        int left_led = p_pos - *same_beat_n * osc_players.single_strike_width;
        int affected_led_index = 0;
        while (affected_leds[affected_led_index] != 0 && affected_leds[affected_led_index] < left_led) affected_led_index++;
        for (int i = 0; i < 2 * *same_beat_n * osc_players.single_strike_width + 1; ++i)
        {
            int led = left_led + i;
            if (led >= oscillators.end_zone_width && led < rad_game_source.basic_source.n_leds - oscillators.end_zone_width)
            {
                affected_leds[affected_led_index++] = led;
            }
        }
    }
}

static int is_colour_match(enum ERAD_COLOURS colour)
{
    int is_match = 0;
    //the gradient is R - G - B - Y - R, distance is always 5, i.e. oscillators.grad_length / 4
    int grad_steps_count = sizeof(oscillators.grad_steps) / sizeof(enum ERAD_COLOURS);
    assert(grad_steps_count == 5);

    int step_length = oscillators.grad_length / (grad_steps_count - 1);
    int cur_colour = oscillators.led0_color_index / step_length; //oscillators.grad_steps[cur_colour] is the beginning of current gradient step
    int next_colour = (cur_colour + 1) % grad_steps_count;
    int cur_colour_blend = oscillators.led0_color_index % step_length;
    //if current blend is close to the start or end, we only allow one colour. Otherwise we allow both from the blend
    if (cur_colour_blend < 2)
    {
        is_match = (colour == oscillators.grad_steps[cur_colour]);
    }
    else if (cur_colour_blend > step_length - 2)
    {
        is_match = (colour == oscillators.grad_steps[next_colour]);
    }
    else
    {
        is_match = (colour == oscillators.grad_steps[cur_colour]) || (colour == oscillators.grad_steps[next_colour]);
    }
    return is_match;
}

#ifdef OSC_UNIT_TESTS

static int OscPlayers_run_one_test(int same_beat_n, int* same_beat_players, int* affected_leds, int player_pos, int* expected, int test_id)
{
    for (int i = 0; i < C_MAX_CONTROLLERS * (2 * C_MAX_CONTROLLERS + 1) + 1; ++i) affected_leds[i] = 0;
    fill_affected_leds(&same_beat_n, same_beat_players, affected_leds, player_pos);
    for (int i = 0; i < C_MAX_CONTROLLERS * (2 * C_MAX_CONTROLLERS + 1) + 1; ++i)
    {
        if (affected_leds[i] != expected[i])
        {
            printf("!!! ERROR !!! test %i failed\n", test_id);
            return 0;
        }
    }
    printf("test %i passed\n", test_id);
    return 1;
}


static int OscPlayers_unit_tests()
{
    int affected_leds[C_MAX_CONTROLLERS * (2 * C_MAX_CONTROLLERS + 1) + 1] = { 0 }; //(2 * C_MAX_CONTROLLERS + 1) is the maximum width affected by one player, assuming single_strike_width == 1
    int expected[C_MAX_CONTROLLERS * (2 * C_MAX_CONTROLLERS + 1) + 1] = { 0 };

    //test 01 -- empty -- expected results: all zeroes
    for (int i = 0; i < C_MAX_CONTROLLERS * (2 * C_MAX_CONTROLLERS + 1) + 1; ++i) expected[i] = 0;
    OscPlayers_run_one_test(0, (int[4]) { 0, 0, 0, 0 }, affected_leds, 100, expected, 1);

    //test 02 -- single -- expected results: 3 leds
    osc_players.pos[0].position = 50;
    for (int i = 0; i < C_MAX_CONTROLLERS * (2 * C_MAX_CONTROLLERS + 1) + 1; ++i) expected[i] = 0;
    for (int i = 0; i < 3; ++i) expected[i] = 49 + i;
    OscPlayers_run_one_test(1, (int[4]){0, 0, 0, 0}, affected_leds, 55, expected, 2);

    //test 03 -- two close -- expected results: 10 leds
    osc_players.pos[0].position = 50;
    osc_players.pos[1].position = 57;
    for (int i = 0; i < C_MAX_CONTROLLERS * (2 * C_MAX_CONTROLLERS + 1) + 1; ++i) expected[i] = 0;
    for (int i = 0; i < 5; ++i) expected[i] = 48 + i;
    for (int i = 0; i < 5; ++i) expected[i + 5] = 55 + i;
    OscPlayers_run_one_test(2, (int[4]) { 1, 0, 0, 0 }, affected_leds, 60, expected, 3);

    //test 04 -- two apart -- expected results: 3 leds
    osc_players.pos[0].position = 50;
    osc_players.pos[1].position = 80;
    for (int i = 0; i < C_MAX_CONTROLLERS * (2 * C_MAX_CONTROLLERS + 1) + 1; ++i) expected[i] = 0;
    for (int i = 0; i < 3; ++i) expected[i] = 79 + i;
    OscPlayers_run_one_test(2, (int[4]) { 0, 1, 0, 0 }, affected_leds, 75, expected, 4);

    //test 05 -- two close, one apart -- expected results: 10 leds
    osc_players.pos[0].position = 80;
    osc_players.pos[1].position = 50;
    osc_players.pos[3].position = 57;
    for (int i = 0; i < C_MAX_CONTROLLERS * (2 * C_MAX_CONTROLLERS + 1) + 1; ++i) expected[i] = 0;
    for (int i = 0; i < 5; ++i) expected[i] = 48 + i;
    for (int i = 0; i < 5; ++i) expected[i + 5] = 55 + i;
    OscPlayers_run_one_test(3, (int[4]) { 1, 0, 3, 0 }, affected_leds, 53, expected, 5);

    //test 06 -- two overlapping -- expected results: 8 leds
    osc_players.pos[0].position = 50;
    osc_players.pos[1].position = 53;
    for (int i = 0; i < C_MAX_CONTROLLERS * (2 * C_MAX_CONTROLLERS + 1) + 1; ++i) expected[i] = 0;
    for (int i = 0; i < 5; ++i) expected[i] = 48 + i;
    for (int i = 0; i < 5; ++i) expected[i + 3] = 51 + i;
    OscPlayers_run_one_test(2, (int[4]) { 0, 1, 0, 0 }, affected_leds, 55, expected, 6);

    //test 07 -- player included
    osc_players.pos[0].position = 46;
    osc_players.pos[1].position = 50;
    osc_players.pos[2].position = 57;
    for (int i = 0; i < C_MAX_CONTROLLERS * (2 * C_MAX_CONTROLLERS + 1) + 1; ++i) expected[i] = 0;
    for (int i = 0; i < 7; ++i) expected[i] = 43 + i;
    for (int i = 0; i < 7; ++i) expected[i + 4] = 47 + i;
    for (int i = 0; i < 7; ++i) expected[i + 11] = 54 + i;
    OscPlayers_run_one_test(3, (int[4]) { 1, 2, 0, 0 }, affected_leds, 50, expected, 7);

    //test get_grad_index
    for (int shift = 0; shift < 20; ++shift)
    {
        for (int led = 0; led < 40; ++led)
        {
            int gi = get_grad_color_index(led, shift, 4);
            printf("%i ", gi);
            if (led == 19) printf("  ");
        }
        printf("   %i\n", shift);
    }



    return 1;
}
#endif // OSC_UNIT_TESTS


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
*   - when we add new amplitude, we use the following formula:
*           A_1 = (A_0 < 1) ? A_n : A_n * A_0
*     A_n presents certain amount of beats by which we want to extend oscillator's life (if A_n = 2, we ar giving it beats_to_halve beats). If the current amplitude is 
*     small, we shall replace it, otherwise we multiply it
*/
void RGM_Oscillators_player_hit(int player_index, enum ERAD_COLOURS colour)
{
    int affected_leds[C_MAX_CONTROLLERS * (2 * C_MAX_CONTROLLERS + 1) + 1] = { 0 }; //(2 * C_MAX_CONTROLLERS + 1) is the maximum width affected by one player, assuming single_strike_width == 1
    int same_beat_players[C_MAX_CONTROLLERS] = { -1 };
    double hit_boost[] = { 1, 2, 3, 4, 5 }; //there must be C_MAX_CONTROLLERS + 1 numbers here
    double phase_seconds = RadGameSource_time_from_start_seconds();
    double impulse_C = cos(M_PI / 2.0 - 2.0 * M_PI * rad_game_songs.freq * phase_seconds);
    double impulse_S = sin(M_PI / 2.0 - 2.0 * M_PI * rad_game_songs.freq * phase_seconds);
    int player_pos = round(osc_players.pos[player_index].position);

    //check if we matched the beat, distance and colour
    if (is_colour_match(colour) && impulse_S * impulse_S < oscillators.S_coeff_threshold) //good strike
    {
        //we need to find how many players matched this beat and update their leds
        int same_beat = 0;
        for (int p = 0; p < rad_game_source.n_players; ++p)
        {
            if (osc_players.last_strike_beat[p] == oscillators.cur_beat && p != player_index)
            {
                same_beat_players[same_beat++] = p;
            }
        }
        //printf("same beat %i  %i %i %i\n", same_beat, osc_players.last_strike_beat[0], osc_players.last_strike_beat[1], oscillators.cur_beat);
        
        //now we get the leds affected by the previous strike
        fill_affected_leds(&same_beat, same_beat_players, affected_leds, player_pos);
        double old_boost = hit_boost[same_beat];
        int al = 0;
        while (affected_leds[al] > 0)
        {
            int led = affected_leds[al++];
            oscillators.phases[0][led] /= old_boost;
        }
        
        //add ourselves and get new affected leds and apply new boost to them
        same_beat_players[same_beat++] = player_index;
        fill_affected_leds(&same_beat, same_beat_players, affected_leds, player_pos);
        double new_boost = hit_boost[same_beat + 1];
        al = 0;
        while (affected_leds[al] > 0)
        {
            int led = affected_leds[al++];
            double A = oscillators.phases[0][led];
            oscillators.phases[0][led] = (A < 1) ? new_boost : A * new_boost;
            oscillators.phases[1][led] = 1.0;
            oscillators.phases[2][led] = 0.0;
        }
        if (same_beat > 1)
        {
            oscillators.new_effect = (enum ESoundEffects)(SE_Reward01 + same_beat - 1);
        }
        osc_players.last_strike_beat[player_index] = oscillators.cur_beat;
    }
    else //bad strike
    {
        int player_pos = round(osc_players.pos[player_index].position);
        for (int led = player_pos - osc_players.single_strike_width; led < player_pos + osc_players.single_strike_width + 1; led++)
        {
            double unnormal_C = oscillators.phases[0][led] * oscillators.phases[1][led] + impulse_C;
            double unnormal_S = oscillators.phases[0][led] * oscillators.phases[2][led] + impulse_S;
            double len = sqrt(unnormal_C * unnormal_C + unnormal_S * unnormal_S);
            oscillators.phases[0][led] = len;
            oscillators.phases[1][led] = unnormal_C / len;
            oscillators.phases[2][led] = unnormal_S / len;
        }
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
#ifdef OSC_UNIT_TESTS
    OscPlayers_unit_tests();
#endif // OSC_UNIT_TESTS
}

/*!
 * @brief Among other things, calculate score. The idea is that every song is worth 9 999 900 points that would be won if 
 * every led was in sync every beat. Therefore:
 * 
 *  points = 9'999'900 * sum(in_sync) / (n_beats * n_leds) = 9'999'900 * sum(in_sync / n_leds) / n_beats
 * 
 * @param ledstrip 
 * @return 
*/
int RGM_Oscillators_update_leds(ws2811_t* ledstrip)
{
    static double completed = 0;
    long time_pos = SoundPlayer_play(oscillators.new_effect);
    if (time_pos == -1)
    {
        long score = 9999900 * oscillators.points / oscillators.cur_beat;
        RadGameLevel_level_finished(score);
    }
    //todo else -- check if bpm freq had not changed
    double in_sync = Oscillators_update_and_count();
    if (in_sync > -1)
    {
        completed = in_sync / (rad_game_source.basic_source.n_leds - 2.0 * oscillators.end_zone_width);
        oscillators.points += completed;
        if (oscillators.cur_beat % 20 == 0) 
        {
            printf("Leds in sync: %f\n", in_sync);
        }
    }
    Oscillators_render(ledstrip, completed, 0);

    Players_update();
    Players_render(ledstrip);
    oscillators.new_effect = SE_N_EFFECTS;

    return 1;
}

void RGM_Oscillators_destruct()
{
    //free(oscillators.grad_colors);
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
            colour = rad_game_source.basic_source.gradient.colors[0];
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


/*!
 * @brief This could/should be a different file, but it shares a lot of functionality with the oscillators gameplay
 *  When players win the game, the game enters this state that cannot be exited (you can switch to another level via http server)
 *  There are internal states:
 *      - 1: play "You Won"
 *      - 2: playing secret message
 *      - 3: playing random song and blinking the leds
 * @param ledstrip 
*/

static void GameWon_clear_leds()
{
    for (int led = 0; led < rad_game_source.basic_source.n_leds; ++led)
    {
        oscillators.phases[0][led] = 2.0;
        oscillators.phases[1][led] = 1.0;
        oscillators.phases[2][led] = 0.0;
    }
    oscillators.cur_beat = -1;
}

static void GameWon_set_colour(ws2811_t* ledstrip, ws2811_led_t colour)
{
    for (int led = 0; led < rad_game_source.basic_source.n_leds; ++led)
    {
        ledstrip->channel[0].leds[led] = colour;
    }
}

void RGM_GameWon_clear()
{
    GameWon_clear_leds();
    Players_init();
    GameMode_set_state(1);
    SoundPlayer_play(SE_WinGame);
}

void RGM_GameWon_update_leds(ws2811_t* ledstrip)
{
    const double blink_fq = 2; //Hz
    int st = GameMode_get_state();
    long t = SoundPlayer_play(SE_N_EFFECTS);
    double sinft = fabs(cos(2 * M_PI * blink_fq * RadGameSource_time_from_start_seconds()));
    if (t != -1) //stil playing
    {
        switch (st)
        {
        case 1: //you win
            GameWon_set_colour(ledstrip, multiply_rgb_color(0xFFFFFF, sinft));
            break;
        case 2: //secret message
            GameWon_set_colour(ledstrip, 0x0);
            break;
        case 3: //random song
            oscillators.cur_beat = (long)(rad_game_songs.freq * RadGameSource_time_from_start_seconds());
            Oscillators_render(ledstrip, 1, 1);
            break;
        default:
            break;
        }
    }
    if (t == -1) //playing ended, switch to new state
    {
        switch (st)
        {
        case 1: //start secret message
        case 3:
            SoundPlayer_start("sound/secret_message.wav");
            GameMode_set_state(2);
            break;
        case 2: //start random song
            RadGameSong_start_random();
            GameMode_set_state(3);
            break;
        }
    }
}