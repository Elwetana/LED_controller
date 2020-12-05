#define _CRT_SECURE_NO_WARNINGS

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

#include "common_source.h"
#include "game_source.h"
#include "controller.h"
#include "colours.h"

//returns +1 if a > b, -1 if a < b and 0 if a == b
#define SGN(a,b)  ((a) > (b)) - ((a) < (b))

#define MAX_N_OBJECTS     256
#define MAX_OBJECT_LENGTH  16

typedef enum EDirection
{
    D_FORWARD, //to higher numbers
    D_BACKWARD //to lower numbers
} dir_t;


/*! Basic object that has position, length and speed
 *  the object is rendered from position in the direction, i.e. it's pushed */
typedef struct MovingObject
{
    double position;  //!< index of the leftmost LED, `position` + `length` - 1 is the index of the rightmost LED
    int length;
    double speed;
    int target;
    int zdepth;
    ws2811_led_t color[MAX_OBJECT_LENGTH];  //!< must be initialized with `length` colors, color[0] is tail, color[length-1] is head
    int deleted;
    void(*on_arrival)(struct MovingObject*);
} moving_object_t;

typedef struct CanvasPixel
{
    //ws2811_led_t color;
    int zbuffer;
    int stencil;
} pixel_t;

static pixel_t* canvas;
static moving_object_t objects[MAX_N_OBJECTS];
static int n_objects = 0;


//====== Stencil and Collisions =======

static void MovingObject_stencil_test()
{
    //canvas[object->position].stencil = stencil_index;

}

//========= Move and Render ===========

//********* Arrival methods ***********
static void MovingObject_arrive_delete(moving_object_t* object)
{
    object->deleted = 1;
}

static void MovingObject_arrive_stop(moving_object_t* object)
{
    object->speed = 0.;
}

//******* Arrival methods end ***********

/*! 
* \param color  RGB color to render
* \param alpha  float 0 to 1
* \param leds   target array of RGB values
* \param led    index to `leds` array
* \param zdepth z-depth into which we are rendering. Z-depth is distance from 
*               camera, so lower z overwrites higher z. If the z-depths are 
*               equal, colors will be added. 
*/
static void render_with_z_test(ws2811_led_t color, double alpha, ws2811_led_t* leds, int led, int zdepth)
{
    assert(led >= 0 && led < game_source.basic_source.n_leds);
    if (canvas[led].zbuffer < zdepth)
    {
        return;
    }
    if (canvas[led].zbuffer > zdepth)
    {
        leds[led] = multiply_rgb_color(color, alpha);
        canvas[led].zbuffer = zdepth;
        return;
    }
    //now we know that canvas[led].zbuffer == zdepth
    int otherColor = leds[led];
    leds[led] = alpha_blend_rgb(color, otherColor, alpha);
}

/*!
* Move and render the object
* \param stencil_index  will be written to stencil. 
* \param render_path    1 -- all intermediate leds will be lit, 0 -- only the end position will be rendered
* \return               0 when the object arrives at _target_, 1 otherwise
*/
static int MovingObject_process(moving_object_t* object, int stencil_index, ws2811_led_t* leds, int render_path)
{
    if(object->deleted)
        return 0;
    double time_seconds = (game_source.basic_source.time_delta / (long)1e3) / (double)1e6;
    double distance = object->speed * time_seconds;
    int dir = SGN(object->target, object->position);
    int led;
    double offset;
    /*
    * How the render_path == 1 works:
    * speed = 3.5, time = 1, position = 7, offset = 0.3
    *     -> distance = 3.5, result position = 10.8, leds: (7) 0.7 - (8) 1 - (9) 1 - (10) 1 - (11) 0.8
    * speed = 0.5, time = 1, position = 7, offset = 0.3
    *     -> distance = 0.5, result position = 7.8, leds: (7) 0.7 - (8) 0.8
    * speed = 0.5, time = 1, position = 7, offset = 8
    *     -> distance = 0.5, result position = 8.3, leds: (7) 0.2 - (8) 1 - (9) 0.3
    * speed = 1, time = 1, position = 7, offset = 0
    *     -> distance = 1, result position = 8, leds: (7) 1 - (8) 1 - (9) 0
    */
    if (render_path && object->length > 0)
    {
        //if moving right, color starting led with alpha = 1 - offset, if moving left, color starting led + 1 with alpha = offset
        led = (int)object->position + (dir < 0);
        assert(led >= 0. && led < game_source.basic_source.n_leds);
        offset = dir * (object->position - led); //this will be always positive and it is the distance toward starting led
        render_with_z_test(object->color[0], 1. - offset, leds, led, object->zdepth);

        //color the leds between start and end
        while ((offset + distance) >= 1.) //we have visited more than one LED
        {
            led += dir;
            distance -= 1.;
            assert(led >= 0. && led < game_source.basic_source.n_leds);
            render_with_z_test(object->color[0], 1., leds, led, object->zdepth);
            if (led == object->target)
            {
                object->position = led;
                object->on_arrival(object);
                return 0;
            }
        }
        object->position = led + dir * distance;
        offset = distance;
    }
    else //we haven't rendered path, either because we don't want or because we have no body
    {
        object->position += dir * distance;
        if (object->length == 0) //there is nothing to render
        {
            return 1;
        }
        //let's render the first led
        led = (int)object->position + (dir < 0);
        assert(led >= 0. && led < game_source.basic_source.n_leds);
        offset = dir * (object->position - led); //this will be always positive and it is the distance toward starting led
        render_with_z_test(object->color[0], 1. - offset, leds, led, object->zdepth);
    }
    //now render the body except the last led
    ws2811_led_t last_color = object->color[0];
    for (int i = 1; i < object->length; i++)
    {
        int body_led = led + dir * i;
        if (body_led >= 0 && body_led < game_source.basic_source.n_leds)
        {
            ws2811_led_t color = lerp_rgb(last_color, object->color[i], (float)(1. - offset));
            render_with_z_test(color, 1.0, leds, body_led, object->zdepth);
            last_color = object->color[i];
        }
        else
        {
            return 1;
        }
    }
    //and finally the last, length+1 led -- when the object is not aligned perfectly, it always affects length + 1 LEDs
    led += object->length;
    if (led >= 0 && led < game_source.basic_source.n_leds)
    {
        render_with_z_test(last_color, offset, leds, led, object->zdepth);
    }
    return 1;
}


