#define _CRT_SECURE_NO_WARNINGS
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunknown-pragmas"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <assert.h>
#ifdef __linux__
#include "ws2811.h"
#else
#include "fakeled.h"
#include "faketime.h"
#endif // __linux__

#include "common_source.h"
#include "colours.h"
#include "xmas_source.h"


static struct {
    //snowflakes
    int n_snowflakes;
    float k_diff;
    float k_spec;
    float spec_phase;
    float spec;
    int snowflake_color;
    float move_chance;
    long diff_base_period;
    long diff_period_range;
    long spec_base_period;
    long spec_period_range;
    float down_chance;
    float left_chance;
    //icicles
    int n_icicle_leds;
    float icicle_speed;
    //gradient
    int gradient_speed;
    //pattern
    int beat_length;
    //valeria
    float valeria_speed;
    //random
    int is_random;
} config;

typedef struct GlitterConfig
{
    float glitter_chance;
    int color;
    long base_period;
    long period_range;
    float phase_position;
    float phase_constant;
    float phase_random;
    float prob1;
    float prob2;
    float prob3;
    float prob4;
    float prob5;
    float amp_add;
    float amp_mul;
} glitter_config_t;

glitter_config_t* glitter_config;
glitter_config_t glt1_config;
glitter_config_t glt2_config;


#pragma region Geometry

typedef enum dir {
    UP,
    RIGHT = 2,
    DOWN = 4,
    LEFT = 6,
    FORWARD = 8,
    BACKWARD = 10,
    HEIGHT = 12
} dir_t;

//neighbors of i-the led and their distance:
// index of upper neighbor, distance to upper neighbor, index of right neighbor, distance to right neighbor, ...
// order is up, right, down, left
// the last column (12) is the height, i.e. how many leds there are below me
static struct {
    int (*neighbors)[HEIGHT + 1];
    int* heads;
    int* springs;
    int n_heads;
    int n_springs;
} geometry;

//Geometry calculations {{{ 
// These functions are only called during init

// 'head' is a led such that 
// (geometry[head][DOWN] != -1) &&  <-- there is something below me
// (geometry[geometry[head][DOWN]][UP] == head) <-- The led below me points back to me
// ((geometry[head][UP] == -1) || (geometry[geometry[head][UP]][DOWN] != head)) && <-- I am either top, or the led above does not point back to me
//           1               geometry[1][UP] = -1; geometry[1][DOWN] = 2
//           |\              geometry[2][UP] =  1; geometry[2][DOWN] = 4
//           2-3             geometry[3][UP] =  1; geometry[3][DOWN] = 5
//           | |\            geometry[4][UP] =  2; geometry[4][DOWN] = 7
//           4-5-6           geometry[5][UP] =  3; geometry[5][DOWN] = 7
//            \|\|           geometry[6][UP] =  3; geometry[6][DOWN] = 8
//             7 8           geometry[7][UP] =  5; geometry[8][UP] = 6
// Heads are 1, 3, 6
// Springs are like heads, but at the bottom
static int check_terminal(int led, dir_t flow_from, dir_t flow_to)
{
    return (
        (geometry.neighbors[led][flow_from] != -1) &&  //there is something in the direction of flow
        (geometry.neighbors[geometry.neighbors[led][flow_from]][flow_to] == led) && //I am upstream from the led in the direction of flow
        ((geometry.neighbors[led][flow_to] == -1) || (geometry.neighbors[geometry.neighbors[led][flow_to]][flow_from] != led)) //I am either end of flow, or the led upstream from me does not flow back to me
        );
}

static void find_heads_and_springs()
{
    geometry.heads = malloc(sizeof(int) * xmas_source.basic_source.n_leds / 2); //there can never be that many heads or springs, but I don't care about saving few bytes of memory
    geometry.springs = malloc(sizeof(int) * xmas_source.basic_source.n_leds / 2);
    geometry.n_heads = 0;
    geometry.n_springs = 0;
    for (int led = 0; led < xmas_source.basic_source.n_leds; ++led)
    {
        if (check_terminal(led, DOWN, UP))
        {
            geometry.heads[geometry.n_heads++] = led;
        }
        if (check_terminal(led, UP, DOWN))
        {
            geometry.springs[geometry.n_springs++] = led;
        }
    }
    //debug output
    printf("HEADS: ");
    for (int i = 0; i < geometry.n_heads; ++i) printf("%d, ", geometry.heads[i]);
    printf("\nSPRINGS: ");
    for (int i = 0; i < geometry.n_springs; ++i) printf("%d, ", geometry.springs[i]);
    printf("\n");
}


static void calculate_height()
{
    for (int i = 0; i < xmas_source.basic_source.n_leds; ++i)
    {
        if (geometry.neighbors[i][HEIGHT] != -1) //we have been here already
            continue;

        int down = geometry.neighbors[i][DOWN];
        int depth = 0;
        while (down != -1 && geometry.neighbors[down][HEIGHT] == -1)
        {
            depth++;
            down = geometry.neighbors[down][DOWN];
        }
        if (down != -1) //we are not at the bottom but we were here already
        {
            depth += geometry.neighbors[down][HEIGHT] + 1;
        }
        geometry.neighbors[i][HEIGHT] = depth;
        down = geometry.neighbors[i][DOWN];
        while (down != -1 && geometry.neighbors[down][HEIGHT] == -1)
        {
            geometry.neighbors[down][HEIGHT] = --depth;
            down = geometry.neighbors[down][DOWN];
        }
    }
}

static int XmasSource_read_geometry()
{
    geometry.neighbors = malloc(xmas_source.basic_source.n_leds * sizeof(*geometry.neighbors));
    FILE* fgeom = fopen("geometry", "r");
    if (fgeom == NULL) {
        printf("Geometry file not found\n");
        exit(-4);
    }
    int row = 0;
    while (row < xmas_source.basic_source.n_leds)
    {
        int n = fscanf(fgeom, "%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\n",
            &geometry.neighbors[row][UP], &geometry.neighbors[row][UP + 1],
            &geometry.neighbors[row][RIGHT], &geometry.neighbors[row][RIGHT + 1],
            &geometry.neighbors[row][DOWN], &geometry.neighbors[row][DOWN + 1],
            &geometry.neighbors[row][LEFT], &geometry.neighbors[row][LEFT + 1]);
        if (n != 8)
        {
            printf("Error reading geometry\n");
            return 0;
        }
        geometry.neighbors[row][FORWARD] = row + 1;
        geometry.neighbors[row][FORWARD + 1] = 1;
        geometry.neighbors[row][BACKWARD] = row - 1;
        geometry.neighbors[row][BACKWARD + 1] = 1;
        geometry.neighbors[row][HEIGHT] = -1;
        row++;
    }
    // the last led has its FORWARD neighbor set to n_leds not
    geometry.neighbors[xmas_source.basic_source.n_leds - 1][FORWARD] = -1;
    /* //debug print line 90
    printf("Row 90: %d %d - %d %d - %d %d - %d %d\n", xmas_source.geometry[90][UP], xmas_source.geometry[90][UP+1],
            xmas_source.geometry[90][RIGHT], xmas_source.geometry[90][RIGHT+1],
            xmas_source.geometry[90][DOWN], xmas_source.geometry[90][DOWN+1],
            xmas_source.geometry[90][LEFT], xmas_source.geometry[90][LEFT+1]);
    */
    calculate_height();
    find_heads_and_springs();
    return 1;
}

