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
#include "m3_game_source.h"
#include "m3_field.h"
#include "m3_bullets.h"
#include "m3_players.h"
#include "m3_game.h"



int* canvas3;
int* zbuffer;

#ifdef DEBUG_M3
int* debug_fi_current;
int* debug_fi_previous;
#endif // DEBUG_M3


//! \brief all pulse functions expect progress prg going from 0 to 1
//! @param prg progress from 0 to 1
//! @return alpha from 0 to 1
static double saw_tooth(double prg)
{
    return 1.0 - prg;
}
static double saw_tooth_first_half(double prg)
{
    return 1.0 - 0.5 * prg;
}
static double saw_tooth_second_half(double prg)
{
    return 0.5 - 0.5 * prg;
}

static double(*pulse_functions[4])(double) = { 0x0, saw_tooth, saw_tooth_first_half, saw_tooth_second_half };


/************* Utility functions ************************/

void Match3_get_segment_and_position(const int segment_info, int* segment, int* position)
{
    *segment = segment_info >> C_SEGMENT_SHIFT;
    *position = ((segment_info - C_LED_Z) & ((1 << C_SEGMENT_SHIFT) - 1));
}

int Match3_get_segment_info(const int segment, const int position)
{
    return C_LED_Z + (segment << C_SEGMENT_SHIFT | position);
}

void Match3_print_info(int led)
{
    if (zbuffer[led] > 0 && zbuffer[led] < C_BULLET_Z)
        Segments_print_info(zbuffer[led] >> C_SEGMENT_SHIFT);
}

/************ Game interactions *************************/

int Match3_Game_catch_bullet(int led)
{
    int dist = 0;
    int catch = 999;
    int bullet_index = -1;
    while (dist <= match3_config.player_catch_distance && catch == 999)
    {
        if (zbuffer[led + dist] >= C_BULLET_Z) catch = dist;
        if (zbuffer[led - dist] >= C_BULLET_Z) catch = -dist;
        dist++;
    }
    if (catch == 999)
    {
        printf("bullet missed\n");
        return 1;
    }
    bullet_index = zbuffer[led + catch] - C_BULLET_Z;
    if (!Match3_Bullets_is_live(bullet_index))
    {
        printf("bullet was already caught\n");
        return 2;
    }
    int segment_info = Match3_Bullets_get_segment_info(bullet_index);
    if (segment_info == 0) //we are not in a segment, nothing can be inserted TODO -- we could check to the left of the bullet
    {
        printf("cannot catch bullet outside segment\n");
        return 3;
    }
    printf("Bullet %i caught at %i\n", bullet_index, led + catch);

    //insert new jewel in the field
    int segment, position;
    Match3_get_segment_and_position(segment_info, &segment, &position);
    jewel_type jt = Match3_Bullets_get_jewel_type(bullet_index);
    printf("Inserting jewel type %i at position %i\n", jt, Segments_get_field_index(segment, position));
    Field_insert_and_evaluate(segment, position, jt, bullet_index);
    //destroy the bullet
    Match3_Bullets_delete(bullet_index);
    return 0;
}


int Match3_Game_swap_jewels(int led, int dir)
{
    if (zbuffer[led] < C_BULLET_Z && zbuffer[led + dir] < C_BULLET_Z &&
        zbuffer[led] > 0 && zbuffer[led + dir] > 0)
    {
        printf("Switching bullets at position %i\n", led);
        int left_led = (dir > 0) ? led : led - 1;
        int segment, switch_pos;
        Match3_get_segment_and_position(zbuffer[left_led], &segment, &switch_pos);
        return Field_swap_and_evaluate(segment, switch_pos);
    }
    else
    {
        printf("Cannot switch jewels here\n");
        return 1;
    }
}

ws2811_led_t get_jewel_color(jewel_type jewel_type)
{
    int gradient_index = (2 * match3_config.n_half_grad + 1) * jewel_type + match3_config.n_half_grad;
    //return match3_game_source.basic_source.gradient.colors[gradient_index];
    return match3_game_source.jewel_colors[gradient_index];
}

/***************** Renderigs *******************************/

