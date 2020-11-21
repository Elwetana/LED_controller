#define _CRT_SECURE_NO_WARNINGS

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#ifdef __linux__
#include "ws2811.h"
#else
#include "fakeled.h"
#include "faketime.h"
#endif // __linux__

#include "common_source.h"
#include "colours.h"
#include "xmas_source.h"


typedef enum dir {
    UP,
    RIGHT = 2,
    DOWN = 4,
    LEFT = 6,
    HEIGHT = 8
} dir_t;


//Common functions {{{ 

typedef struct PeriodData {
    long lastChange;
    long nextChange;
    long basePeriod;
    long periodRange;
} period_data_t;

/**
* Generate linearly increasing number from 0 to 2 * pi, with some random variation
* The length of the period is within interval basePeriod +- 0.5 * periodRange
* Once 2*pi angle is reached, new period is calculated
*/
double get_angle(period_data_t* period_data, long frame)
{
    if (frame >= period_data->nextChange)
    {
        period_data->lastChange = period_data->nextChange;
        period_data->nextChange = frame + period_data->basePeriod + (long)((random_01() - 0.5f) * period_data->periodRange);
    }
    return 2 * M_PI * (double)(frame - period_data->lastChange) / (double)(period_data->nextChange - period_data->lastChange);
}

//}}}


//Snowflakes {{{
#define C_N_SNOWFLAKES 5
int snowflakes[] = { 10, 30, 50, 70, 90 };
period_data_t diff_data[C_N_SNOWFLAKES];
period_data_t spec_data[C_N_SNOWFLAKES];
const int hue = 210;
const double sat = 0.8;
const double lgt = 0.5;
const double k_diff = 0.5 / 2.;
const double k_spec = 0.5 / 1.5;
const double spec_phase = M_PI / 2;
const double spec = 20;

const double move_chance = 0.001; //chance per frame

void Snowflakes_init()
{
    for (int flake = 0; flake < C_N_SNOWFLAKES; ++flake)
    {
        diff_data[flake].basePeriod  = 750;
        diff_data[flake].periodRange = 250;
        spec_data[flake].basePeriod = 1000;
        spec_data[flake].periodRange = 300;
    }
}

void Snowflakes_update()
{
    for (int flake = 0; flake < C_N_SNOWFLAKES; flake++)
    {
        if (random_01() < move_chance)
        {
            //printf("Moving snowflake number %d\n", flake);
            //now we need to generate direction, let's say there is 50% chance of going down
            float r01 = random_01();
            int dir = (r01 < 0.5f) ? DOWN : (r01 < 0.75f) ? LEFT : RIGHT;
            //check if there is a led in this direction
            if ((xmas_source.geometry[snowflakes[flake]][dir] == -1) || (xmas_source.geometry[snowflakes[flake]][dir + 1] != 1))
            {
                dir = DOWN; //if we can't move in the desired direction, we can try to go DOWN
            }
            if (xmas_source.geometry[snowflakes[flake]][dir] != -1)
            {
                snowflakes[flake] = xmas_source.geometry[snowflakes[flake]][dir];
            }
            else
            {
                //we cannot move this snowflake any further, so we shall spawn a new one
                int new_flake = (int)(random_01() * xmas_source.n_heads);
                snowflakes[flake] = xmas_source.heads[new_flake];
                //printf("Spawning new flake at %d\n", xmas_source.springs[new_flake]);
            }
        }
    }
}

static int update_leds_snowflake(int frame, ws2811_t* ledstrip)
{
    for (int led = 0; led < xmas_source.basic_source.n_leds; ++led)
    {
        ledstrip->channel[0].leds[led] = 0;
    }
    for (int flake = 0; flake < C_N_SNOWFLAKES; ++flake)
    {
        int led = snowflakes[flake];

        double diff_alpha = get_angle(&diff_data[flake], frame);
        double spec_alpha = get_angle(&spec_data[flake], frame);
        float hsl[3];
        hsl[0] = hue / 360.f;
        hsl[1] = (float)sat;
        double l = lgt + k_diff * cos(diff_alpha) + k_spec * pow(cos(spec_alpha + spec_phase), spec);
        if (l > 1) l = 1;
        hsl[2] = (float)l;

        ledstrip->channel[0].leds[led] = hsl2rgb(hsl);
        for (int dir = 0; dir < 8; dir += 2)
        {
            if ((xmas_source.geometry[led][dir] != -1) && (xmas_source.geometry[led][dir + 1] == 1))
            {
                int neighbor = xmas_source.geometry[led][dir];
                hsl[2] = 0.5 * (float)pow(l, 2);
                ledstrip->channel[0].leds[neighbor] = hsl2rgb(hsl);
            }
        }
    }
    Snowflakes_update();
    return 1;
}