#pragma endregion

#pragma region Common

static long random_time_start;

typedef struct PeriodData {
    //all are times in ms
    unsigned long lastChange;
    unsigned long nextChange;
    unsigned long basePeriod;
    unsigned long periodRange;
    double phaseShift;
} period_data_t;

unsigned long current_time_in_ms()
{
    return (unsigned long)(xmas_source.basic_source.current_time / (uint64_t)1e6);
}

/**
* Generate linearly increasing number from 0 to 2 * pi, with some random variation
* The length of the period is within interval basePeriod +- 0.5 * periodRange
* Once 2*pi angle is reached, new period is calculated
*/
double get_angle(period_data_t* period_data)
{
    unsigned long cur_time = current_time_in_ms();
    if (cur_time >= period_data->nextChange)
    {
        period_data->lastChange = period_data->nextChange;
        period_data->nextChange = cur_time + period_data->basePeriod + (long)((random_01() - 0.5f) * period_data->periodRange);
    }
    return 2 * M_PI * (double)(cur_time - period_data->lastChange) / (double)(period_data->nextChange - period_data->lastChange) + period_data->phaseShift;
}

typedef struct MovingLed {
    int origin;     // index of the LED where the motion started
    float distance; // distance from origin, 0 to 1
    dir_t direction;
    float speed;    // in leds per second
    int stop_at_destination; // 0 - continue movement, 1 stop at destination
    int is_moving;  // 0 - not moving, 1 - is moving
    int next_stop;
    int destination;
} moving_led_t;

void MovingLed_move(moving_led_t* moving_led)
{
    if (moving_led->is_moving == 0)
        return;
    double time_seconds = (xmas_source.basic_source.time_delta / (long)1e3) / (double)1e6;
    moving_led->distance += moving_led->speed * (float)time_seconds;
    if (moving_led->distance >= 1.0f)
    {
        moving_led->distance -= 1;
        if (moving_led->distance > 1.0f) //this should never happen, it means that we have travelled more than one led distance in one frame
        {
            moving_led->distance = 0.9f;
        }
        moving_led->origin = geometry.neighbors[moving_led->origin][moving_led->direction];
        if ((geometry.neighbors[moving_led->origin][moving_led->direction] == -1) // we cannot keep moving in this direction
            || (moving_led->destination == moving_led->origin && moving_led->stop_at_destination))  // or we are supposed to stop at destination and we have arrived
        {
            moving_led->distance = 0.0f;
            moving_led->is_moving = 0;
        }
    }
    moving_led->next_stop = geometry.neighbors[moving_led->origin][moving_led->direction];
}

int MovingLed_get_intensity(moving_led_t* moving_led, float* at_origin, float* at_destination)
{
    *at_origin = (1.f - moving_led->distance);
    *at_destination = moving_led->distance;
    /*
    if(moving_led->distance > 0.5f)
    {
        *at_origin = 0.f;
        *at_destination = 1.f;
    }
    else
    {
        *at_origin = 1.f;
        *at_destination = 0.f;
    }// */
    return 1;
}

#pragma endregion

#pragma region Snowflakes

static moving_led_t* snowflakes;
static period_data_t* diff_data;
static period_data_t* spec_data;
static hsl_t snowflake_colors[3]; // 0 is black before and after, 2 is the actual snowflake, 1 is the edge

void Snowflakes_init()
{
    snowflakes = malloc(sizeof(moving_led_t) * config.n_snowflakes);
    diff_data = malloc(sizeof(period_data_t) * config.n_snowflakes);
    spec_data = malloc(sizeof(period_data_t) * config.n_snowflakes);
    if (!snowflakes || !diff_data || !spec_data)
    {
        printf("Cannot allocate memory for snowflakes\n");
        return;
    }
    int d = (int)(xmas_source.basic_source.n_leds / config.n_snowflakes);
    unsigned long cur_time = current_time_in_ms();
    for (int flake = 0; flake < config.n_snowflakes; ++flake)
    {
        diff_data[flake].nextChange  = cur_time - 1;
        diff_data[flake].basePeriod  = config.diff_base_period;
        diff_data[flake].periodRange = config.diff_period_range;
        diff_data[flake].phaseShift  = 0;
        spec_data[flake].nextChange  = cur_time - 1;
        spec_data[flake].basePeriod  = config.spec_base_period;
        spec_data[flake].periodRange = config.spec_period_range;
        spec_data[flake].phaseShift  = config.spec_phase;

        snowflakes[flake].origin = flake * d;
        snowflakes[flake].speed = 0.5f;
        snowflakes[flake].distance = 0;
        snowflakes[flake].is_moving = 0;
        snowflakes[flake].stop_at_destination = 1;
    }
    for(int i=0;i<config.n_snowflakes;++i) printf("Flake %d origin %d\n",i,snowflakes[i].origin);
    rgb2hsl(xmas_source.basic_source.gradient.colors[config.snowflake_color], &snowflake_colors[2]);
    rgb2hsl(xmas_source.basic_source.gradient.colors[config.snowflake_color+1], &snowflake_colors[1]);
    snowflake_colors[0] = snowflake_colors[1];
    snowflake_colors[0].l = 0.f;
    //for(int i = 0; i < 3; ++i) printf("Color %i -- h: %f, s: %f, l: %f\n", i, snowflake_colors[i].h, snowflake_colors[i].s, snowflake_colors[i].l);
}

void Snowflakes_update()
{
    for (int flake = 0; flake < config.n_snowflakes; flake++)
    {
        if(snowflakes[flake].is_moving)
        {
            MovingLed_move(&snowflakes[flake]);
            continue;
        }
        if (random_01() < config.move_chance)
        {
            printf("Moving snowflake number %d\n", flake);
            //now we need to generate direction, let's say there is 50% chance of going down
            float r01 = random_01();
            int dir = (r01 < config.down_chance) ? DOWN : (r01 < (config.down_chance + config.left_chance) ? LEFT : RIGHT);
            //check if there is a led in this direction
            if ((geometry.neighbors[snowflakes[flake].origin][dir] == -1) || (geometry.neighbors[snowflakes[flake].origin][dir + 1] != 1))
            {
                dir = DOWN; //if we can't move in the desired direction, we can try to go DOWN
            }
            if (geometry.neighbors[snowflakes[flake].origin][dir] != -1)
            {
                snowflakes[flake].direction = dir;
                snowflakes[flake].is_moving = 1;
            }
            else
            {
                //we cannot move this snowflake any further, so we shall spawn a new one
                int new_flake = (int)(random_01() * geometry.n_heads);
                snowflakes[flake].origin = geometry.heads[new_flake];
                printf("Spawning new flake %i at %d\n", flake, new_flake);
            }
        }
    }
    //printf("SF update finished\n");
}