/*! The sequence of actions during one loop:
*   - process inputs - this may include timers?
*   - process collisions using stencil buffer -- this may also trigger events
*   - process colors for blinking objects
*   - move and render all objects
* 
* \param frame      current frame - not used
* \param ledstrip   pointer to rendering device
* \returns          1 when render is required, i.e. always
*/
int GameSource_update_leds(int frame, ws2811_t* ledstrip)
{
    (void)frame;
    enum EButtons button;
    enum EState state;
    int i = Controller_get_button(&button, &state);
    if(i != 0) printf("controller button: %s, state: %i\n", Controller_get_button_name(button), state);
    if (i != 0 && button == XBTN_A && state == BT_pressed)
    {
        objects[n_objects].color[0] = game_source.basic_source.gradient.colors[0];
        objects[n_objects].position = 0;
        objects[n_objects].target = 199;
        objects[n_objects].speed = 50.f;
        objects[n_objects].zdepth = 1;
        objects[n_objects].length = 1;
        objects[n_objects].on_arrival = MovingObject_arrive_delete;
        n_objects++;
    }
    for (int led = 0; led < game_source.basic_source.n_leds; ++led)
    {
        ledstrip->channel[0].leds[led] = 0;
    }
    for (int p = 0; p < n_objects; ++p)
    {
        MovingObject_process(&objects[p], 1, ledstrip->channel[0].leds, 1);
    }
    return 1;
}

//msg = color?xxxxxx
void GameSource_process_message(const char* msg)
{
    char* sep = strchr(msg, '?');
    if (sep == NULL)
    {
        printf("GameSource: message does not contain target %s\n", msg);
        return;
    }
    if ((sep - msg) >= 32)
    {
        printf("GameSource: target is too long or poorly formatted: %s\n", msg);
        return;
    }
    if ((strlen(sep + 1) >= 64))
    {
        printf("GameSource: message too long or poorly formatted: %s\n", msg);
        return;
    }
    char target[32];
    char payload[64];
    strncpy(target, msg, sep - msg);
    strncpy(payload, sep + 1, 64);
    target[sep - msg] = 0x0;
    if (!strncasecmp(target, "color", 5))
    {
        int col;
        col = (int)strtol(payload, NULL, 16);
        game_source.basic_source.gradient.colors[0] = col;
        game_source.first_update = 0;
        printf("Switched colour in GameSource to: %s = %x\n", payload, col);
    }
    else
        printf("GameSource: Unknown target: %s, payload was: %s\n", target, payload);

}

void GameSource_init(int n_leds, int time_speed, uint64_t current_time)
{
    BasicSource_init(&game_source.basic_source, n_leds, time_speed, source_config.colors[GAME_SOURCE], current_time);
    game_source.first_update = 0;
    canvas = malloc(sizeof(pixel_t) * n_leds);
    Controller_init();
}

void GameSource_destruct()
{
    free(canvas);
}

void GameSource_construct()
{
    BasicSource_construct(&game_source.basic_source);
    game_source.basic_source.update = GameSource_update_leds;
    game_source.basic_source.init = GameSource_init;
    game_source.basic_source.destruct = GameSource_destruct;
    game_source.basic_source.process_message = GameSource_process_message;
}

GameSource game_source = {
    .basic_source.construct = GameSource_construct,
    .first_update = 0 
};
