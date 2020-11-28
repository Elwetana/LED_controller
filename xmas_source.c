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
#include <Xinput.h>
#pragma comment(lib, "XInput.lib")
#endif // __linux__

#include "common_source.h"
#include "colours.h"
#include "xmas_source.h"


//Common functions {{{ 

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
struct SGeometry {
    int (*neighbors)[HEIGHT + 1];
    int* heads;
    int* springs;
    int n_heads;
    int n_springs;
};

static struct SGeometry geometry;

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

static void XmasSource_read_geometry()
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
        fscanf(fgeom, "%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\n",
            &geometry.neighbors[row][UP], &geometry.neighbors[row][UP + 1],
            &geometry.neighbors[row][RIGHT], &geometry.neighbors[row][RIGHT + 1],
            &geometry.neighbors[row][DOWN], &geometry.neighbors[row][DOWN + 1],
            &geometry.neighbors[row][LEFT], &geometry.neighbors[row][LEFT + 1]);
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
}

// Geometry}}}

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

typedef struct MovingLed {
    int origin;     // index of the LED where the motion started
    float distance; // distance from origin, 0 to 1
    dir_t direction;
    float speed;    // in leds per second
    int stop_at_destination; // 0 - continue movement, 1 stop at destination
    int is_moving;  // 0 - not moving, 1 - is moving
} moving_led_t;

void MovingLed_move(moving_led_t* moving_led)
{
    if (moving_led->is_moving == 0)
        return;
    double time_seconds = (xmas_source.basic_source.time_delta / (long)1e3) / (double)1e6;
    moving_led->distance += moving_led->speed * time_seconds;
    if (moving_led->distance >= 1.0f)
    {
        moving_led->distance -= 1;
        if (moving_led->distance > 1.0f) //this should never happen, it means that we have travelled more than one led distance in one frame
        {
            moving_led->distance = 0.9f;
        }
        moving_led->origin = geometry.neighbors[moving_led->origin][moving_led->direction];
        if ((geometry.neighbors[moving_led->origin][moving_led->direction] == -1) // we cannot keep moving in this direction
            || moving_led->stop_at_destination)                                   // or we are supposed to stop at destination
        {
            moving_led->distance = 0.0f;
            moving_led->is_moving = 0;
        }
    }
}

int MovingLed_get_intensity(moving_led_t* moving_led, float* at_origin, float* at_destination)
{
    *at_origin = (1 - moving_led->distance);
    *at_destination = moving_led->distance;
    return 1;
}

//}}}


//Snowflakes {{{
#define C_N_SNOWFLAKES 5
static int snowflakes[] = { 10, 30, 50, 70, 90 };
static period_data_t diff_data[C_N_SNOWFLAKES];
static period_data_t spec_data[C_N_SNOWFLAKES];
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
            if ((geometry.neighbors[snowflakes[flake]][dir] == -1) || (geometry.neighbors[snowflakes[flake]][dir + 1] != 1))
            {
                dir = DOWN; //if we can't move in the desired direction, we can try to go DOWN
            }
            if (geometry.neighbors[snowflakes[flake]][dir] != -1)
            {
                snowflakes[flake] = geometry.neighbors[snowflakes[flake]][dir];
            }
            else
            {
                //we cannot move this snowflake any further, so we shall spawn a new one
                int new_flake = (int)(random_01() * geometry.n_heads);
                snowflakes[flake] = geometry.heads[new_flake];
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
            if ((geometry.neighbors[led][dir] != -1) && (geometry.neighbors[led][dir + 1] == 1))
            {
                int neighbor = geometry.neighbors[led][dir];
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

//Icicles {{{

typedef struct Icicle
{
    moving_led_t led;
    float hsl[3];
} icicle_t;

#define C_N_ICICLES 1
static icicle_t icicles[C_N_ICICLES];

static void Icicles_init()
{
    for (int i = 0; i < C_N_ICICLES; ++i)
    {
        icicles[i].led.origin = 0;
        icicles[i].led.direction = FORWARD;
        icicles[i].led.distance = 0;
        icicles[i].led.speed = 1.0f;
        icicles[i].led.stop_at_destination = 0;
        icicles[i].led.is_moving = 1;
        icicles[i].hsl[0] = 210.f / 360.f;
        icicles[i].hsl[1] = 0.8f;
        icicles[i].hsl[2] = 0.5f;
    }
}

static int update_leds_icicles(ws2811_t* ledstrip)
{
    for (int i = 0; i < C_N_ICICLES; ++i)
    {
        MovingLed_move(&icicles[i].led);
        float origin_intensity, destination_intensity;
        MovingLed_get_intensity(&icicles[i].led, &origin_intensity, &destination_intensity);
        float hsl[3];
        hsl[0] = icicles[i].hsl[0];
        hsl[1] = icicles[i].hsl[1];
        hsl[0] = icicles[i].hsl[2] * origin_intensity;
        ledstrip->channel[0].leds[icicles[i].led.origin] = hsl2rgb(hsl);
        hsl[0] = icicles[i].hsl[2] * destination_intensity;
        ledstrip->channel[0].leds[geometry.neighbors[icicles[i].led.origin][icicles[i].led.direction]] = hsl2rgb(hsl);
        //printf("Updated %d led with intensity %f\n", icicles[i].led.origin, origin_intensity);
    }

    return 1;
}

//Icicles }}}

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
    case XM_ICICLES:
        return update_leds_icicles(ledstrip);
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
    free(geometry.neighbors);
    free(geometry.heads);
    free(geometry.springs);
    printf("Geometry freed\n");
}

void XmasSource_init(int n_leds, int time_speed)
{
    BasicSource_init(&xmas_source.basic_source, n_leds, time_speed, source_config.colors[XMAS_SOURCE]);
    xmas_source.basic_source.update = XmasSource_update_leds;
    xmas_source.basic_source.destruct = XmasSource_destruct;
    xmas_source.basic_source.process_message = XmasSource_process_message;
    xmas_source.mode = XM_ICICLES;
    xmas_source.first_update = 0;
    XmasSource_read_geometry();
    Snowflakes_init();
    Icicles_init();
}

XmasSource xmas_source = {
    .basic_source.init = XmasSource_init,
    .first_update = 0,
    .led_index = 0
};


/*
    XINPUT_STATE state;
    DWORD dwUserIndex = 0;
    while (1)
    {
        DWORD res = XInputGetState(dwUserIndex, &state);
        if(state.Gamepad.wButtons != 0)
            printf("Res: %d, buttons: %d\n", res, state.Gamepad.wButtons);

    }


*/