//Snowflakes }}}


//Glitter {{{

const float glitter_chance = 0.0055555;

static int select_glitter_color()
{
    //1 - green 30% , 2 -- red 30%, 3 -- bright orange 10%, 4 -- purple 10%, 5 -- blue 20%
    float r01 = random_01();
    if(r01 < 0.30f) return 1;
    if(r01 < 0.60f) return 2;
    if(r01 < 0.70f) return 3;
    if(r01 < 0.80f) return 4;
    return 5;
}

static int update_leds_glitter(ws2811_t* ledstrip)
{
    if (xmas_source.first_update == 0)
    {
        //for the first update, we set every led randomly to one of the five glitter colours -- these are gradient colours 1-5
        for (int led = 0; led < xmas_source.basic_source.n_leds; ++led)
        {
            int col = select_glitter_color();
            ledstrip->channel[0].leds[led] = xmas_source.basic_source.gradient.colors[col];
            printf("Setting led %d to color %x\n", led, xmas_source.basic_source.gradient.colors[col]);
        }
        xmas_source.first_update = 1;
        return 1;
    }
    else
    {
        //in all subsequent updates there is a chance that exactly one led will be set to new colour (or possibly the same colour)
        if (random_01() < glitter_chance)
        {
            int led = (int)(random_01() * xmas_source.basic_source.n_leds);
            int col = select_glitter_color();
            ledstrip->channel[0].leds[led] = xmas_source.basic_source.gradient.colors[col];
            printf("Resetting led %d to color %x\n", led, xmas_source.basic_source.gradient.colors[col]);
            return 1;
        }
    }
    return 0;
}

//Glitter }}}

static int update_leds_debug(ws2811_t* ledstrip)
{
    if (xmas_source.first_update > 0)
    {
        return 0;
    }
    for (int led = 0; led < xmas_source.basic_source.n_leds; ++led)
    {
        ledstrip->channel[0].leds[led] = led == xmas_source.led_index ? xmas_source.basic_source.gradient.colors[0] : 0;
    }
    xmas_source.first_update = 1;
    return 1;
}

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
        (xmas_source.geometry[led][flow_from] != -1) &&  //there is something in the direction of flow
        (xmas_source.geometry[xmas_source.geometry[led][flow_from]][flow_to] == led) && //I am upstream from the led in the direction of flow
        ((xmas_source.geometry[led][flow_to] == -1) || (xmas_source.geometry[xmas_source.geometry[led][flow_to]][flow_from] != led)) //I am either end of flow, or the led upstream from me does not flow back to me
        );
}

static void find_heads_and_springs()
{
    xmas_source.heads = malloc(sizeof(int) * xmas_source.basic_source.n_leds / 2); //there can never be that many heads or springs, but I don't care about saving few bytes of memory
    xmas_source.springs = malloc(sizeof(int) * xmas_source.basic_source.n_leds / 2);
    xmas_source.n_heads = 0;
    xmas_source.n_springs = 0;
    for (int led = 0; led < xmas_source.basic_source.n_leds; ++led)
    {
        if(check_terminal(led, DOWN, UP))
        {
            xmas_source.heads[xmas_source.n_heads++] = led;
        }
        if (check_terminal(led, UP, DOWN))
        {
            xmas_source.springs[xmas_source.n_springs++] = led;
        }
    }
    //debug output
    printf("HEADS: ");
    for (int i = 0; i < xmas_source.n_heads; ++i) printf("%d, ", xmas_source.heads[i]);
    printf("\nSPRINGS: ");
    for (int i = 0; i < xmas_source.n_springs; ++i) printf("%d, ", xmas_source.springs[i]);
    printf("\n");
}


