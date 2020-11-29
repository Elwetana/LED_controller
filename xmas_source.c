#define _CRT_SECURE_NO_WARNINGS
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunknown-pragmas"

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

#pragma endregion

#pragma region Common

typedef struct PeriodData {
    //all are times in ms
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
double get_angle(period_data_t* period_data)
{
    long cur_time = xmas_source.basic_source.current_time / (long)1e6;
    if (cur_time >= period_data->nextChange)
    {
        period_data->lastChange = period_data->nextChange;
        period_data->nextChange = cur_time + period_data->basePeriod + (long)((random_01() - 0.5f) * period_data->periodRange);
    }
    return 2 * M_PI * (double)(cur_time - period_data->lastChange) / (double)(period_data->nextChange - period_data->lastChange);
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

#define C_N_SNOWFLAKES 5
static moving_led_t snowflakes[C_N_SNOWFLAKES];
static period_data_t diff_data[C_N_SNOWFLAKES];
static period_data_t spec_data[C_N_SNOWFLAKES];
const int hue = 210;
const double sat = 0.8;
const double lgt = 0.5;
const double k_diff = 0.5 / 2.;
const double k_spec = 0.5 / 1.5;
const double spec_phase = M_PI / 2;
const double spec = 20;

const double move_chance = 0.01; //chance per frame

void Snowflakes_init()
{
    int d = (int)(xmas_source.basic_source.n_leds / C_N_SNOWFLAKES);
    for (int flake = 0; flake < C_N_SNOWFLAKES; ++flake)
    {
        diff_data[flake].basePeriod  =  20000;
        diff_data[flake].periodRange =   7500;
        spec_data[flake].basePeriod  =  30000;
        spec_data[flake].periodRange =   2000;

        snowflakes[flake].origin = flake * d;
        snowflakes[flake].speed = 0.5f;
        snowflakes[flake].distance = 0;
        snowflakes[flake].is_moving = 0;
        snowflakes[flake].stop_at_destination = 1;
    }
}

void Snowflakes_update()
{
    for (int flake = 0; flake < C_N_SNOWFLAKES; flake++)
    {
        if(snowflakes[flake].is_moving)
        {
            MovingLed_move(&snowflakes[flake]);
            continue;
        }
        if (random_01() < move_chance)
        {
            //printf("Moving snowflake number %d\n", flake);
            //now we need to generate direction, let's say there is 50% chance of going down
            float r01 = random_01();
            int dir = (r01 < 0.999f) ? DOWN : (r01 < 0.75f) ? LEFT : RIGHT;
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
    for (int flake = 0; flake < C_N_SNOWFLAKES; ++flake)
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
            flake_leds[1 + i] = geometry.neighbors[flake_leds[0]][(dir + 2 * i) % 8];
        }
        if (snowflakes[flake].is_moving)
        {
            flake_leds[5] = geometry.neighbors[flake_leds[1]][dir];
            flake_leds[6] = geometry.neighbors[flake_leds[1]][(dir + 2) % 8];
            flake_leds[7] = geometry.neighbors[flake_leds[1]][(dir + 6) % 8];
        }

        float origin_intensity, dest_intensity;
        MovingLed_get_intensity(&snowflakes[flake], &origin_intensity, &dest_intensity);

        double diff_alpha = get_angle(&diff_data[flake]);
        double spec_alpha = get_angle(&spec_data[flake]);
        float hsl[3];
        hsl[0] = hue / 360.f;
        hsl[1] = (float)sat;
        double l = lgt + k_diff * cos(diff_alpha) + k_spec * pow(cos(spec_alpha + spec_phase), spec);
        l = lgt;
        if (l > 1) l = 1;
        float l_origin = (float)l * origin_intensity + 0.5 * (float)l * dest_intensity;
        float l_destin = (float)l * dest_intensity + 0.5 * (float)l * origin_intensity;

        //now set all leds
        hsl[2] = l_origin;
        ledstrip->channel[0].leds[flake_leds[0]] = hsl2rgb(hsl);
        hsl[2] = l_destin;
        if(flake_leds[1] != -1) ledstrip->channel[0].leds[flake_leds[1]] = hsl2rgb(hsl);
        /*hsl[2] = (float)pow(l_origin, 2) * 0.5;
        for (int i = 2; i < 5; ++i)
        {
            if (flake_leds[i] != -1) ledstrip->channel[0].leds[flake_leds[i]] = hsl2rgb(hsl);
        }*/
        hsl[2] = (float)pow(l_destin, 2) * 0.5;
        if (flake_leds[5] != -1) ledstrip->channel[0].leds[flake_leds[5]] = hsl2rgb(hsl);
        /*for (int i = 5; i < 8; ++i)
        {
            if (flake_leds[i] != -1) ledstrip->channel[0].leds[flake_leds[i]] = hsl2rgb(hsl);
        }*/
    }
    return 1;
}

#pragma endregion

#pragma region Glitter

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

#pragma endregion

#pragma region Icicles

static moving_led_t* icicles;
static const int n_icicle_leds = 4;
static float icicle_colors[6][3]; // 0 and 5 are black before and after, the rest is { 6, 7, 8, 9 } in config

static void Icicles_init()
{
    icicles = malloc(sizeof(moving_led_t) * geometry.n_heads);
    for (int i = 0; i < geometry.n_heads; ++i)
    {
        icicles[i].origin = geometry.heads[i];
        icicles[i].direction = DOWN;
        icicles[i].distance = 0;
        icicles[i].speed = 5.f;
        icicles[i].stop_at_destination = 0;
        icicles[i].is_moving = 1;
    }
    for(int i = 0; i < n_icicle_leds; ++i)
    {
        rgb2hsl(xmas_source.basic_source.gradient.colors[6+i], icicle_colors[i+1]);
    }
    for(int i = 0; i < 3; ++i)
    {
        icicle_colors[0][i] = icicle_colors[1][i];
        icicle_colors[n_icicle_leds + 1][i] = icicle_colors[n_icicle_leds][i];
    }
    icicle_colors[0][2] = 0.f;
    icicle_colors[n_icicle_leds + 1][2] = 0.f;
}

//  Let's assume that origin/destination split in 0th led is 0.3/0.7
//  Led Weight  Result
//   0    1      0.3
//   1    0.5    0.7 + 0.5 * 0.3
//   2    0.25   0.5 * 0.7 + 0.25 * 0.3
//  etc.
static int update_leds_icicles(ws2811_t* ledstrip)
{
    for (int led = 0; led < xmas_source.basic_source.n_leds; ++led)
    {
        ledstrip->channel[0].leds[led] = 0;
    }
    for (int i = 0; i < geometry.n_heads; ++i)
    {
        MovingLed_move(&icicles[i]);
        if (icicles[i].is_moving == 0)
        {
            icicles[i].origin = geometry.heads[i];
            icicles[i].is_moving = 1;
        }
        float origin_intensity, destination_intensity;
        MovingLed_get_intensity(&icicles[i], &origin_intensity, &destination_intensity);
        int led = icicles[i].origin;
        float hsl_out[3];
        lerp_hsl(icicle_colors[0], icicle_colors[1], origin_intensity, hsl_out);
        ledstrip->channel[0].leds[led] = hsl2rgb(hsl_out);
        for (int ice_led = 0; ice_led < n_icicle_leds; ++ice_led)
        {
            led = geometry.neighbors[led][icicles[i].direction];
            if (led == -1)
                break;
            lerp_hsl(icicle_colors[ice_led+1], icicle_colors[ice_led+2], origin_intensity, hsl_out);
            ledstrip->channel[0].leds[led] = hsl2rgb(hsl_out);
            //if(i == 0) printf("Ice led %i: %f\n", led, hsl[2]);
        }
        //printf("Updated %d led with intensity %f\n", icicles[i].led.origin, origin_intensity);
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
        return update_leds_snowflake(ledstrip);
    case XM_GLITTER:
        return update_leds_glitter(ledstrip);
    case XM_ICICLES:
        return update_leds_icicles(ledstrip);
    case XM_DEBUG:
        return update_leds_debug(ledstrip);
    case N_XMAS_MODES:
        printf("Invalid Xmas Source Mode in frame %d\n", frame);
        break;
    }
    return 0;
}

void XmasSource_destruct()
{
    free(geometry.neighbors);
    free(geometry.heads);
    free(geometry.springs);
    free(icicles);
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
    Icicles_init();
}

XmasSource xmas_source = {
    .basic_source.init = XmasSource_init,
    .first_update = 0,
    .led_index = 0
};

#pragma endregion

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
#pragma GCC diagnostic pop