static void render_players(void)
{
    double d = miliseconds_from_start() / (double)match3_config.player_dit_length;
    int dit = (int)d % match3_config.player_n_dits;
    double prg = d - trunc(d);

    int highlited_player = Match3_Player_get_highlight();
    if (highlited_player > -1)  //we shall only paint this one player and nothing else -- only available in select phase
    {
        int pos = Match3_Player_get_position(highlited_player);
        canvas3[pos] = match3_config.player_colour | 0xFF << 24;
        return;
    }

    for (int player = 0; player < match3_game_source.n_players; ++player)
    {
        if (!Match3_Player_is_rendered(player)) 
            continue;
        //player is displayed when its pattern is on or when it is just moving
        int is_moving = Match3_Player_is_moving(player);
        if (match3_config.player_patterns[player][dit] || is_moving)
        {
            int pos = Match3_Player_get_position(player);
            unsigned char alpha = is_moving ? 0xFF : (unsigned char)(pulse_functions[match3_config.player_patterns[player][dit]](prg) * 0xFF);
            canvas3[pos] = match3_config.player_colour | alpha << 24;
        }
    }
}

static void render_bullets(void)
{
    int n_bullets = Match3_Bullets_get_n();
    for (int bullet = 0; bullet < n_bullets; ++bullet)
    {
        double frac = Match3_Bullets_get_position(bullet);
        int led = (int)frac;
        frac = frac - led;
        frac *= 2;
        while (frac > 1.) frac -= 1;
        double alpha = saw_tooth(frac);
        zbuffer[led] = C_BULLET_Z + bullet;
        int colour = get_jewel_color(Match3_Bullets_get_jewel_type(bullet));
        canvas3[led] = colour | (int)(0xFF * alpha) << 24;
    }
}

/*
static void render_bullets_alpha(void)
{
    int n_bullets = Match3_Bullets_get_n();
    for (int bullet = 0; bullet < n_bullets; ++bullet)
    {
        int led = (int)Match3_Bullets_get_position(bullet);
        for (int i = 1; i < 4; ++i)
        {
            int alpha = (int)((0.25 + (double)i / 8.) * 0xFF);
            int canvas_colour = canvas3[led + i] & 0xFFFFFF;
            canvas3[led + i] = alpha << 24 | canvas_colour;
            canvas_colour = canvas3[led - i] & 0xFFFFFF;
            canvas3[led - i] = alpha << 24 | canvas_colour;
        }
    }
}
*/

static void render_collapsing_segments(void)
{
    int last_led = match3_game_source.basic_source.n_leds - 1 - Match3_Emitor_get_length();
    int segment = Segments_get_next_collapsing(-1);
    while (segment > -1)
    {
        int led_discombobulation = 0;
        int bullet_leaving = (Segments_get_n_bullets(segment) > 0 && Segments_get_bullet(segment, 0).segment_position == 0) ? 1 : 0;
        int leaving_bullet_index = bullet_leaving ? Segments_get_bullet(segment, 0).bullet_index : -1;
        Segments_reset_bullets(segment);
        int segment_length = Segments_get_length(segment);
        int segment_position = (int)floor(Segments_get_position(segment));
        double collapse_progress = 4. * Segments_get_collapse_progress(segment);
        while (collapse_progress > 1) collapse_progress--;
        for (int pos = 0; pos < segment_length; ++pos)
        {
            int led = pos + segment_position + led_discombobulation;
            if (bullet_leaving == 1 && pos == 0 && led_discombobulation == 0) //bullet has already left
            {
                bullet_leaving = 2;
                led++;
                segment_position++;
                if (led > last_led)
                    continue;
            }
            //check collision with bullets
            while (zbuffer[led] > 0)
            {
                ASSERT_M3_CONTINUE(zbuffer[led] >= C_BULLET_Z); //we should never render one segment over another
                //Match3_Bullets_set_segment_info(zbuffer[led] - C_BULLET_Z, Match3_get_segment_info(segment, pos));
                Segments_add_bullet(segment, zbuffer[led] - C_BULLET_Z, pos);
                led += 1;
                led_discombobulation++;
                if (led > last_led)
                    break;
            }
            canvas3[led] = ((int)(0xFF * collapse_progress) << 24) | match3_config.collapse_colour;;
            zbuffer[led] = 0;
#ifdef DEBUG_M3
            debug_fi_current[led] = 1 + Segments_get_field_index(segment, pos);
#endif // DEBUG_M3
        }
        if (bullet_leaving == 2)
        {
            printf("Collapsing segment %i moved by bullet %i\n", segment, leaving_bullet_index);
            Segments_add_shift(segment, 1);
        }
        segment = Segments_get_next_collapsing(segment);
    }
}