static void add_close_neighbor(int* add_to, int add_index, int led_index, dir_t dir)
{
    if(geometry.neighbors[led_index][dir + 1] == 1)
    {
        add_to[add_index] = geometry.neighbors[led_index][dir];
    }
}

static int update_leds_snowflake(ws2811_t* ledstrip)
{
    Snowflakes_update();
    for (int led = 0; led < xmas_source.basic_source.n_leds; ++led)
    {
        ledstrip->channel[0].leds[led] = 0;
    }

    // we shall create a structure for the leds influenced by the current snowflakes
    //      4 7
    //    3 0 1 5  -> for snowflake moving to right
    //      2 6
    int flake_leds[8];
    for (int flake = 0; flake < config.n_snowflakes; ++flake)
    {
        flake_leds[0] = snowflakes[flake].origin;
        for(int i = 1; i < 8; ++i)
        {
            flake_leds[i] = -1;
        }
        dir_t dir = UP;
        if (snowflakes[flake].is_moving)
        {
            dir = snowflakes[flake].direction;
        }
        for (int i = 0; i < 4; ++i)
        {
            add_close_neighbor(flake_leds, i + 1, flake_leds[0], (dir + 2 * i) % 8);
        }
        if (snowflakes[flake].is_moving)
        {
            add_close_neighbor(flake_leds, 5, flake_leds[1], dir);
            add_close_neighbor(flake_leds, 6, flake_leds[1], (dir + 2) % 8);
            add_close_neighbor(flake_leds, 7, flake_leds[1], (dir + 6) % 8);
        }

        float origin_intensity, destination_intensity;
        MovingLed_get_intensity(&snowflakes[flake], &origin_intensity, &destination_intensity);

        double diff_alpha = get_angle(&diff_data[flake]);
        double spec_alpha = get_angle(&spec_data[flake]);
        double l = config.k_diff * cos(diff_alpha) + config.k_spec * pow(cos(spec_alpha), config.spec);

        hsl_t centre_col = snowflake_colors[2];
        float snowflake_hue = centre_col.h;
        centre_col.l += (float)l;
        if (centre_col.l + l > 1.f) centre_col.l = 1.f;
        hsl_t edge_col = snowflake_colors[1];
        edge_col.l += (float)pow(l, 2);
        if (edge_col.l > 1.f) edge_col.l = 1.f;
        hsl_t black_col = snowflake_colors[0];
        //now set all leds
        hsl_t res;
        lerp_hsl(&edge_col, &centre_col, origin_intensity, &res);
        assert((res.h - snowflake_hue) * (res.h - snowflake_hue) < 0.01);
        ledstrip->channel[0].leds[flake_leds[0]] = hsl2rgb(&res);
        lerp_hsl(&centre_col, &edge_col, origin_intensity, &res);
        assert((res.h - snowflake_hue) * (res.h - snowflake_hue) < 0.01);
        if (flake_leds[1] != -1) 
            ledstrip->channel[0].leds[flake_leds[1]] = hsl2rgb(&res);
        lerp_hsl(&black_col, &edge_col, origin_intensity, &res);
        assert((res.h - snowflake_hue) * (res.h - snowflake_hue) < 0.01);
        for (int i = 2; i < 5; ++i) // 2,5
        {
            if (flake_leds[i] != -1) ledstrip->channel[0].leds[flake_leds[i]] = hsl2rgb(&res);
        }
        lerp_hsl(&edge_col, &black_col, origin_intensity, &res);
        assert((res.h - snowflake_hue) * (res.h - snowflake_hue) < 0.01);
        for (int i = 5; i < 8; ++i) //5,8
        {
            if (flake_leds[i] != -1) ledstrip->channel[0].leds[flake_leds[i]] = hsl2rgb(&res);
        }
    }
    return 1;
}

#pragma endregion

#pragma region Glitter

static int select_glitter_color()
{
    //1 - green 30% , 2 -- red 30%, 3 -- bright orange 10%, 4 -- purple 10%, 5 -- blue 20%
    float r01 = random_01();
    if (r01 < glitter_config->prob1) return glitter_config->color + 0; else r01 -= glitter_config->prob1;
    if (r01 < glitter_config->prob2) return glitter_config->color + 1; else r01 -= glitter_config->prob2;
    if (r01 < glitter_config->prob3) return glitter_config->color + 2; else r01 -= glitter_config->prob3;
    if (r01 < glitter_config->prob4) return glitter_config->color + 3;
    return 4;
}

static period_data_t* glitter_periods;
static ws2811_led_t* glitter_colors;

static void Glitter_init_common()
{
    glitter_periods = malloc(sizeof(period_data_t) * xmas_source.basic_source.n_leds);
    glitter_colors = malloc(sizeof(ws2811_led_t) * xmas_source.basic_source.n_leds);
    if (!glitter_periods || !glitter_colors)
    {
        printf("Cannot allocate memory for glitter.\n");
        return;
    }
    unsigned long cur_time = current_time_in_ms();
    for (int led = 0; led < xmas_source.basic_source.n_leds; ++led)
    {
        int col = select_glitter_color();
        glitter_colors[led] = xmas_source.basic_source.gradient.colors[col];
        glitter_periods[led].nextChange = cur_time - 1;
        glitter_periods[led].basePeriod = glitter_config->base_period;
        glitter_periods[led].periodRange = glitter_config->period_range;
        glitter_periods[led].phaseShift = glitter_config->phase_constant +
            glitter_config->phase_position * (double)led / (double)xmas_source.basic_source.n_leds +
            glitter_config->phase_random * random_01();
        //printf("Setting led %d to shift %f\n", led, glitter_periods[led].phaseShift);
    }
}

static void Glitter1_init()
{
    glitter_config = &glt1_config;
    Glitter_init_common();
    printf("Glitter 1 initialized\n");
}

static void Glitter2_init()
{
    glitter_config = &glt2_config;
    Glitter_init_common();
    printf("Glitter 2 initialized\n");
}

static void Glitter_destruct()
{
    free(glitter_periods);
    free(glitter_colors);
}

static int update_leds_glitter(ws2811_t* ledstrip)
{
    //in all subsequent updates there is a chance that exactly one led will be set to new colour (or possibly the same colour)
    if (random_01() < glitter_config->glitter_chance)
    {
        int led = (int)(random_01() * xmas_source.basic_source.n_leds);
        int col = select_glitter_color();
        glitter_colors[led] = xmas_source.basic_source.gradient.colors[col];
        //printf("Resetting led %d to color %x\n", led, xmas_source.basic_source.gradient.colors[col]);
        return 1;
    }
    for (int led = 0; led < xmas_source.basic_source.n_leds; ++led)
    {
        double angle = get_angle(&glitter_periods[led]);
        double t = glitter_config->amp_add + glitter_config->amp_mul * cos(angle);
        ledstrip->channel[0].leds[led] = multiply_rgb_color(glitter_colors[led], t);
        //printf("%f  ", t);
    }
    //printf("\n");
    return 1;
}

