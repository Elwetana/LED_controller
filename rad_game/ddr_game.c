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


#define C_MAX_DDR_BULLETS 32

enum EDDR_HIT_INTERVAL
{
    DHI_MISS,
    DHI_GOOD,
    DHI_GREAT,
    DHI_PERFECT,
    DHI_N_COUNT
};

struct DdrEmitor
{
    int emitor_pos; //!< start of the emitor
    int player_pos; //!< player position, this does not change
    int reward_pos; //!< reward strip start, this position also does not changes
    double reaction_progress;
    enum EDDR_HIT_INTERVAL reaction;
    RadMovingObject bullets[C_MAX_DDR_BULLETS];
    int n_bullets;
    int furthest_bullet; //!< index of the most distant bullet, usually 0
    int points;
    int streak;
};

static struct
{
    struct DdrEmitor data[C_MAX_CONTROLLERS];
    int last_beat;
    const int reaction_len;   //how many leds will be used to show reactions and streak
    const int reaction_ratio; //part of the beat that will be reward
    //const int player_col_index;
    const double hit_intervals[DHI_N_COUNT]; //!< if player is off by more than intervals[DHI_MISS] part of beat, it's a miss
    hsl_t grad_colors[19];
    const int bullet_colors_offset;
    const int grad_colors_offset;
    const int grad_length;
    const int reaction_colors_offset;
    const int streak_grad_offset;
    const int streak_grad_len;
    const double points_to_size_amp; //see get_emitor_length for usage
    const int points_per_color; //see fire bullet for usage
    const int beats_to_target;
}
ddr_emitors =
{
    .last_beat = 0,
    .reaction_len = 8,
    .reaction_ratio = 4,
    //.player_col_index = 19,
    .hit_intervals = {0.5, 0.25, 0.1, 0.0}, // (oo, 0.5> -- miss, (0.5, 0.25> -- good, (0.25, 0.1> -- great, (0.1, 0> -- perfect
    .bullet_colors_offset = 19,
    .grad_colors_offset = 23,
    .grad_length = 10,
    .reaction_colors_offset = 33,
    .streak_grad_offset = 37,
    .streak_grad_len = 6,
    .points_to_size_amp = 0.25,
    .points_per_color = 100000,
    .beats_to_target = 24
};

void RGM_DDR_clear()
{
    for (int em = 0; em < rad_game_source.n_players; ++em)
    {
        ddr_emitors.data[em].n_bullets = 0;
        ddr_emitors.data[em].reaction_progress = 0.0;
        ddr_emitors.data[em].points = 1;
        ddr_emitors.data[em].streak = 0;
    }
}

void RGM_DDR_init()
{
    srand(100);
    int field_len = rad_game_source.basic_source.n_leds / rad_game_source.n_players; //for 200 leds and 3 player = 66
    int offset = (rad_game_source.basic_source.n_leds % field_len) / 2;
    for (int em = 0; em < rad_game_source.n_players; ++em)
    {
        ddr_emitors.data[em].emitor_pos = offset + em * field_len;
        ddr_emitors.data[em].player_pos = offset + (em + 1) * field_len - ddr_emitors.reaction_len - 2;
        ddr_emitors.data[em].reward_pos = offset + (em + 1) * field_len - ddr_emitors.reaction_len;
    }
    for (int i = 0; i < ddr_emitors.grad_length; ++i)
    {
        rgb2hsl(rad_game_source.basic_source.gradient.colors[ddr_emitors.grad_colors_offset + i], &ddr_emitors.grad_colors[i]);
    }
    RGM_DDR_clear();
}

/*!
 * @brief Does three things:
 *  -- update streak
 *  -- update score
 *  -- starts reaction
 */
