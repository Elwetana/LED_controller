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
* Move and render the object
* \param stencil_index  will be written to stencil.
* \param render_path    1 -- all intermediate leds will be lit, 0 -- only the end position will be rendered
* \return               0 when the object arrives at _target_, 1 otherwise
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