#pragma endregion

#pragma region Icicles

static moving_led_t* icicles;
static hsl_t* icicle_colors; // 0 and 5 are black before and after, the rest is { 6, 7, 8, 9 } in config
static const int C_ICICLE_COLOR = 6;

static void Icicles_init()
{
    icicle_colors = malloc(sizeof(hsl_t) * (config.n_icicle_leds + 2));
    icicles = malloc(sizeof(moving_led_t) * geometry.n_heads);
    if (!icicle_colors || !icicles)
    {
        printf("Cannot allocate memory for icicles.\n");
        return;
    }
    for (int i = 0; i < geometry.n_heads; ++i)
    {
        //printf("h %i\n", geometry.neighbors[geometry.heads[i]][HEIGHT]);
        if(geometry.neighbors[geometry.heads[i]][HEIGHT] < 3)
        {
            icicles[i].origin = -1;
            continue;
        }
        icicles[i].origin = geometry.heads[i];
        icicles[i].direction = DOWN;
        icicles[i].distance = 0;
        icicles[i].speed = config.icicle_speed;
        icicles[i].stop_at_destination = 0;
        icicles[i].is_moving = 1;
    }
    for(int i = 0; i < config.n_icicle_leds; ++i)
    {
        rgb2hsl(xmas_source.basic_source.gradient.colors[C_ICICLE_COLOR+i], &icicle_colors[i+1]);
    }
    icicle_colors[0] = icicle_colors[1];
    icicle_colors[0].l = 0.f;
    icicle_colors[config.n_icicle_leds + 1] = icicle_colors[config.n_icicle_leds];
    icicle_colors[config.n_icicle_leds + 1].l = 0.f;
}

/**
* Every element in icicles is just the top led. We than render (n_icicle_leds + 1) leds:
*   Actual LEDs:         o    o    o    o    o
*   Icicle elements:       *    *    *    *      -> movement
* We add two black elements in front and in the end and then we interpolate between icicle elements
* to get the colour of the LED
*/
static int update_leds_icicles(ws2811_t* ledstrip)
{
    for (int led = 0; led < xmas_source.basic_source.n_leds; ++led)
    {
        ledstrip->channel[0].leds[led] = 0;
    }
    for (int i = 0; i < geometry.n_heads; ++i)
    {
        if(icicles[i].origin == -1)
        {
           continue;
        }
        if(random_01() < 0.5)
            MovingLed_move(&icicles[i]);
        if (icicles[i].is_moving == 0)
        {
            icicles[i].origin = geometry.heads[i];
            icicles[i].is_moving = 1;
        }
        float origin_intensity, destination_intensity;
        MovingLed_get_intensity(&icicles[i], &origin_intensity, &destination_intensity);
        int led = icicles[i].origin;
        hsl_t hsl_out;
        lerp_hsl(&icicle_colors[0], &icicle_colors[1], origin_intensity, &hsl_out);
        ledstrip->channel[0].leds[led] = hsl2rgb(&hsl_out);
        for (int ice_led = 0; ice_led < config.n_icicle_leds; ++ice_led)
        {
            led = geometry.neighbors[led][icicles[i].direction];
            if (led == -1)
                break;
            lerp_hsl(&icicle_colors[ice_led+1], &icicle_colors[ice_led+2], origin_intensity, &hsl_out);
            ledstrip->channel[0].leds[led] = hsl2rgb(&hsl_out);
            //if(i == 0) printf("Ice led %i: %f\n", led, hsl[2]);
        }
        //printf("Updated %d led with intensity %f\n", icicles[i].led.origin, origin_intensity);
    }

    return 1;
}

#pragma endregion

#pragma region Gradient

static const int XMAS_GRAD_START = 12;
#define XMAS_GRAD_LEN 19

static unsigned long start_time = 0;
static hsl_t grad_colors[XMAS_GRAD_LEN];

static void Gradient_init()
{
    start_time = current_time_in_ms();
    for (int i = 0; i < XMAS_GRAD_LEN; ++i)
    {
        rgb2hsl(xmas_source.basic_source.gradient.colors[XMAS_GRAD_START + i], &grad_colors[i]);
    }
}

static int update_leds_gradient(ws2811_t* ledstrip)
{
    double time_shift = (double)(current_time_in_ms() - start_time) / config.gradient_speed;
    int length = 2 * XMAS_GRAD_LEN - 2;
    for (int led = 0; led < xmas_source.basic_source.n_leds; ++led)
    {
        double dindex = fabs(fmod(led + time_shift, length) - length / 2);
        int index = (int)dindex;
        double offset = dindex - index;
        if(index < XMAS_GRAD_LEN - 1)
        {
            hsl_t col_hsl;
            lerp_hsl(&grad_colors[index], &grad_colors[index + 1], offset, &col_hsl);
            ws2811_led_t c = hsl2rgb(&col_hsl);
#ifdef GAME_DEBUG
            int r = (int)((c & 0xFF0000) >> 16);
            int g = (int)((c & 0xFF00) >> 8);
            int b = (int)((c & 0xFF));
            if (b > r && b > g) {
                printf("Mixing %x and %x with offset %f gave %x (index %i)\n", xmas_source.basic_source.gradient.colors[XMAS_GRAD_START + index], xmas_source.basic_source.gradient.colors[XMAS_GRAD_START + index + 1], offset, c, index);
            }
#endif // GAME_DEBUG
            ledstrip->channel[0].leds[led] = c;
        }
        else
        {
            ledstrip->channel[0].leds[led] = hsl2rgb(&grad_colors[XMAS_GRAD_LEN - 1]);
        }
    }
    return 1;
}

#pragma endregion

#pragma region Pattern

int rings[200]; //n_leds

static void Pattern_init()
{
    for (int spring = 0; spring < geometry.n_springs; ++spring)
    {
        rings[geometry.springs[spring]] = 1;  //ring 0
    }
    int ring = 1;
    int up_found = 1;
    while (up_found)
    {
        up_found = 0;
        for (int led = 0; led < xmas_source.basic_source.n_leds; ++led)
        {
            if (rings[led] == ring && geometry.neighbors[led][UP] != -1)
            {
                rings[geometry.neighbors[led][UP]] = ring + 1;
                up_found = 1;
            }
        }
        ring++;
    }
    Gradient_init();
}

static int update_leds_pattern(ws2811_t* ledstrip)
{
    double time_shift = (double)(current_time_in_ms() - start_time) / config.gradient_speed;
    int length = 2 * XMAS_GRAD_LEN - 2;
    for (int led = 0; led < xmas_source.basic_source.n_leds; ++led)
    {
        int ring = rings[led];
        double dindex = fabs(fmod(ring + time_shift, length) - length / 2);
        int index = (int)dindex;
        double offset = dindex - index;
        if (index < XMAS_GRAD_LEN - 1)
        {
            hsl_t col_hsl;
            lerp_hsl(&grad_colors[index], &grad_colors[index + 1], offset, &col_hsl);
            ws2811_led_t c = hsl2rgb(&col_hsl);
            ledstrip->channel[0].leds[led] = c;
        }
        else
        {
            ledstrip->channel[0].leds[led] = hsl2rgb(&grad_colors[XMAS_GRAD_LEN - 1]);
        }
    }
    return 1;
}