static void DdrPlayer_action(int player_index, enum EDDR_HIT_INTERVAL hit, enum ERAD_COLOURS colour)
{
    //update streak
    if (hit < DHI_GREAT)
    {
        ddr_emitors.data[player_index].streak = 0;
    }
    else
    {
        ddr_emitors.data[player_index].streak++;
    }
    //update score
    /*
    The scoring system for DDR versions 1 and 2 (including the Plus remixes) is as follows: For every step:
    Multiplier (M) = (# of steps in your current combo / 4) rounded down "Good" step = M * 100 (and this ends your combo) "Great" step = M * M * 100 "Perfect" step = M * M * 300
    e.g. When you get a 259 combo, the 260th step will earn you:
    M = (260 / 4) rounded down = 65 Great step = M x M X 100 = 65 x 65 x 100 = 422,500 Perfect step = Great step score x 3 = 422,500 x 3 = 1,267,500
    https://remywiki.com/DanceDanceRevolution_Scoring_System
    */
    int M = (ddr_emitors.data[player_index].streak / 4) + 1;
    int points_earned = 0;
    int bullet_value = (int[]){ 100, 1000, 10000, 100000 } [colour] ;
    switch (hit)
    {
    case DHI_MISS:
        points_earned = -bullet_value / 10;
        break;
    case DHI_GOOD:
        points_earned = M * bullet_value;
        break;
    case DHI_GREAT:
        points_earned = M * M * bullet_value;
        break;
    case DHI_PERFECT:
        points_earned = M * M * 3 * bullet_value;
        break;
    case DHI_N_COUNT:
        break;
    }
    ddr_emitors.data[player_index].points += points_earned;
    if (ddr_emitors.data[player_index].points < 1) ddr_emitors.data[player_index].points = 1;
    //start reaction
    double reaction_len = 1000 / rad_game_songs.freq / ddr_emitors.reaction_ratio; //reaction length in ms
    ddr_emitors.data[player_index].reaction_progress = reaction_len;
    ddr_emitors.data[player_index].reaction = hit;
    if (hit != 0)
        printf("Player %i made hit %i, score %i, streak %i\n", player_index, hit, ddr_emitors.data[player_index].points, ddr_emitors.data[player_index].streak);
}

static void DdrEmitors_delete_bullet(int player_index, int bullet_index)
{
    ddr_emitors.data[player_index].n_bullets--;
    for (int b = bullet_index; b < ddr_emitors.data[player_index].n_bullets; ++b)
    {
        ddr_emitors.data[player_index].bullets[b] = ddr_emitors.data[player_index].bullets[b + 1];
    }
}

/*!
 * @brief will fire a bullet for all players, all of them with the same color
 * @param beats how many beats to reach the player
*/
static void DdrEmitors_fire_bullet(int beats)
{
    int total_points = 0;
    for (int p = 0; p < rad_game_source.n_players; ++p)
    {
        total_points += ddr_emitors.data[p].points;
    }
    int max_colors = 1 + total_points / ddr_emitors.points_per_color;
    if (max_colors > 4) max_colors = 4;
    int col = (int)(random_01() * max_colors); //enum ERAD_COLOURS
    for (int p = 0; p < rad_game_source.n_players; ++p)
    {
        if (ddr_emitors.data[p].n_bullets < C_MAX_DDR_BULLETS)
        {
            int emitor_len = 6;
            int dist = ddr_emitors.data[p].player_pos - ddr_emitors.data[p].emitor_pos - emitor_len - 1;
            double time_to_reach = (double)beats / rad_game_songs.freq;
            ddr_emitors.data[p].bullets[ddr_emitors.data[p].n_bullets].position = (double)ddr_emitors.data[p].emitor_pos + emitor_len + 1;
            ddr_emitors.data[p].bullets[ddr_emitors.data[p].n_bullets].moving_dir = +1;
            ddr_emitors.data[p].bullets[ddr_emitors.data[p].n_bullets].custom_data = col;
            ddr_emitors.data[p].bullets[ddr_emitors.data[p].n_bullets].speed = (double)dist / time_to_reach;
            ddr_emitors.data[p].n_bullets++;
        }
    }
}

static void DdrEmitors_update_bullets()
{
    double time_elapsed = ((rad_game_source.basic_source.time_delta / (long)1e3) / (double)1e6);
    for (int p = 0; p < rad_game_source.n_players; ++p)
    {
        double furthest_pos = 0;
        int b = 0;
        while (b < ddr_emitors.data[p].n_bullets)
        {
            double distance_moved = time_elapsed * ddr_emitors.data[p].bullets[b].speed;
            double miss_distance = ddr_emitors.data[p].bullets[b].speed / rad_game_songs.freq * ddr_emitors.hit_intervals[DHI_MISS];
            if ((ddr_emitors.data[p].bullets[b].position + distance_moved) > (ddr_emitors.data[p].player_pos + miss_distance))
            {
                DdrPlayer_action(p, DHI_MISS, ddr_emitors.data[p].bullets[b].custom_data);
                DdrEmitors_delete_bullet(p, b); //this will decrease n_bullets
            }
            else
            {
                ddr_emitors.data[p].bullets[b].position += distance_moved;
                if (ddr_emitors.data[p].bullets[b].position > furthest_pos)
                {
                    furthest_pos = ddr_emitors.data[p].bullets[b].position;
                    ddr_emitors.data[p].furthest_bullet = b;
                }
                b++;
            }
        }
    }
}

