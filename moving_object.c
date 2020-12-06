#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
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
#include "colours.h"
#include "game_source.h"
#include "moving_object.h"


void Canvas_clear()
{
    for (int i = 0; i < game_source.basic_source.n_leds; i++)
    {
        canvas[i].zbuffer = 999;
        canvas[i].stencil = 0;
        canvas[i].object_index = -1;
    }
}

void MovingObject_init_stopped(moving_object_t* object, uint32_t position, uint32_t length, int zdepth, uint32_t color_index)
{
    object->position = (double)position;
    object->length = length;
    object->speed = 0.;
    object->target = position;
    object->zdepth = zdepth;
    for (int i = 0; i < length; ++i)
    {
        object->color[i] = game_source.basic_source.gradient.colors[config.color_index_player];
    }
    object->deleted = 0;
    object->on_arrival = NULL;
}

//********* Arrival methods ***********
void MovingObject_arrive_delete(moving_object_t* object)
{
    object->deleted = 1;
}

void MovingObject_arrive_stop(moving_object_t* object)
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
 * @brief  Move and render the object.
 * There are four possible combinations of movement direction and facing we must handle.
 * Objects with length 0 will not be rendered at all, even if `render_path == 1`
 * @param object         object to move and render
 * @param stencil_index  will be written to stencil.
 * @param leds           rendering device
 * @param render_path    1 -- all intermediate leds will be lit, 0 -- only the end position will be rendered
 * @return              0 when the object arrives at `target`, 1 otherwise
*/
int MovingObject_process(moving_object_t* object, int stencil_index, ws2811_led_t* leds, int render_path)
{
    if (object->deleted)
        return 0;
    double time_seconds = (game_source.basic_source.time_delta / (long)1e3) / (double)1e6;
    double distance = object->speed * time_seconds;
    int dir = SGN(object->target, object->position);
    int led;
    double offset;
    /*
    * How the render_path == 1 works (assuming dir == facing == 1):
    * speed = 3.5, time = 1, position = 7, offset = 0.3
    *     -> distance = 3.5, result position = 10.8, leds: (7) 0.7 - (8) 1 - (9) 1 - (10) 1 - (11) 0.8
    * speed = 0.5, time = 1, position = 7, offset = 0.3
    *     -> distance = 0.5, result position = 7.8, leds: (7) 0.7 - (8) 0.8
    * speed = 0.5, time = 1, position = 7, offset = 8
    *     -> distance = 0.5, result position = 8.3, leds: (7) 0.2 - (8) 1 - (9) 0.3
    * speed = 1, time = 1, position = 7, offset = 0
    *     -> distance = 1, result position = 8, leds: (7) 1 - (8) 1 - (9) 0
    */
    /* if moving right, color trailing led with alpha = 1 - offset, if moving left, color trailing led + 1 with alpha = offset
       trailing is the at `position` if `dir == facing` and `postion + length - 1` otherwise
       Example for position 4.7 and length 3, the trailing LED, offset and leading LED will be for (facing & direction):
         forward & right ( 1, 1):   4, 0.7, 7       (2)   (3)   (4)  *(5)  *(6)  *(7)         4 + 0 + 0
         forward & left  ( 1,-1):   7, 0.3, 4                                                 4 + 2 + 1
         back    & right (-1, 1):   2, 0.7, 5       (2)  *(3)  *(4)  *(5)   (6)   (7)         4 - 2 + 0
         back    & left  (-1,-1):   5, 0.3, 2                                                 4 + 0 + 1
    */
    int df_not_aligned = (1 - dir * object->facing) / 2; //this is a useful quantity, 0 if facing == dir, +1 if facing != dir
    int trailing_led = (int)object->position - dir * df_not_aligned * (object->length - 1) + (dir < 0);
    int leading_led = trailing_led + dir * (object->length - 1);
    offset = dir * (object->position - (int)object->position - (dir < 0)); //this will be always positive and it is the distance toward starting led
    ws2811_led_t trailing_color = object->color[df_not_aligned * (object->length - 1)];
    assert(trailing_led >= 0. && trailing_led < game_source.basic_source.n_leds);
    assert(leading_led >= 0. && leading_led < game_source.basic_source.n_leds);
    assert(offset >= 0. && offset < 1.);
    int target_reached = 0;
    if (render_path && object->length > 0)
    {
        render_with_z_test(trailing_color, 1. - offset, leds, trailing_led, object->zdepth);
    }
    //color the leds between start and end
    while ((offset + distance) > 0.999) //we have visited more than one LED
    {
        trailing_led += dir;
        leading_led += dir;
        distance -= 1.;
        assert(trailing_led >= 0. && trailing_led < game_source.basic_source.n_leds);
        if (render_path && object->length > 0)
        {
            render_with_z_test(trailing_color, 1., leds, trailing_led, object->zdepth);
        }
        if (leading_led == object->target)
        {
            target_reached = 1;
            distance = -offset;
        }
    }
    offset += distance;
    //if we weren't rendering the path, we need to render the trailing LED now
    if (!render_path && object->length > 0)
    {
        render_with_z_test(trailing_color, 1. - offset, leds, trailing_led, object->zdepth);
    }
    //now render the body, from trailing led to leading led
    //if facing and direction are aligned, we are rendering color from 1 to length-1, if they are opposite, we must render from length - 2 to 0
    for (uint32_t i = 1, color_index = df_not_aligned * (object->length - 3) + 1; i < object->length; i++, color_index += object->facing * dir)
    {
        int body_led = trailing_led + dir * i;
        ws2811_led_t color = mix_rgb_color(trailing_color, object->color[color_index], (float)offset);
        render_with_z_test(color, 1.0, leds, body_led, object->zdepth);
        trailing_color = object->color[color_index];
    }
    //if we are not led-aligned, we have to render one more lead after the leading_led
    if (offset > 0.)
    {
        render_with_z_test(trailing_color, (float)offset, leds, leading_led + 1, object->zdepth);
    }
    object->position = (dir * object->facing > 0) * (double)trailing_led + (dir * object->facing < 0) * (double)leading_led;
    if (target_reached)
    {
        object->on_arrival(object);
        return 0;
    }
    object->position += dir * offset;
    return 1;
}