/*
struct
{
    int* times;      //for every beat the shift to right, -1 means no light
    int* colors;    //for every led < pattern_length, index into gradient.colors
    int colors_length;
    int times_length;
} pattern_data;

int joy_pattern_colors[] = { 31,31,31,31,31, 32,32,32,32,32, 33,33,33,33,33, 34,34,34,34,34};
int joy_pattern_time[] = {0, 0, -1, 5, 5, -1, -1, 10, -1, 15, -1, 20, -1, -1, 25, -1, 30, -1, 35, -1, 40, -1, -1, 45, -1, 50, -1, -1};

//int sad_pattern_color[] = {};

static void Pattern_init()
{
    start_time = current_time_in_ms();
    pattern_data.colors = joy_pattern_colors;
    pattern_data.colors_length = sizeof(joy_pattern_colors)/sizeof(int);
    pattern_data.times = joy_pattern_time;
    pattern_data.times_length = sizeof(joy_pattern_time)/sizeof(int);
}

static int update_leds_pattern(ws2811_t* ledstrip)
{
    int beat = ((current_time_in_ms() - start_time) / config.beat_length) % pattern_data.times_length;
    for (int led = 0; led < xmas_source.basic_source.n_leds; ++led)
    {
        if (pattern_data.times[beat] == -1)
        {
            ledstrip->channel[0].leds[led] = 0x0;
        }
        else
        {
            int index = (led + pattern_data.times[beat]) % pattern_data.colors_length;
            ledstrip->channel[0].leds[led] = xmas_source.basic_source.gradient.colors[pattern_data.colors[index]];
        }
    }
    return 1;
}
*/
#pragma endregion

#pragma region Fireworks

static moving_led_t flares[255];
static int fireworks_gen = 0;
static const int firework_color_index = 35;
static const float flare_speed_at_1 = 21.; //leds per second
static const float deceleration_at_1 = 2.;  //leds per second squared
static const int wait_time_at_1 = 250; //in ms
static int flare_wait_time; //if > 0 we are waiting to spawn next gen, if < 0 wait is over, if == 0 we haven't started waiting yet
static ws2811_led_t* static_flares;

static void Fireworks_init()
{
    static_flares = malloc(sizeof(ws2811_led_t) * xmas_source.basic_source.n_leds);
    fireworks_gen = 0;
}

static void Fireworks_init_flare(int fi, int origin, dir_t direction, int destination, float speed)
{
    flares[fi].origin = origin;
    flares[fi].direction = direction;
    flares[fi].destination = destination;
    flares[fi].speed = speed;
    flares[fi].distance = 0;
    flares[fi].stop_at_destination = 0;
    flares[fi].is_moving = 1;
}

static void Firworks_spawn_next_gen()
{
    //printf("spawning new flare generation: %i\n", fireworks_gen + 1);
    int cur_gen_end_index = 1 << (fireworks_gen - 1);
    int reach = xmas_source.basic_source.n_leds / (2 << fireworks_gen);
    float flare_speed = flare_speed_at_1 / (float)(1 << fireworks_gen);
    for (int fi = 0; fi < cur_gen_end_index; ++fi)
    {
        int origin = flares[fi].origin;
        static_flares[origin] = xmas_source.basic_source.gradient.colors[firework_color_index + fireworks_gen - 1];
        Fireworks_init_flare(fi, origin, FORWARD, origin + reach, flare_speed);
        Fireworks_init_flare(cur_gen_end_index + fi, origin, BACKWARD, origin - reach, flare_speed);
    }
    fireworks_gen++;
}

static void Fireworks_update(ws2811_led_t* leds)
{
    if (fireworks_gen == 0)
    {
        for (int led = 0; led < xmas_source.basic_source.n_leds; ++led)
            static_flares[led] = 0x0;
        //spawn the first flare
        Fireworks_init_flare(0, 1, FORWARD, xmas_source.basic_source.n_leds / 2, flare_speed_at_1);
        fireworks_gen++;
        return;
    }
    int cur_gen_end_index = 1 << (fireworks_gen - 1);
    int flares_at_destination = 0;
    hsl_t color;
    float deceleration = deceleration_at_1 / (1 << (fireworks_gen - 1));
    rgb2hsl(xmas_source.basic_source.gradient.colors[firework_color_index + fireworks_gen - 1], &color);
    for (int fi = 0; fi < cur_gen_end_index; ++fi)
    {
        MovingLed_move(&flares[fi]);
        double time_seconds = (double)(xmas_source.basic_source.time_delta) / (double)1e9;
        flares[fi].speed -= deceleration * time_seconds;

        assert(flares[fi].origin >= 0 && flares[fi].origin < xmas_source.basic_source.n_leds);
        assert(flares[fi].next_stop >= -1 && flares[fi].next_stop < xmas_source.basic_source.n_leds);
        //pos = 5, offset = 0.3 => 5 gets 70%, 6 gets 30% when moving forward
        float at_origin, at_next_stop;
        MovingLed_get_intensity(&flares[fi], &at_origin, &at_next_stop);
        hsl_t res = color;
        res.l *= at_origin;
        leds[flares[fi].origin] = hsl2rgb(&res);
        if (flares[fi].next_stop != -1)
        {
            res.l = color.l * at_next_stop;
            leds[flares[fi].next_stop] = hsl2rgb(&res);
        }
        int dir = 2 * (flares[fi].direction == FORWARD) - 1; //+1 when moving forward, -1 when moving backward
        if ((dir * (flares[fi].destination - flares[fi].next_stop)) < 1) //we are approaching destination
        {
            flares[fi].stop_at_destination = 1;
        }
        if (!flares[fi].is_moving)
        {
            flares_at_destination++;
        }
        if (flares_at_destination == cur_gen_end_index)
        {
            assert(flare_wait_time < 1); //we should never get positive flare time, this is skipped directly
            if (flare_wait_time == 0)
            {
                flare_wait_time = wait_time_at_1 * (1 << (fireworks_gen - 1));
                //printf("setting wait for %i ms\n", flare_wait_time);
            }
            else
            {
                flare_wait_time = 0;
                if (fireworks_gen == 7)
                    fireworks_gen = 0;
                else
                    Firworks_spawn_next_gen();
            }
        }
    }
}

static int update_leds_fireworks(ws2811_t* ledstrip)
{
    if (flare_wait_time > 0)
    {
        flare_wait_time -= (int)((double)xmas_source.basic_source.time_delta / (double)1e6);
        if (flare_wait_time == 0) flare_wait_time--;
        //printf("Wait time remaining: %i\n", flare_wait_time);
        return 0;
    }
    for (int led = 0; led < xmas_source.basic_source.n_leds; ++led)
    {
        ledstrip->channel[0].leds[led] = static_flares[led];
    }
    Fireworks_update(ledstrip->channel[0].leds);
    return 1;
}