static void DdrEmitors_update()
{
    DdrEmitors_update_bullets();
    //check whether we want to emit a new bullet
    double time_seconds = ((rad_game_source.basic_source.current_time - rad_game_source.start_time) / (long)1e3) / (double)1e6;
    double beat = time_seconds * rad_game_songs.freq;
    //todo -- emit bullets in other phases of the beat?
    if ((int)beat > ddr_emitors.last_beat)
    {
        ddr_emitors.last_beat = (int)beat;
        if (random_01() > 0.1f)
        {
            DdrEmitors_fire_bullet(ddr_emitors.beats_to_target);
        }
    }
    //check reactions
    double delta_ms = (rad_game_source.basic_source.time_delta / 1e6);
    for (int p = 0; p < rad_game_source.n_players; ++p)
    {
        if (ddr_emitors.data[p].reaction_progress > 0)
            ddr_emitors.data[p].reaction_progress -= delta_ms;
        if (ddr_emitors.data[p].reaction_progress < 0)
            ddr_emitors.data[p].reaction_progress = 0;
    }
}

void RGM_DDR_player_hit(int player_index, enum ERAD_COLOURS colour)
{
    int fb = ddr_emitors.data[player_index].furthest_bullet;
    if ((enum ERAD_COLOURS)ddr_emitors.data[player_index].bullets[fb].custom_data != colour)
    {
        printf("Player %i pressed button %i\n", player_index, colour);
        return; //this is not right colour, if the bullet is missed, we will find about it in the update_bullets function
    }

    double dist_from_player = fabs(ddr_emitors.data[player_index].player_pos - ddr_emitors.data[player_index].bullets[fb].position);
    double beat_length = ddr_emitors.data[player_index].bullets[fb].speed / rad_game_songs.freq;
    double beat_ratio = dist_from_player / beat_length;
    int is_hit = DHI_MISS; //0
    while (is_hit < DHI_N_COUNT && beat_ratio < ddr_emitors.hit_intervals[is_hit]) is_hit++;
    printf("In frame %i player %i pressed button %i distance in leds %f, beat position %f\n", rad_game_source.cur_frame, player_index, colour, dist_from_player, beat_ratio);
    if (is_hit > 0)
    {
        DdrPlayer_action(player_index, (enum EDDR_HIT_INTERVAL)is_hit, colour);
        DdrEmitors_delete_bullet(player_index, fb);
    }
}

void RGM_DDR_player_move(int player_index, signed char dir)
{
    (void)player_index;
    (void)dir;
}

/*!
 * @brief Render score on emtitor position in the following way:
 *      0 -- this is emitor_pos -- white
 *      1 millions
 *      2 hundred th.
 *      3 tens th.
 *      4 thousands
 *      5 hundreds
 *      6 white
 *  The colours for 5 to 2 correspond to the colours of bullets, colour for millions is 0x888888
 *  The value is displayed in brightness: 0 -> 0, 1 -> 0.5, 5 -> 1, 9 -> 1.5;
 *  There is no blinking
 * @param ledstrip 
*/
static void DdrEmitors_render_emitors(ws2811_t* ledstrip)
{
    for (int p = 0; p < rad_game_source.n_players; ++p)
    {
        int short_score = ddr_emitors.data[p].points / 100;
        int digits[5];
        int magnitude = 1;
        for (int i = 0; i < 5; ++i)
        {
            digits[4 - i] = (short_score % (magnitude * 10)) / magnitude;
            magnitude *= 10;
        }
        ledstrip->channel[0].leds[ddr_emitors.data[p].emitor_pos] = 0xFFFFFF;
        ledstrip->channel[0].leds[ddr_emitors.data[p].emitor_pos + 6] = 0xFFFFFF;
        for (int i = 0; i < 5; ++i)
        {
            double brightness = digits[i] == 0 ? 0.0 : 1.0 + ((double)digits[i] - 5) / 8.0;
            int colour = i > 0 ? rad_game_source.basic_source.gradient.colors[ddr_emitors.bullet_colors_offset + 4 - i] : 0x888888;
            ledstrip->channel[0].leds[ddr_emitors.data[p].emitor_pos + 1 + i] = multiply_rgb_color(colour, brightness);
        }
    }
}