int unit_tests()
{
    ws2811_led_t leds[20];
    for (int i = 0; i < 20; i++) leds[i] = 0;
    moving_object_t o;
    MovingObject_init_stopped(&o, 0, 3, 1, 0);
    o.color[0] = 60;
    o.color[1] = 100;
    o.color[2] = 200;
    o.facing = MO_FORWARD;
    o.on_arrival = MovingObject_arrive_stop;
    o.target = 18;
    game_source.basic_source.time_delta = (uint64_t)1e9 * 1;
    
    //begin tests
    //static facing forward
    o.position = 1;
    o.speed = 0;
    MovingObject_process(&o, 0, leds, 1);
    assert(leds[0] == 0);
    assert(leds[1] == 60);
    assert(leds[2] == 100);
    assert(leds[3] == 200);
    assert(leds[4] == 0);

    for (int i = 0; i < 20; i++) leds[i] = 0;
    Canvas_clear();

    //moving right, facing forward
    o.position = 0.25;
    o.speed = 3.5;
    MovingObject_process(&o, 0, leds, 1);
    assert(o.position == 3.75);
    assert(leds[0] == 45); //60 * 0.75
    assert(leds[1] == 60);
    assert(leds[2] == 60);
    assert(leds[3] == 60);
    assert(leds[4] == 70);
    assert(leds[5] == 125);
    assert(leds[6] == 150);

    for (int i = 0; i < 20; i++) leds[i] = 0;
    Canvas_clear();

    //moving right, facing forward, arriving exactly
    o.position = 0.75;
    o.speed = 2.25;
    MovingObject_process(&o, 0, leds, 1);
    assert(o.position == 3.);
    assert(leds[0] == 15);
    assert(leds[1] == 60);
    assert(leds[2] == 60);
    assert(leds[3] == 60);
    assert(leds[4] == 100);
    assert(leds[5] == 200);
    assert(leds[6] == 0);

    for (int i = 0; i < 20; i++) leds[i] = 0;
    Canvas_clear();

    //movign right, facing forward, arriving at target
    o.position = 0.5;
    o.speed = 3.0;
    o.target = 5;
    MovingObject_process(&o, 0, leds, 1);
    assert(o.position == 3.);
    assert(leds[0] == 30);
    assert(leds[1] == 60);
    assert(leds[2] == 60);
    assert(leds[3] == 60);
    assert(leds[4] == 100);
    assert(leds[5] == 200);
    assert(leds[6] == 0);

    for (int i = 0; i < 20; i++) leds[i] = 0;
    Canvas_clear();

    //static, facing backward
    o.position = 4;
    o.facing = MO_BACKWARD;
    o.speed = 0;
    MovingObject_process(&o, 0, leds, 1);
    assert(o.position == 4.);
    assert(leds[0] == 0);
    assert(leds[1] == 0);
    assert(leds[2] == 200);
    assert(leds[3] == 100);
    assert(leds[4] == 60);
    assert(leds[5] == 0);

    for (int i = 0; i < 20; i++) leds[i] = 0;
    Canvas_clear();

    //moving right, facing backward
    o.speed = 2.75;
    o.position = 3.5;
    o.target = 99;
    MovingObject_process(&o, 0, leds, 1);
    assert(o.position == 6.25);
    assert(leds[0] == 0);
    assert(leds[1] == 100);
    assert(leds[2] == 200);
    assert(leds[4] == 200);
    assert(leds[5] == 125); //0.75 * 100 + 0.25 * 200
    assert(leds[6] == 70);  //0.75 * 60 + 0.25 * 100
    assert(leds[7] == 15);

    return 0;
}