#pragma endregion

#pragma region Sledges

#define C_SLEDGES_TOTAL 16
#define C_TAIL_MAX 32
static moving_led_t sledges[C_SLEDGES_TOTAL];
static hsl_t sledge_colors[C_SLEDGES_TOTAL];
const double acc = 1.0;  //led/s^2

static void Sledges_init()
{
    for (int si = 0; si < C_SLEDGES_TOTAL; ++si)
    {
        sledges[si].origin = xmas_source.basic_source.n_leds - 1;
        sledges[si].direction = BACKWARD;
        sledges[si].destination = 0;
        sledges[si].speed = 0.0;
        sledges[si].distance = 0;
        sledges[si].stop_at_destination = 1;
        sledges[si].is_moving = 0;
        int i = random_01() * xmas_source.basic_source.gradient.n_colors;
        rgb2hsl(xmas_source.basic_source.gradient.colors[i], &sledge_colors[si]);
    }
    sledges[0].is_moving = 1;
}

/*!
 * @brief The idea is to spawn 16 "sledges" in regular intervals and then wait untill the last one gets to the bottom, wait and restart
*/
static void Sledges_update()
{
    double time_seconds = (double)(xmas_source.basic_source.time_delta) / (double)1e9;
    int moving_sledges = 0;
    for (int si = 0; si < C_SLEDGES_TOTAL; ++si)
    {
        if (sledges[si].is_moving)
        {
            sledges[si].speed += acc * time_seconds;
            MovingLed_move(&sledges[si]);
            if (si < C_SLEDGES_TOTAL - 1 && sledges[si + 1].is_moving == 0 && sledges[si].origin < xmas_source.basic_source.n_leds * (1.0 - 1.0 / 64.0))
            {
                sledges[si + 1].is_moving = 1;
            }
            moving_sledges++;
            //printf("si %i, origin: %i, stop: %i, dist: %f, speed: %f\n", si, sledges[si].origin, sledges[si].next_stop, sledges[si].distance, sledges[si].speed);
        }
    }
    if (moving_sledges == 0)
    {
        Sledges_init();
    }
}

static void Sledges_render(ws2811_t* ledstrip)
{
    for (int led = 0; led < xmas_source.basic_source.n_leds; ++led)
    {
        ledstrip->channel[0].leds[led] = 0x0;
    }
    hsl_t tail[C_TAIL_MAX + 1]; //+1 is for the last black led
    for (int si = 0; si < C_SLEDGES_TOTAL; ++si)
    {
        if (sledges[si].is_moving)
        {
            float tail_len = 1 + sledges[si].speed / 2.0f;
            if ((int)tail_len >= C_TAIL_MAX) tail_len = C_TAIL_MAX - 1;
            hsl_t black;
            black = sledge_colors[si];
            black.s *= 0.01;
            black.l *= 0.01;
            tail[0] = sledge_colors[si];
            for (int i = 1; i < (int)tail_len + 1; ++i)
            {
                tail[i] = tail[0];
                float aten = (tail_len - i)/tail_len;
                tail[i].l *= aten; 
                tail[i].s *= (0.5f + aten/2.0f);
            }
            float at_origin, at_next_stop;
            MovingLed_get_intensity(&sledges[si], &at_origin, &at_next_stop);
            hsl_t led_col;
            lerp_hsl(&black, &tail[0], at_next_stop, &led_col);
            ledstrip->channel[0].leds[sledges[si].next_stop] = hsl2rgb(&led_col);
            for (int i = 1; i < (int)tail_len + 1; ++i)
            {
                lerp_hsl(&tail[i - 1], &tail[i], at_next_stop, &led_col);
                ledstrip->channel[0].leds[sledges[si].next_stop + i] = hsl2rgb(&led_col);
            }
        }
    }
}


static int update_leds_sledges(ws2811_t* ledstrip)
{
    Sledges_update();
    Sledges_render(ledstrip);
    return 1;
}

#pragma endregion

#pragma region Valeria

static long valeria_start;
static int valeria_color_index = 42;

static void Valeria_init()
{
    valeria_start = current_time_in_ms();
}

static int update_leds_valeria(ws2811_t* ledstrip)
{
    int phase = 0;
    float left_dist = config.valeria_speed * (current_time_in_ms() - valeria_start) / 1000.0;
    while (left_dist > xmas_source.basic_source.n_leds)
    {
        left_dist -= xmas_source.basic_source.n_leds;
        phase++;
    }
    phase %= 4;
    float right_dist = (float)xmas_source.basic_source.n_leds - left_dist;
    //several options are possible:
    // Phase 0:
    // 0 < left_dist < right_dist < N -> left colour from 0 to left led, black from left to right, right colour from right to N
    // 0 < right_dist < left_dist < N -> left colour from 0 to right led, union colour from right to left, right colour from left to N
    // Phase 1:
    // 0 < left_dist < right_dist < N -> right colour from 0 to left led, union from left to right, left colour from right to N
    // 0 < right_dist < left_dist < N -> right colour from 0 to right led, black colour from right to left, left colour from left to N
    // Phase 2:
    // 0 < left_dist < right_dist < N -> right colour from 0 to left led, black from left to right, left colour from right to N
    // 0 < right_dist < left_dist < N -> right colour from 0 to right led, union colour from right to left, left colour from left to N
    // Phase 3:
    // 0 < left_dist < right_dist < N -> left colour from 0 to left led, union from left to right, right colour from right to N
    // 0 < right_dist < left_dist < N -> left colour from 0 to right led, black colour from right to left, right colour from left to N

    ws2811_led_t left_col, mid_col, right_col;
    left_col = (phase == 0 || phase == 3) ? xmas_source.basic_source.gradient.colors[valeria_color_index] : xmas_source.basic_source.gradient.colors[valeria_color_index + 1];
    right_col = (phase == 0 || phase == 3) ? xmas_source.basic_source.gradient.colors[valeria_color_index + 1] : xmas_source.basic_source.gradient.colors[valeria_color_index];
    mid_col = (((left_dist < right_dist) && (phase == 0 || phase == 2)) || ((left_dist > right_dist) && (phase == 1 || phase == 3))) ?
        0x0 : xmas_source.basic_source.gradient.colors[valeria_color_index + 2];
    int left_led, right_led;
    left_led = (left_dist < right_dist) ? left_dist : right_dist;
    right_led = (left_dist < right_dist) ? right_dist : left_dist;
    float t = left_led - (int)left_led;
    for (int led = 0; led < (int)left_led; ++led)
    {
        ledstrip->channel[0].leds[led] = left_col;
    }
    ledstrip->channel[0].leds[(int)left_led] = mix_rgb_color(left_col, mid_col, t);
    for (int led = (int)left_led + 1; led < (int)right_led; ++led)
    {
        ledstrip->channel[0].leds[led] = mid_col;
    }
    ledstrip->channel[0].leds[(int)right_led] = mix_rgb_color(right_col, mid_col, t);
    for (int led = (int)right_led + 1; led < xmas_source.basic_source.n_leds; ++led)
    {
        ledstrip->channel[0].leds[led] = right_col;
    }
    return 1;
}