static void render_moving_segments(void)
{
    double sins[N_GEM_COLORS] = { 0 };
    double coss[N_GEM_COLORS] = { 0 };
    for (int i = 0; i < N_GEM_COLORS; ++i)
    {
        sins[i] = sin(2 * M_PI * match3_config.gem_freq[i] * (match3_game_source.basic_source.current_time - match3_game_source.start_time) / 1e9);
        coss[i] = cos(2 * M_PI * match3_config.gem_freq[i] * (match3_game_source.basic_source.current_time - match3_game_source.start_time) / 1e9);
    }

    int last_led = match3_game_source.basic_source.n_leds - 1 - Match3_Emitor_get_length();
    int segment = Segments_get_next_moving(-1);
    while (segment > -1)
    {
        int bullet_leaving = (Segments_get_n_bullets(segment) > 0 && Segments_get_bullet(segment, 0).segment_position == 0);
        Segments_reset_bullets(segment);
        int segment_length = Segments_get_length(segment);
        int segment_position = (int)floor(Segments_get_position(segment));
        int hole_position = Segments_get_hole_position(segment);
        int hole_direction = -Segments_get_direction(segment);

        int led_discombobulation = 0;
        for (int pos = 0; pos < segment_length; ++pos)
        {
            int led = pos + segment_position + led_discombobulation;
            if (pos >= hole_position)
            {
                led++;
            }
            if ((led + hole_direction) > last_led)
            {
                match3_game_source.level_phase = M3LP_LEVEL_LOST;
                continue;
            }
            if (led < 0)
                continue;

            if (bullet_leaving == 1 && pos == 0 && led_discombobulation == 0) //bullet has already left
            {
                bullet_leaving = 2;
                led++;
                led_discombobulation++;
                if (led > last_led)
                    continue;
            }

            if (pos == hole_position) //we will outptut two leds for this pos, one for the hole, one for the jewel
            {
                if (zbuffer[led + hole_direction] > 0) //if there is bullet on the hole position, we have to shift even more
                {
                    ASSERT_M3_CONTINUE(zbuffer[led + hole_direction] >= C_BULLET_Z); //we should never render one field over another
                    Match3_Bullets_set_segment_info(zbuffer[led + hole_direction] - C_BULLET_Z, Match3_get_segment_info(segment, pos));
                    Segments_add_bullet(segment, zbuffer[led + hole_direction] - C_BULLET_Z, pos);
                    led += 1;
                    led_discombobulation++;
                }
            }
            if (led > last_led)
                continue;
            //check collision with bullets
            while (zbuffer[led] > 0)
            {
                ASSERT_M3_CONTINUE(zbuffer[led] >= C_BULLET_Z); //we should never render one segment over another
                Match3_Bullets_set_segment_info(zbuffer[led] - C_BULLET_Z, Match3_get_segment_info(segment, pos));
                Segments_add_bullet(segment, zbuffer[led] - C_BULLET_Z, pos);
                led += 1;
                led_discombobulation++;
                if (led > last_led)
                    break;
            }
            if (led > last_led)
                continue;

            jewel_t jewel = Segments_get_jewel(segment, pos);
            jewel_type type = jewel.type;
            //this will transform ampl to <0, 2 * N_HALF_GRAD>
            double ampl = (sins[type] * jewel.cos_phase + coss[type] * jewel.sin_phase + 1) * match3_config.n_half_grad;
            int gradient_index = (int)ampl;
            //double blend = ampl - (int)ampl;
            gradient_index += (2 * match3_config.n_half_grad + 1) * type;
            ASSERT_M3_CONTINUE(gradient_index >= type * (2 * match3_config.n_half_grad + 1));
            ASSERT_M3_CONTINUE(gradient_index < (type + 1)* (2 * match3_config.n_half_grad + 1));
            ASSERT_M3_CONTINUE(gradient_index + 1 < match3_game_source.basic_source.gradient.n_colors);
            //gradient_index = (2 * match3_config.n_half_grad + 1) * type + match3_config.n_half_grad;

            //canvas3[led] = 0xFF << 24 | match3_game_source.basic_source.gradient.colors[gradient_index];
            canvas3[led] = 0xFF << 24 | match3_game_source.jewel_colors[gradient_index];
            zbuffer[led] = Match3_get_segment_info(segment, pos);

#ifdef DEBUG_M3
            debug_fi_current[led] = 1 + Segments_get_jewel_id(segment, pos);
#endif // DEBUG_M3
        }
        Segments_set_discombobulation(segment, led_discombobulation);
        if (bullet_leaving == 2)
        {
            Segments_add_shift(segment, 1);
        }
        segment = Segments_get_next_moving(segment);
    }
}