static void calculate_height()
{
     for (int i = 0; i < xmas_source.basic_source.n_leds; ++i)
    {
        if (xmas_source.geometry[i][HEIGHT] != -1) //we have been here already
            continue;

        int down = xmas_source.geometry[i][DOWN];
        int depth = 0;
        while (down != -1 && xmas_source.geometry[down][HEIGHT] == -1)
        {
            depth++;
            down = xmas_source.geometry[down][DOWN];
        }
        if (down != -1) //we are not at the bottom but we were here already
        {
            depth += xmas_source.geometry[down][HEIGHT] + 1;
        }
        xmas_source.geometry[i][HEIGHT] = depth;
        down = xmas_source.geometry[i][DOWN];
        while (down != -1 && xmas_source.geometry[down][HEIGHT] == -1)
        {
            xmas_source.geometry[down][HEIGHT] = --depth;
            down = xmas_source.geometry[down][DOWN];
        }
    }
}

static void XmasSource_read_geometry()
{
    xmas_source.geometry = malloc(xmas_source.basic_source.n_leds * sizeof(*xmas_source.geometry));
    FILE* fgeom = fopen("geometry", "r");
    if (fgeom == NULL) {
        printf("Geometry file not found\n");
        exit(-4);
    }
    int row = 0;
    while (row < xmas_source.basic_source.n_leds)
    {
        fscanf(fgeom, "%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\n", 
            &xmas_source.geometry[row][UP], &xmas_source.geometry[row][UP+1],
            &xmas_source.geometry[row][RIGHT], &xmas_source.geometry[row][RIGHT+1],
            &xmas_source.geometry[row][DOWN], &xmas_source.geometry[row][DOWN+1],
            &xmas_source.geometry[row][LEFT], &xmas_source.geometry[row][LEFT+1]);
        xmas_source.geometry[row][HEIGHT] = -1;
        row++;
    }
    /* //debug print line 90
    printf("Row 90: %d %d - %d %d - %d %d - %d %d\n", xmas_source.geometry[90][UP], xmas_source.geometry[90][UP+1],
            xmas_source.geometry[90][RIGHT], xmas_source.geometry[90][RIGHT+1],
            xmas_source.geometry[90][DOWN], xmas_source.geometry[90][DOWN+1],
            xmas_source.geometry[90][LEFT], xmas_source.geometry[90][LEFT+1]);
    */
    calculate_height();
    find_heads_and_springs();
}

// Geometry}}}

// The whole message is e.g. LED MSG MORSETEXT?HI%20URSULA
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
    if (!strncasecmp(target, "MORSETEXT", 9))
    {
        xmas_source.led_index = atoi(payload);
        xmas_source.first_update = 0;
        printf("Debug info: %s\n", payload);
    }
    else if (!strncasecmp(target, "MORSEMODE", 9))
    {
        //int mode = atoi(payload);
        //MorseSource_change_mode(mode);
        //printf("Setting new MorseSource mode: %i\n", mode);
    }
    else
        printf("Unknown target: %s, payload was: %s\n", target, payload);
}

//returns 1 if leds were updated, 0 if update is not necessary
int XmasSource_update_leds(int frame, ws2811_t* ledstrip)
{
    switch (xmas_source.mode)
    {
    case XM_SNOWFLAKES:
        return update_leds_snowflake(frame, ledstrip);
    case XM_GLITTER:
        return update_leds_glitter(ledstrip);
    case XM_DEBUG:
        return update_leds_debug(ledstrip);
    case N_XMAS_MODES:
        printf("Invalid Xmas Source Mode\n");
        break;
    }
    return 0;
}

void XmasSource_destruct()
{
    free(xmas_source.geometry);
    free(xmas_source.heads);
    free(xmas_source.springs);
}

void XmasSource_init(int n_leds, int time_speed)
{
    BasicSource_init(&xmas_source.basic_source, n_leds, time_speed, source_config.colors[XMAS_SOURCE]);
    xmas_source.basic_source.update = XmasSource_update_leds;
    xmas_source.basic_source.destruct = XmasSource_destruct;
    xmas_source.basic_source.process_message = XmasSource_process_message;
    xmas_source.mode = XM_SNOWFLAKES;
    xmas_source.first_update = 0;
    XmasSource_read_geometry();
    Snowflakes_init();
}

XmasSource xmas_source = {
    .basic_source.init = XmasSource_init,
    .first_update = 0,
    .led_index = 0
};