static void DdrEmitors_render_bullets(ws2811_t* ledstrip)
{
    for (int p = 0; p < rad_game_source.n_players; ++p)
    {
        for (int b = 0; b < ddr_emitors.data[p].n_bullets; ++b)
        {
            int color = rad_game_source.basic_source.gradient.colors[ddr_emitors.data[p].bullets[b].custom_data + ddr_emitors.bullet_colors_offset];
            RadMovingObject_render(&ddr_emitors.data[p].bullets[b], color, ledstrip);
        }
    }
}

static void DdrEmitors_render_players(ws2811_t* ledstrip)
{
    for (int p = 0; p < rad_game_source.n_players; ++p)
    {
        ledstrip->channel[0].leds[ddr_emitors.data[p].player_pos] = 0xFFFFFF;
    }
}

/*!
* @brief If reaction is active, render that, otherwise render streak
* Streak has its own gradient that we show one by one
*/
static void DdrEmitors_render_reactions(ws2811_t* ledstrip)
{
    for (int p = 0; p < rad_game_source.n_players; ++p)
    {
        int color1 = 0x0;
        int color2 = 0x0;
        int col2_start = ddr_emitors.reaction_len;

        if (ddr_emitors.data[p].reaction_progress > 0)
        {
            color1 = rad_game_source.basic_source.gradient.colors[ddr_emitors.data[p].reaction + ddr_emitors.reaction_colors_offset];
        }
        else if (ddr_emitors.data[p].streak > 0)
        {
            int grad_index = ddr_emitors.data[p].streak / ddr_emitors.reaction_len;
            if (grad_index < ddr_emitors.streak_grad_len)
            {
                col2_start = ddr_emitors.data[p].streak % ddr_emitors.reaction_len;
                color1 = rad_game_source.basic_source.gradient.colors[ddr_emitors.streak_grad_offset + grad_index];
                if (grad_index > 0)
                    color2 = rad_game_source.basic_source.gradient.colors[ddr_emitors.streak_grad_offset + grad_index - 1];
            }
            else
            {
                double A = ddr_emitors.streak_grad_len / grad_index;
                double y = A + (1 - A) * sin(M_PI * rad_game_songs.freq * ((rad_game_source.basic_source.current_time - rad_game_source.start_time) / (long)1e3) / (double)1e6);
                //the pulsing gets more intense as streak grows
                if (y < y) y = -y;
                color1 = multiply_rgb_color(rad_game_source.basic_source.gradient.colors[ddr_emitors.streak_grad_offset + ddr_emitors.streak_grad_len - 1], y);
            }
        }
        for (int led = ddr_emitors.data[p].reward_pos; led < ddr_emitors.data[p].reward_pos + col2_start; ++led)
        {
            ledstrip->channel[0].leds[led] = color1;
        }
        for (int led = ddr_emitors.data[p].reward_pos + col2_start; led < ddr_emitors.data[p].reward_pos + ddr_emitors.reaction_len; ++led)
        {
            ledstrip->channel[0].leds[led] = color2;
        }
    }
}

int RGM_DDR_update_leds(ws2811_t* ledstrip)
{
    for (int led = 0; led < rad_game_source.basic_source.n_leds; ++led)
    {
        ledstrip->channel[0].leds[led] = 0x0;
    }
    //update player
    long time_pos = SoundPlayer_play(SE_N_EFFECTS);
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
    //update emitors
    DdrEmitors_update();
    //update bullets
    DdrEmitors_update_bullets();

    //render emitor
    DdrEmitors_render_emitors(ledstrip);
    //render player
    DdrEmitors_render_players(ledstrip);
    //render bullets
    DdrEmitors_render_bullets(ledstrip);
    //render reaction
    DdrEmitors_render_reactions(ledstrip);
    return 1;
}

/*void RGM_DDR_destruct()
{
    (void)1;
}*/


void RGM_DDR_render_ready(ws2811_t* ledstrip)
{
    for (int led = 0; led < rad_game_source.basic_source.n_leds; ++led)
    {
        ledstrip->channel[0].leds[led] = 0x0;
    }
    //render emitor
    for (int p = 0; p < rad_game_source.n_players; ++p) ddr_emitors.data[p].points = 5555500;
    DdrEmitors_render_emitors(ledstrip);
    for (int p = 0; p < rad_game_source.n_players; ++p) ddr_emitors.data[p].points = 0;
    //render player
    DdrEmitors_render_players(ledstrip);
    //render reaction
    DdrEmitors_render_reactions(ledstrip);

}

void RGM_DDR_get_ready_interval(int player_index, int* left_led, int* right_led)
{
    *left_led = ddr_emitors.data[player_index].emitor_pos;
    *right_led = ddr_emitors.data[player_index].reward_pos + ddr_emitors.reaction_len - 1;
}