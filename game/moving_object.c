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
#include "moving_object.h"
#include "pulse_object.h"
#include "game_source_priv.h"
#include "game_source.h"

#define C_PRECIS 0.0001

void Canvas_clear(ws2811_led_t* leds)
{
    for (int i = 0; i < game_source.basic_source.n_leds; i++)
    {
        canvas[i].zbuffer = 999;
        canvas[i].stencil = -1;
        canvas[i].object_index = -1;
        leds[i] = 0;
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
        object->color[i] = game_source.basic_source.gradient.colors[color_index];
    }
    object->deleted = 0;
    object->on_arrival = NULL;
}

void MovingObject_set_facing(moving_object_t* object, enum MovingObjectFacing facing)
{
    if (object->facing == facing)
        return;
    object->facing = facing;
    object->position -= facing * ((double)object->length - 1.);
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
 * @brief Fill the MoveResults structure
 * 
 * The length of trail is always (body_start - trail_start) * dir
 * @return  now returns always 1
 */
int MovingObject_get_move_results(moving_object_t* object, struct MoveResults* mr)
/*
    If moving right, color trailing led with alpha = 1 - offset, if moving left, color trailing led + 1 with alpha = offset
       trailing is the at `position` if `dir == facing` and `postion + length - 1` otherwise
       Example for position 4.7 and length 3, the trailing LED, offset and leading LED will be for (facing & direction):
         forward & right ( 1, 1):   4, 0.7, 7
         forward & left  ( 1,-1):   7, 0.3, 4
         back    & right (-1, 1):   4, 0.7, 7
         back    & left  (-1,-1):   7, 0.3, 4
*/
{
    double time_seconds = (game_source.basic_source.time_delta / (long)1e3) / (double)1e6;
    double distance = object->speed * time_seconds;
    int dir = SGN(object->target, object->position);
    int df_not_aligned = (1 - dir * object->facing) / 2; //this is a useful quantity, 0 if facing == dir, +1 if facing != dir

    int trailing_led = (int)object->position + (dir < 0) * object->length;
    int leading_led = (int)object->position + (dir > 0) * (object->length - 1);

    double offset = object->position - (int)object->position;
    if (dir < 0) offset = 1. - offset;

    assert(trailing_led >= 0. && trailing_led < game_source.basic_source.n_leds);
    assert(leading_led >= 0. && leading_led < game_source.basic_source.n_leds);
    assert(offset >= 0. && offset <= 1.);
    int target_reached = 0;

    mr->dir = dir;
    mr->df_not_aligned = df_not_aligned;
    mr->trail_start = trailing_led;
    mr->trail_offset = offset;

    if (dir * (leading_led + dir * (distance + offset) - (double)object->target) >= -C_PRECIS) //when moving right, condition is >=, when moving left it's <=
    {
        target_reached = 1;
        distance = dir * ((double)object->target - leading_led - offset); // this will be > 0 if we haven't started behind the target already
        if (distance < -C_PRECIS)
        {
            target_reached = -1; //we've already been to target, no need to call the arrival function again
            distance = 0; //also, we don't update position
        }
    }
    mr->end_position = object->position + dir * distance;
    mr->body_start = (int)mr->end_position + (dir < 0) * (object->length);
    mr->body_offset = dir * (mr->end_position - (int)mr->end_position - (dir < 0));
    mr->body_end = mr->body_start + dir * (object->length);
    mr->target_reached = target_reached;
    assert(mr->body_start >= 0. && mr->body_start < game_source.basic_source.n_leds);
    assert(mr->body_offset >= 0. && mr->body_offset <= 1.);
    assert(dir * (mr->trail_start - mr->body_start) <= 0); 
    assert(dir * (mr->body_start - mr->body_end) <= 0);
    assert(dir * (mr->body_start - mr->end_position) <= 0);
    return 1;
}


int MovingObject_render(moving_object_t* object, struct MoveResults* mr, ws2811_led_t* leds, int render_trail)
{
    if (object->length == 0)
        return 0;
    //render trail
    ws2811_led_t trailing_color = object->color[mr->df_not_aligned * (object->length - 1)];
    if (render_trail)
    {
        int trailing_led = mr->trail_start;
        render_with_z_test(trailing_color, 1. - mr->trail_offset, leds, trailing_led, object->zdepth);
        trailing_led += mr->dir;
        while (mr->dir * (mr->body_start - trailing_led) >= 0)
        {
            render_with_z_test(trailing_color, 1., leds, trailing_led, object->zdepth);
            trailing_led += mr->dir;
        }
    }
    if (!render_trail)
    {
        render_with_z_test(trailing_color, 1. - mr->body_offset, leds, mr->body_start, object->zdepth);
    }

    // Now render the body, from trailing led to leading led
    // If facing and direction are aligned, we are rendering color from 1 to length-1, if they are opposite, we must render from length - 2 to 0
    // First and last led are rendered with alpha, the rest with color mixing
    int body_led = mr->body_start + mr->dir;
    int color_index = mr->df_not_aligned * (object->length - 3) + 1;
    while (mr->dir * (mr->body_end - body_led) > 0)
    //for (uint32_t i = 1, color_index = mr->df_not_aligned * (object->length - 3) + 1; i < object->length; i++, color_index += object->facing * mr->dir)
    {
        ws2811_led_t color = mix_rgb_color(trailing_color, object->color[color_index], (float)mr->body_offset);
        render_with_z_test(color, 1.0, leds, body_led, object->zdepth);
        trailing_color = object->color[color_index];
        color_index += object->facing * mr->dir;
        body_led += mr->dir;
    }
    int leading_led = mr->body_end;
    render_with_z_test(trailing_color, (float)mr->body_offset, leds, leading_led, object->zdepth);
    return 1;
}


int MovingObject_update(moving_object_t* object, struct MoveResults* mr)
{
    if (object->deleted)
        return 0;
    object->position = mr->end_position;
    if (mr->target_reached == 1)
    {
        object->on_arrival(object);
        return 0;
    }
    return 1;
}


/*!
 * @brief  Move and render the object -- this is now only used by unit tests
 * There are four possible combinations of movement direction and facing we must handle.
 * Objects with length 0 will not be rendered at all, even if `render_path == 1`
 * @param object         object to move and render
 * @param stencil_index  will be written to stencil.
 * @param leds           rendering device
 * @param render_trail   1 -- all intermediate leds will be lit, 0 -- only the end position will be rendered
 * @return               0 when the object arrives at `target`, 1 otherwise
*/
static int MovingObject_process(moving_object_t* object, ws2811_led_t* leds, int render_trail)
{
    /*
    Simple summary of steps:
        1. calculate distance travelled: d = v * t
            1.1 determine leading and trailing edge
            1.2 if leading edge gets past target, cap the distance, distance can never be less than zero
        2. if render_trail, render the trail from trailing edge to trailing edge + distance
        3. render the body
    */
    struct MoveResults mr;
    MovingObject_get_move_results(object, &mr);
    MovingObject_render(object, &mr, leds, render_trail);
    return MovingObject_update(object, &mr);
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
    ws2811_led_t leds[200]; //this must be the same as n_leds
    Canvas_clear(leds);

    moving_object_t o;
    MovingObject_init_stopped(&o, tp.position, tp.facing, 3, 1, 0);
    o.color[0] = 60;
    o.color[1] = 100;
    o.color[2] = 200;
    o.on_arrival = MovingObject_arrive_stop;
    o.target = tp.target;
    o.speed = tp.speed;
    game_source.basic_source.time_delta = (uint64_t)1e9 * 1;

    MovingObject_process(&o, leds, tp.trail);
    assert(o.position == expected_position);
    for (int i = 0; i < 10; i++)
    {
        assert(leds[i] == (uint32_t)expected_colors[i]);
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
		.position = 3., .facing = MO_BACKWARD, .target = 9, .speed = 0., .trail = 1
	}, 3., (int[10]) { 0, 0, 0, 200, 100, 60, 0, 0, 0, 0 });

	//moving right, facing backward
	run_test((struct TestParams) {
		.position = 1.5, .facing = MO_BACKWARD, .target = 9, .speed = 2.75, .trail = 1
	}, 4.25, (int[10]) { /*0*/0, /*1*/100, /*2*/200, /*3*/200, /*4*/200, /*5*/125 /*0.75 * 100 + 0.25 * 200*/, /*6*/70 /*0.75 * 60 + 0.25 * 100*/, /*7*/15, /*8*/0, /*9*/0 });

	//already after target
	run_test((struct TestParams) {
		.position = 3, .facing = MO_FORWARD, .target = 4, .speed = 2.75, .trail = 1
	}, 3, (int[10]) { /*0*/0, /*1*/0, /*2*/0, /*3*/60, /*4*/100, /*5*/200, /*6*/0, /*7*/0, /*8*/0, /*9*/0 });

	//moving left, facing backward
	run_test((struct TestParams) {
		.position = 4, .facing = MO_BACKWARD, .target = 0, .speed = 2, .trail = 1
	}, 2, (int[10]) { /*0*/0, /*1*/0, /*2*/200, /*3*/100, /*4*/60, /*5*/60, /*6*/60, /*7*/0, /*8*/0, /*9*/0 });

	//moving left, facing forward
	run_test((struct TestParams) {
		.position = 4.25, .facing = MO_BACKWARD, .target = 0, .speed = 2, .trail = 1
	}, 2.25, (int[10]) { /*0*/0, /*1*/0, /*2*/150, /*3*/125, /*4*/70, /*5*/60, /*6*/60, /*7*/15, /*8*/0, /*9*/0 });

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
        .position = 2., .facing = MO_BACKWARD, .target = 9, .speed = 0., .trail = 0
    }, 2., (int[10]) { 0, 0, 200, 100, 60, 0, 0, 0, 0, 0 });

    //moving right, facing backward
    run_test((struct TestParams) {
        .position = 1.5, .facing = MO_BACKWARD, .target = 9, .speed = 2.75, .trail = 0
    }, 4.25, (int[10]) { /*0*/0, /*1*/0, /*2*/0, /*3*/0, /*4*/150, /*5*/125 /*0.75 * 100 + 0.25 * 200*/, /*6*/70 /*0.75 * 60 + 0.25 * 100*/, /*7*/15, /*8*/0, /*9*/0 });

    //already after target
    run_test((struct TestParams) {
        .position = 3, .facing = MO_FORWARD, .target = 4, .speed = 2.75, .trail = 0
    }, 3, (int[10]) { /*0*/0, /*1*/0, /*2*/0, /*3*/60, /*4*/100, /*5*/200, /*6*/0, /*7*/0, /*8*/0, /*9*/0 });

    //moving left, facing backward
    run_test((struct TestParams) {
        .position = 4, .facing = MO_BACKWARD, .target = 0, .speed = 2, .trail = 0
    }, 2, (int[10]) { /*0*/0, /*1*/0, /*2*/200, /*3*/100, /*4*/60, /*5*/0, /*6*/0, /*7*/0, /*8*/0, /*9*/0 });

    //moving left, facing forward
    run_test((struct TestParams) {
        .position = 4.25, .facing = MO_BACKWARD, .target = 0, .speed = 2, .trail = 0
    }, 2.25, (int[10]) { /*0*/0, /*1*/0, /*2*/150, /*3*/125, /*4*/70, /*5*/15, /*6*/0, /*7*/0, /*8*/0, /*9*/0 });


	return 0;
}