#pragma endregion

#pragma region XmasSource

static int update_leds_debug(ws2811_t* ledstrip)
{
    if (xmas_source.first_update > 0)
    {
        return 0;
    }
    for (int led = 0; led < xmas_source.basic_source.n_leds; ++led)
    {
        ledstrip->channel[0].leds[led] = led == xmas_source.led_index ? xmas_source.basic_source.gradient.colors[0] : 0;
        //ledstrip->channel[0].leds[led] = led < (int)sizeof(xmas_source.basic_source.gradient.colors)/(int)sizeof(int) ? xmas_source.basic_source.gradient.colors[led] : 0;
    }
    xmas_source.first_update = 1;
    return 1;
}

XMAS_MODE_t string_to_xmas_mode(const char* txt)
{
    if (strcasecmp(txt, "debug") == 0)
        return XM_DEBUG;
    if (strcasecmp(txt, "snowflakes") == 0)
        return XM_SNOWFLAKES;
    if (strcasecmp(txt, "icicles") == 0)
        return XM_ICICLES;
    if (strcasecmp(txt, "glitter") == 0)
        return XM_GLITTER;
    if (strcasecmp(txt, "glitter2") == 0)
        return XM_GLITTER2;
    if (strcasecmp(txt, "gradient") == 0)
        return XM_GRADIENT;
    if (strcasecmp(txt, "joy_pattern") == 0)
        return XM_JOY_PATTERN;
    if (strcasecmp(txt, "fireworks") == 0)
        return XM_FIREWORKS;
    if (strcasecmp(txt, "sledges") == 0)
        return XM_SLEDGES;
    if (strcasecmp(txt, "valeria") == 0)
        return XM_VALERIA;
    return N_XMAS_MODES;
}

int XmasSource_process_config(const char* name, const char* value)
{
    if (strcasecmp(name, "n_snowflakes") == 0) {
        config.n_snowflakes = atoi(value);
        return 1;
    }
    if (strcasecmp(name, "k_diff") == 0) {
        config.k_diff = strtof(value, NULL);
        return 1;
    }
    if (strcasecmp(name, "k_spec") == 0) {
        config.k_spec = strtof(value, NULL);
        return 1;
    }
    if (strcasecmp(name, "spec_phase") == 0) {
        config.spec_phase = strtof(value, NULL);
        return 1;
    }
    if (strcasecmp(name, "spec") == 0) {
        config.spec = strtof(value, NULL);
        return 1;
    }
    if (strcasecmp(name, "snowflake_color") == 0) {
        config.snowflake_color = atoi(value);
        return 1;
    }
    if (strcasecmp(name, "move_chance") == 0) {
        config.move_chance = strtof(value, NULL);
        return 1;
    }
    if (strcasecmp(name, "diff_base_period") == 0) {
        config.diff_base_period = atol(value);
        return 1;
    }
    if (strcasecmp(name, "diff_period_range") == 0) {
        config.diff_period_range = atol(value);
        return 1;
    }
    if (strcasecmp(name, "spec_base_period") == 0) {
        config.spec_base_period = atol(value);
        return 1;
    }
    if (strcasecmp(name, "spec_period_range") == 0) {
        config.spec_period_range = atol(value);
        return 1;
    }
    if (strcasecmp(name, "down_chance") == 0) {
        config.down_chance = strtof(value, NULL);
        return 1;
    }
    if (strcasecmp(name, "left_chance") == 0) {
        config.left_chance = strtof(value, NULL);
        return 1;
    }
    //glitter1
    if (strcasecmp(name, "glt1_chance") == 0) {
        glt1_config.glitter_chance = strtof(value, NULL);
        return 1;
    }
    if (strcasecmp(name, "glt1_color") == 0) {
        glt1_config.color = atoi(value);
        return 1;
    }
    if (strcasecmp(name, "glt_green") == 0) {
        glt1_config.prob1 = strtof(value, NULL);
        return 1;
    }
    if (strcasecmp(name, "glt_red") == 0) {
        glt1_config.prob2 = strtof(value, NULL);
        return 1;
    }
    if (strcasecmp(name, "glt_orange") == 0) {
        glt1_config.prob3 = strtof(value, NULL);
        return 1;
    }
    if (strcasecmp(name, "glt_purple") == 0) {
        glt1_config.prob4 = strtof(value, NULL);
        return 1;
    }
    if (strcasecmp(name, "glt_blue") == 0) {
        glt1_config.prob5 = strtof(value, NULL);
        return 1;
    }
    if (strcasecmp(name, "glt1_phase_position") == 0) {
        glt1_config.phase_position = strtof(value, NULL);
        return 1;
    }
    if (strcasecmp(name, "glt1_phase_constant") == 0) {
        glt1_config.phase_constant = strtof(value, NULL);
        return 1;
    }
    if (strcasecmp(name, "glt1_phase_random") == 0) {
        glt1_config.phase_random = strtof(value, NULL);
        return 1;
    }
    if (strcasecmp(name, "glt1_amp_add") == 0) {
        glt1_config.amp_add = strtof(value, NULL);
        return 1;
    }
    if (strcasecmp(name, "glt1_amp_mul") == 0) {
        glt1_config.amp_mul = strtof(value, NULL);
        return 1;
    }
    if (strcasecmp(name, "glt1_base_period") == 0) {
        glt1_config.base_period = atol(value);
        return 1;
    }
    if (strcasecmp(name, "glt1_period_range") == 0) {
        glt1_config.period_range = atol(value);
        return 1;
    }
    //glitter2
    if (strcasecmp(name, "glt2_phase_position") == 0) {
        glt2_config.phase_position = strtof(value, NULL);
        return 1;
    }
    if (strcasecmp(name, "glt2_phase_constant") == 0) {
        glt2_config.phase_constant = strtof(value, NULL);
        return 1;
    }
    if (strcasecmp(name, "glt2_phase_random") == 0) {
        glt2_config.phase_random = strtof(value, NULL);
        return 1;
    }
    if (strcasecmp(name, "glt2_amp_add") == 0) {
        glt2_config.amp_add = strtof(value, NULL);
        return 1;
    }
    if (strcasecmp(name, "glt2_amp_mul") == 0) {
        glt2_config.amp_mul = strtof(value, NULL);
        return 1;
    }
    if (strcasecmp(name, "glt2_color") == 0) {
        glt2_config.color = atoi(value);
        return 1;
    }

    if (strcasecmp(name, "glt_sky") == 0) {
        glt2_config.prob1 = strtof(value, NULL);
        return 1;
    }
    if (strcasecmp(name, "glt_star") == 0) {
        glt2_config.prob2 = strtof(value, NULL);
        glt2_config.prob3 = 1.f;
        glt2_config.prob4 = 1.f;
        glt2_config.prob5 = 1.f;
        return 1;
    }
    if (strcasecmp(name, "glt2_chance") == 0) {
        glt2_config.glitter_chance = strtof(value, NULL);
        return 1;
    }
    if (strcasecmp(name, "glt2_base_period") == 0) {
        glt2_config.base_period = atol(value);
        return 1;
    }
    if (strcasecmp(name, "glt2_period_range") == 0) {
        glt2_config.period_range = atol(value);
        return 1;
    }    
    //icicles
    if (strcasecmp(name, "n_icicle_leds") == 0) {
        config.n_icicle_leds = atoi(value);
        return 1;
    }
    if (strcasecmp(name, "icicle_speed") == 0) {
        config.icicle_speed = strtof(value, NULL);
        return 1;
    }
    if (strcasecmp(name, "gradient_speed") == 0) {
        config.gradient_speed = atoi(value);
        return 1;
    }
    if (strcasecmp(name, "beat_length") == 0) {
        config.beat_length = atoi(value);
        return 1;
    }
    //valeria
    if (strcasecmp(name, "valeria_speed") == 0) {
        config.valeria_speed = strtof(value, NULL);
        return 1;
    }
    printf("Unknown config option %s with value %s\n", name, value);
    return 0;
}