static void render_emitor(void)
{
    int from = match3_game_source.basic_source.n_leds - 1 - Match3_Emitor_get_length();
    int to = match3_game_source.basic_source.n_leds;
    for (int led = from; led < to; ++led)
    {
        canvas3[led] = get_jewel_color(Match3_Emitor_get_jewel_type()) | 0xFF << 24;
    }
}

static void clear_canvas(void)
{
    //clear z-buffer
    memset(zbuffer, 0, sizeof(int) * match3_game_source.basic_source.n_leds);
    //clear canvas
    memset(canvas3, 0, sizeof(int) * match3_game_source.basic_source.n_leds);

}


//! \brief Calculate actual positions of field segments, bullets and players during normal play
//! 
//! How to render the field
//!  1. render bullets
//!  2. render jewels (i.e segments), may be affected bullets
//!  3. render players (no z-checks, painted over everything)
void Match3_Game_render_field(void)
{
    clear_canvas();
    //render all objects to canvas and zbuffer
    render_bullets();
    render_moving_segments();
    render_players();
    render_emitor();
    render_collapsing_segments();
}

void Match3_Game_render_select(void)
{
    clear_canvas();
    render_players();
}


//! @brief Actually render the colours from canvas to leds
//! @param frame 
//! @param ledstrip 
void Match3_Game_render_leds(int frame, ws2811_t* ledstrip)
{
    //TODO this could/should be a separate function
    //output canvas into leds
#ifdef DEBUG_M3
    static int leds_moved_all = 0;
    int leds_moved = 0;
#endif // DEBUG_M3

    for (int led = 0; led < match3_game_source.basic_source.n_leds; ++led)
    {
        double canvas_alpha = (double)((canvas3[led] & 0xFF000000) >> 24) / (double)0xFF;
        int canvas_colour = canvas3[led] & 0xFFFFFF;
        ledstrip->channel[0].leds[led] = multiply_rgb_color(canvas_colour, canvas_alpha);
#ifdef DEBUG_M3
        if (debug_fi_current[led] != debug_fi_previous[led]) leds_moved++;
        debug_fi_previous[led] = 0;
#endif // DEBUG_M3
    }

#ifdef DEBUG_M3
    if (frame > 2)
    {
        leds_moved_all += leds_moved;
        if (leds_moved > 6)
            printf("LEDs moved this frame %i, average %f\n", leds_moved, (double)leds_moved_all / frame);
    }
    int* tmp = debug_fi_previous;
    debug_fi_previous = debug_fi_current;
    debug_fi_current = tmp;
#else
    (void)frame;
#endif // DEBUG_M3
}

void Match3_Game_init(void)
{
    canvas3 = malloc(sizeof(int) * match3_game_source.basic_source.n_leds);
    zbuffer = malloc(sizeof(int) * match3_game_source.basic_source.n_leds);
#ifdef DEBUG_M3
    debug_fi_current = malloc(sizeof(int) * match3_game_source.basic_source.n_leds);
    debug_fi_previous = malloc(sizeof(int) * match3_game_source.basic_source.n_leds);
#endif // DEBUG_M3

}

void Match3_Game_destruct(void)
{
    free(canvas3);
    free(zbuffer);
#ifdef DEBUG_M3
    free(debug_fi_current);
    free(debug_fi_previous);
#endif // DEBUG_M3
}
