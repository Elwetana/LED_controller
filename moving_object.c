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

#define C_PRECIS 0.0001

void Canvas_clear()
{
    for (int i = 0; i < game_source.basic_source.n_leds; i++)
    {
        canvas[i].zbuffer = 999;
        canvas[i].stencil = 0;
        canvas[i].object_index = -1;
    }
}

void MovingObject_init_stopped(moving_object_t* object, double position, enum MovingObjectFacing facing, uint32_t length, int zdepth, uint32_t color_index)
{
    object->position = position;
    object->facing = facing;
    object->length = length;
    object->speed = 0.;
    object->target = (int)position;
    object->zdepth = zdepth;
    for (uint32_t i = 0; i < length; ++i)
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
 * @param render_trail   1 -- all intermediate leds will be lit, 0 -- only the end position will be rendered
 * @return               0 when the object arrives at `target`, 1 otherwise
*/
int MovingObject_process(moving_object_t* object, int stencil_index, ws2811_led_t* leds, int render_trail)
{
    /* How the render_trail == 1 works (assuming dir == facing == 1):
     speed = 3.5, time = 1, position = 7, offset = 0.3
         -> distance = 3.5, result position = 10.8, leds: (7) 0.7 - (8) 1 - (9) 1 - (10) 1 - (11) 0.8
     speed = 0.5, time = 1, position = 7, offset = 0.3
         -> distance = 0.5, result position = 7.8, leds: (7) 0.7 - (8) 0.8
     speed = 0.5, time = 1, position = 7, offset = 8
         -> distance = 0.5, result position = 8.3, leds: (7) 0.2 - (8) 1 - (9) 0.3
     speed = 1, time = 1, position = 7, offset = 0
         -> distance = 1, result position = 8, leds: (7) 1 - (8) 1 - (9) 0
    
    If moving right, color trailing led with alpha = 1 - offset, if moving left, color trailing led + 1 with alpha = offset
       trailing is the at `position` if `dir == facing` and `postion + length - 1` otherwise
       Example for position 4.7 and length 3, the trailing LED, offset and leading LED will be for (facing & direction):
         forward & right ( 1, 1):   4, 0.7, 7       (2)   (3)   (4)  *(5)  *(6)  *(7)         4 + 0 + 0
         forward & left  ( 1,-1):   7, 0.3, 4                                                 4 + 2 + 1
         back    & right (-1, 1):   2, 0.7, 5       (2)  *(3)  *(4)  *(5)   (6)   (7)         4 - 2 + 0
         back    & left  (-1,-1):   5, 0.3, 2                                                 4 + 0 + 1

    Simple summary of steps:
        1. calculate distance travelled: d = v * t
            1.1 determine leading and trailing edge
            1.2 if leading edge gets past target, cap the distance, distance can never be less than zero
        2. if render_trail, render the trail from trailing edge to trailing edge + distance
        3. render the body
    */

    if (object->deleted)
        return 0;
    double time_seconds = (game_source.basic_source.time_delta / (long)1e3) / (double)1e6;
    double distance = object->speed * time_seconds;
    int dir = SGN(object->target, object->position);
    int df_not_aligned = (1 - dir * object->facing) / 2; //this is a useful quantity, 0 if facing == dir, +1 if facing != dir
    int trailing_led = (int)object->position - dir * df_not_aligned * (object->length - 1) + (dir < 0);
    int leading_led = trailing_led + dir * (object->length - 1);
    double offset = dir * (object->position - (int)object->position - (dir < 0)); //this will be always positive and it is the distance toward starting led
    ws2811_led_t trailing_color = object->color[df_not_aligned * (object->length - 1)];
    assert(trailing_led >= 0. && trailing_led < game_source.basic_source.n_leds);
    assert(leading_led >= 0. && leading_led < game_source.basic_source.n_leds);
    assert(offset >= 0. && offset <= 1.);
    int target_reached = 0;

    //determine actual distance travelled
    if (dir * (leading_led + dir * (distance + offset) - (double)object->target) >= -C_PRECIS) //when moving right, condition is >=, when moving left it's <=
    {
        target_reached = 1;
        distance = dir * ((double)object->target - leading_led - offset); // this will be > 0 if we haven't started behind the target already
        if (distance > C_PRECIS)
        {
            object->position += dir * distance;
        }
        else
        {
            target_reached = 0; //we've already been to target, no need to call the arrival function again
            distance = 0;
            //also, we don't update position
        }
    }
    else
    {
        object->position += dir * distance;
    }

    //now render trail
    if (render_trail && object->length > 0 && distance > C_PRECIS)
    {
        render_with_z_test(trailing_color, 1. - offset, leds, trailing_led, object->zdepth);
        for (int i = 1; i - (distance + offset) < C_PRECIS ; ++i)
        {
            render_with_z_test(trailing_color, 1., leds, trailing_led + dir * i, object->zdepth);
        }
    }

    //now we can update distance and calculate new offset
    trailing_led = (int)object->position - dir * df_not_aligned * (object->length - 1) + (dir < 0);
    leading_led = trailing_led + dir * (object->length - 1);
    offset = dir * (object->position - (int)object->position - (dir < 0));
    assert(trailing_led >= 0. && trailing_led < game_source.basic_source.n_leds);
    assert(leading_led >= 0. && leading_led < game_source.basic_source.n_leds);
    assert(offset >= 0. && offset <= 1.);

    //if we haven't rendered the tail, we will render the last led
    if ((!render_trail || distance < C_PRECIS) && object->length > 0)
    {
        render_with_z_test(trailing_color, 1. - offset, leds, trailing_led, object->zdepth);
    }

    //now render the body, from trailing led to leading led
    //if facing and direction are aligned, we are rendering color from 1 to length-1, if they are opposite, we must render from length - 2 to 0
    if (object->length > 0)
    {
        for (uint32_t i = 1, color_index = df_not_aligned * (object->length - 3) + 1; i < object->length; i++, color_index += object->facing * dir)
        {
            int body_led = trailing_led + dir * i;
            ws2811_led_t color = mix_rgb_color(trailing_color, object->color[color_index], (float)offset);
            render_with_z_test(color, 1.0, leds, body_led, object->zdepth);
            trailing_color = object->color[color_index];
        }
    }
    if ((offset > C_PRECIS) && object->length > 0)
    {
        render_with_z_test(trailing_color, (float)offset, leds, leading_led + dir, object->zdepth);
    }
    if (target_reached)
    {
        object->on_arrival(object);
        return 0;
    }
    return 1;
}




/***************************************************************************
*                                 UNIT TESTS
****************************************************************************/

struct TestParams
{
    double position;
    enum MovingObjectFacing facing;
    int target;
    double speed;
    int trail;
};

int run_test(struct TestParams tp, double expected_position, int expected_colors[10])
{
    ws2811_led_t leds[10];
    for (int i = 0; i < 10; i++) leds[i] = 0;
    Canvas_clear();

    moving_object_t o;
    MovingObject_init_stopped(&o, tp.position, tp.facing, 3, 1, 0);
    o.color[0] = 60;
    o.color[1] = 100;
    o.color[2] = 200;
    o.on_arrival = MovingObject_arrive_stop;
    o.target = tp.target;
    o.speed = tp.speed;
    game_source.basic_source.time_delta = (uint64_t)1e9 * 1;

    MovingObject_process(&o, 0, leds, tp.trail);
    assert(o.position == expected_position);
    for (int i = 0; i < 10; i++)
    {
        assert(leds[i] == expected_colors[i]);
    }
    return 1;
}

int unit_tests()
{
	//begin tests
	//static facing forward
	run_test((struct TestParams) {
		.position = 1, .facing = MO_FORWARD, .target = 9, .speed = 0., .trail = 1
	}, 1, (int[10]) { 0, 60, 100, 200, 0, 0, 0, 0, 0, 0 });

	//moving right, facing forward
	run_test((struct TestParams) {
        .position = 0.25, .facing = MO_FORWARD, .target = 9, .speed = 3.5, .trail = 1 
    }, 3.75, (int[10]) { 45 /*60 * 0.75*/, 60, 60, 60, 70, 125, 150, 0, 0, 0 });

	//moving right, facing forward, arriving exactly
	run_test((struct TestParams) {
		.position = 0.75, .facing = MO_FORWARD, .target = 9, .speed = 2.25, .trail = 1
	}, 3., (int[10]) { 15, 60, 60, 60, 100, 200, 0, 0, 0, 0 });

	//movign right, facing forward, arriving at target
	run_test((struct TestParams) {
		.position = 0.5, .facing = MO_FORWARD, .target = 5, .speed = 3., .trail = 1
	}, 3., (int[10]) { 30, 60, 60, 60, 100, 200, 0, 0, 0, 0 });

	//static, facing backward
	run_test((struct TestParams) {
		.position = 4., .facing = MO_BACKWARD, .target = 9, .speed = 0., .trail = 1
	}, 4., (int[10]) { 0, 0, 200, 100, 60, 0, 0, 0, 0, 0 });

	//moving right, facing backward
	run_test((struct TestParams) {
		.position = 3.5, .facing = MO_BACKWARD, .target = 9, .speed = 2.75, .trail = 1
	}, 6.25, (int[10]) { /*0*/0, /*1*/100, /*2*/200, /*3*/200, /*4*/200, /*5*/125 /*0.75 * 100 + 0.25 * 200*/, /*6*/70 /*0.75 * 60 + 0.25 * 100*/, /*7*/15, /*8*/0, /*9*/0 });

	//already after target
	run_test((struct TestParams) {
		.position = 3, .facing = MO_FORWARD, .target = 4, .speed = 2.75, .trail = 1
	}, 3, (int[10]) { /*0*/0, /*1*/0, /*2*/0, /*3*/60, /*4*/100, /*5*/200, /*6*/0, /*7*/0, /*8*/0, /*9*/0 });

	//moving left, facing backward
	run_test((struct TestParams) {
		.position = 6, .facing = MO_BACKWARD, .target = 0, .speed = 2, .trail = 1
	}, 4, (int[10]) { /*0*/0, /*1*/0, /*2*/200, /*3*/100, /*4*/60, /*5*/60, /*6*/60, /*7*/0, /*8*/0, /*9*/0 });

	//moving left, facing forward
	run_test((struct TestParams) {
		.position = 6.25, .facing = MO_BACKWARD, .target = 0, .speed = 2, .trail = 1
	}, 4.25, (int[10]) { /*0*/0, /*1*/0, /*2*/150, /*3*/125, /*4*/70, /*5*/60, /*6*/60, /*7*/15, /*8*/0, /*9*/0 });

	//no trails
    //static facing forward
    run_test((struct TestParams) {
        .position = 1, .facing = MO_FORWARD, .target = 9, .speed = 0., .trail = 0
    }, 1, (int[10]) { 0, 60, 100, 200, 0, 0, 0, 0, 0, 0 });

    //moving right, facing forward
    run_test((struct TestParams) {
        .position = 0.25, .facing = MO_FORWARD, .target = 9, .speed = 3.5, .trail = 0
    }, 3.75, (int[10]) { 0, 0, 0, 15, 70, 125, 150, 0, 0, 0 });

    //moving right, facing forward, arriving exactly
    run_test((struct TestParams) {
        .position = 0.75, .facing = MO_FORWARD, .target = 9, .speed = 2.25, .trail = 0
    }, 3., (int[10]) { 0, 0, 0, 60, 100, 200, 0, 0, 0, 0 });

    //movign right, facing forward, arriving at target
    run_test((struct TestParams) {
        .position = 0.5, .facing = MO_FORWARD, .target = 5, .speed = 3., .trail = 0
    }, 3., (int[10]) { 0, 0, 0, 60, 100, 200, 0, 0, 0, 0 });

    //static, facing backward
    run_test((struct TestParams) {
        .position = 4., .facing = MO_BACKWARD, .target = 9, .speed = 0., .trail = 0
    }, 4., (int[10]) { 0, 0, 200, 100, 60, 0, 0, 0, 0, 0 });

    //moving right, facing backward
    run_test((struct TestParams) {
        .position = 3.5, .facing = MO_BACKWARD, .target = 9, .speed = 2.75, .trail = 0
    }, 6.25, (int[10]) { /*0*/0, /*1*/0, /*2*/0, /*3*/0, /*4*/150, /*5*/125 /*0.75 * 100 + 0.25 * 200*/, /*6*/70 /*0.75 * 60 + 0.25 * 100*/, /*7*/15, /*8*/0, /*9*/0 });

    //already after target
    run_test((struct TestParams) {
        .position = 3, .facing = MO_FORWARD, .target = 4, .speed = 2.75, .trail = 0
    }, 3, (int[10]) { /*0*/0, /*1*/0, /*2*/0, /*3*/60, /*4*/100, /*5*/200, /*6*/0, /*7*/0, /*8*/0, /*9*/0 });

    //moving left, facing backward
    run_test((struct TestParams) {
        .position = 6, .facing = MO_BACKWARD, .target = 0, .speed = 2, .trail = 0
    }, 4, (int[10]) { /*0*/0, /*1*/0, /*2*/200, /*3*/100, /*4*/60, /*5*/0, /*6*/0, /*7*/0, /*8*/0, /*9*/0 });

    //moving left, facing forward
    run_test((struct TestParams) {
        .position = 6.25, .facing = MO_BACKWARD, .target = 0, .speed = 2, .trail = 0
    }, 4.25, (int[10]) { /*0*/0, /*1*/0, /*2*/150, /*3*/125, /*4*/70, /*5*/15, /*6*/0, /*7*/0, /*8*/0, /*9*/0 });


	return 0;
}