static void random_mode();

//returns 1 if leds were updated, 0 if update is not necessary
int XmasSource_update_leds(int frame, ws2811_t* ledstrip)
{
    if (config.is_random && current_time_in_ms() - random_time_start > 300000l)
    {
        random_mode();
    }
    switch (xmas_source.mode)
    {
    case XM_SNOWFLAKES:
        return update_leds_snowflake(ledstrip);
    case XM_GLITTER:
    case XM_GLITTER2:
        return update_leds_glitter(ledstrip);
    case XM_ICICLES:
        return update_leds_icicles(ledstrip);
    case XM_GRADIENT:
        return update_leds_gradient(ledstrip);
    case XM_DEBUG:
        return update_leds_debug(ledstrip);
    case XM_JOY_PATTERN:
        return update_leds_pattern(ledstrip);
    case XM_FIREWORKS:
        return update_leds_fireworks(ledstrip);
    case XM_SLEDGES:
        return update_leds_sledges(ledstrip);
    case XM_VALERIA:
        return update_leds_valeria(ledstrip);
    case N_XMAS_MODES:
        printf("Invalid Xmas Source Mode in frame %d\n", frame);
        break;
    }
    return 0;
}

void XmasSource_destruct_current_mode()
{
    switch (xmas_source.mode)
    {
    case XM_DEBUG:
        break;
    case XM_GLITTER:
    case XM_GLITTER2:
        Glitter_destruct();
        break;
    case XM_ICICLES:
        free(icicle_colors);
        free(icicles);
        break;
    case XM_SNOWFLAKES:
        free(snowflakes);
        free(diff_data);
        free(spec_data);
        break;
    case XM_FIREWORKS:
        free(static_flares);
    case XM_GRADIENT:
    case XM_JOY_PATTERN:
    case XM_SLEDGES:
    case XM_VALERIA:
        break;
    case N_XMAS_MODES:
        break;
    }
}

void XmasSource_destruct()
{
    XmasSource_destruct_current_mode();
    free(geometry.neighbors);
    free(geometry.heads);
    free(geometry.springs);
}

void XmasSource_init_current_mode()
{
    switch (xmas_source.mode)
    {
    case XM_DEBUG:
        break;
    case XM_GLITTER:
        Glitter1_init();
        break;
    case XM_GLITTER2:
        Glitter2_init();
        break;
    case XM_ICICLES:
        Icicles_init();
        break;
    case XM_SNOWFLAKES:
        Snowflakes_init();
        break;
    case XM_GRADIENT:
        Gradient_init();
        break;
    case XM_JOY_PATTERN:
        Pattern_init();
        break;
    case XM_FIREWORKS:
        Fireworks_init();
        break;
    case XM_SLEDGES:
        Sledges_init();
        break;
    case XM_VALERIA:
        Valeria_init();
        break;
    case N_XMAS_MODES:
        break;
    }
}

static void random_mode()
{
    int mode = 1 + random_01() * (N_XMAS_MODES - 1); //mode 0 is debug, not very interesting
    XmasSource_destruct_current_mode();
    xmas_source.mode = (XMAS_MODE_t)mode;
    XmasSource_init_current_mode();
    xmas_source.first_update = 0;
    printf("Switched random mode in XmasSource to: %i\n", mode);
    config.is_random = 1;
    random_time_start = current_time_in_ms();
}

// The whole message is e.g. LED MSG MODE?GLITTER
// This function will only receive the part after LED MSG
void XmasSource_process_message(const char* msg)
{
    char* sep = strchr(msg, '?');
    if (sep == NULL)
    {
        printf("Message does not contain target %s\n", msg);
        return;
    }
    if ((sep - msg) >= 32)
    {
        printf("Target is too long or poorly formatted: %s\n", msg);
        return;
    }
    if ((strlen(sep + 1) >= 64))
    {
        printf("Message too long or poorly formatted: %s\n", msg);
        return;
    }
    char target[32];
    char payload[64];
    strncpy(target, msg, sep - msg);
    strncpy(payload, sep + 1, 64);
    target[sep - msg] = 0x0;
    payload[63] = 0x0;
    if (!strncasecmp(target, "MODE", 4))
    {
        XMAS_MODE_t mode = string_to_xmas_mode(payload);
        if (mode == N_XMAS_MODES)
        {
            //printf("Mode not recognized: %s\n", payload);
            random_mode();
            return;
        }
        XmasSource_destruct_current_mode();
        xmas_source.mode = mode;
        XmasSource_init_current_mode();
        xmas_source.first_update = 0;
        printf("Switched mode in XmasSource to: %s\n", payload);
        config.is_random = 0;
    }
    else if (!strncasecmp(target, "DEBUG", 5))
    {
        xmas_source.led_index = atoi(payload);
        xmas_source.first_update = 0;
        printf("Set new debug led to %i\n", xmas_source.led_index);
    }
    else
        printf("Unknown target: %s, payload was: %s\n", target, payload);
}

void XmasSource_init(int n_leds, int time_speed, uint64_t current_time)
{
    BasicSource_init(&xmas_source.basic_source, n_leds, time_speed, source_config.colors[XMAS_SOURCE], current_time);
    xmas_source.mode = XM_GLITTER;
    xmas_source.first_update = 0;
    XmasSource_read_geometry();
    XmasSource_init_current_mode();
}

void XmasSource_construct()
{
    BasicSource_construct(&xmas_source.basic_source);
    xmas_source.basic_source.init = XmasSource_init;
    xmas_source.basic_source.update = XmasSource_update_leds;
    xmas_source.basic_source.destruct = XmasSource_destruct;
    xmas_source.basic_source.process_message = XmasSource_process_message;
    xmas_source.basic_source.process_config = XmasSource_process_config;
}

XmasSource xmas_source = {
    .basic_source.construct = XmasSource_construct,
    .first_update = 0,
    .led_index = 0
};

#pragma endregion

#pragma GCC diagnostic pop
