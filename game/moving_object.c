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
#include "game_object.h"
#include "game_source.h"

#define GAME_DEBUG

#define C_PRECIS 0.0001

/*! Basic object that has position, length and speed
 *  the object is rendered from position in the direction, i.e. it's pushed */
typedef struct MovingObject
{
    int index;
    double position;    //!< index of the leftmost LED, `position` + `length` - 1 is the index of the rightmost LED
    uint32_t length;
    double speed;       //<! in leds per second
    uint32_t target;
    enum MovingObjectFacing facing;
    int zdepth;
    ws2811_led_t color[MAX_OBJECT_LENGTH];  //!< must be initialized with `length` colors, color[0] is tail, color[length-1] is head, regardless of `facing`
    void(*on_arrival)(int);
} moving_object_t;


typedef struct MoveResults
{
    int updated;
    int dir;             //!< -1 - moving left, +1 moving right
    int df_not_aligned;  //!<  0 if facing == dir, +1 if facing != dir
    int trail_start;
    double trail_offset; //!< number from <0,1>
    int body_start;
    int body_end;        //!< this is the last led, inclusive
    double body_offset;
    int target_reached;  //!< 0 - target not reached, 1 target was reached, -1 target was already reached without moving
    double end_position;
} move_results_t;

static moving_object_t moving_objects[MAX_N_OBJECTS];
static move_results_t move_results[MAX_N_OBJECTS];


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

void MovingObject_init_stopped(int mi, double position, enum MovingObjectFacing facing, uint32_t length, int zdepth)
{
    moving_object_t* object = &moving_objects[mi];
    object->index = mi;
    object->position = position;
    object->facing = facing;
    object->length = length;
    object->speed = 0.;
    object->target = (int)position;
    object->zdepth = zdepth;
    object->on_arrival = NULL;
}

void MovingObject_set_facing(int mi, enum MovingObjectFacing facing)
{
    moving_object_t* object = &moving_objects[mi];
    if (object->facing == facing)
        return;
    object->facing = facing;
    object->position -= facing * ((double)object->length - 1.);
}

void MovingObject_init_movement(int mi, double speed, int target, void(*on_arrival)(int))
{
    moving_object_t* object = &moving_objects[mi];
    object->speed = speed;
    object->target = target;
    object->on_arrival = on_arrival;
}

void MovingObject_apply_colour(int mi, ws2811_led_t* colors)
{
    for (int i = 0; i < (int)moving_objects[mi].length; ++i)
    {
        moving_objects[mi].color[i] = colors[i];
    }
}

int MovingObject_get_length(int mi)
{
    return moving_objects[mi].length;
}

double MovingObject_get_position(int mi)
{
    return moving_objects[mi].position;
}

void MovingObject_stop(int mi)
{
    moving_objects[mi].speed = 0;
    assert(moving_objects[mi].position - (int)moving_objects[mi].position < C_PRECIS);
}

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
int MovingObject_calculate_move_results(int mi)
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
    moving_object_t* object = &moving_objects[mi];
    move_results_t* mr = &move_results[mi];
    mr->updated = 1;
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

    // p + d > t -> p + d = t -> d = t - p
    if (dir * (object->position + dir * distance - (double)object->target) >= -C_PRECIS && object->speed > 0.) //when moving right, condition is >=, when moving left it's <=
    {
        target_reached = 1;
        distance = dir * ((double)object->target - object->position); // this will be > 0 if we haven't started behind the target already
        assert(distance > -C_PRECIS);
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

void MovingObject_get_move_results(int mi, int* left_end, int* right_end, int* dir)
{
    *left_end = (move_results[mi].dir > 0) ? move_results[mi].trail_start : move_results[mi].body_end;
    *right_end = (move_results[mi].dir < 0) ? move_results[mi].trail_start : move_results[mi].body_end;
    *dir = move_results[mi].dir;
}

void MovingObject_target_hit(int mi, int new_body_end, void(*new_callback)(int))
{
#ifdef GAME_DEBUG
    int player_pos = move_results[255].body_end;
    printf("Player %i, projectile %i\n", player_pos, new_body_end);
#endif
    move_results_t* mr = &move_results[mi];
    int length = moving_objects[mi].length;
    mr->body_end = new_body_end;
    mr->body_start = new_body_end - mr->dir * length;
    //if dir > 0: mr->trail_start <= mr->body_start and vice versa 
    assert(mr->dir * (mr->body_start - mr->trail_start) >= 0);
    mr->target_reached = 1;
    mr->end_position = mr->dir > 0 ? mr->body_start : mr->body_end;
    mr->body_offset = 0;
    moving_objects[mi].on_arrival = new_callback;
}


int MovingObject_render(int mi, ws2811_led_t* leds, int render_trail)
{
    moving_object_t* object = &moving_objects[mi];
    move_results_t* mr = &move_results[mi];
    if (object->length == 0)
        return 0;
    assert(mr->updated == 1);
    //printf("Rendering object at positions from %i to %i with color in led 0 %x\n", mr->body_start, mr->body_end, object->color[0]);
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


int MovingObject_update(int mi)
{
    moving_object_t* object = &moving_objects[mi];
    move_results_t* mr = &move_results[mi];

    assert(mr->updated == 1);
    mr->updated = 0;
    object->position = mr->end_position;
    if (mr->target_reached == 1)
    {
        object->on_arrival(object->index);
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
static int MovingObject_process(int mi, ws2811_led_t* leds, int render_trail)
{
    /*
    Simple summary of steps:
        1. calculate distance travelled: d = v * t
            1.1 determine leading and trailing edge
            1.2 if leading edge gets past target, cap the distance, distance can never be less than zero
        2. if render_trail, render the trail from trailing edge to trailing edge + distance
        3. render the body
    */
    MovingObject_calculate_move_results(mi);
    MovingObject_render(mi, leds, render_trail);
    return MovingObject_update(mi);
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

    MovingObject_init_stopped(0, tp.position, tp.facing, 3, 1);
    MovingObject_init_movement(0, tp.speed, tp.target, MovingObject_stop);
    moving_object_t* o = &moving_objects[0];
    o->color[0] = 60;
    o->color[1] = 100;
    o->color[2] = 200;
    game_source.basic_source.time_delta = (uint64_t)1e9 * 1;

    MovingObject_process(0, leds, tp.trail);
    assert(o->position == expected_position);
    for (int i = 0; i < 10; i++)
    {
        assert(leds[i] == (uint32_t)expected_colors[i]);
    }
    return 1;
}

int unit_tests()
{
    run_test((struct TestParams) {
        .position = 1, .facing = MO_FORWARD, .target = 2, .speed = 0.25, .trail = 1
    }, 1.25, (int[10]) { 0, 60, 90, 175, 50, 0, 0, 0, 0, 0 });

    
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
		.position = 0.5, .facing = MO_FORWARD, .target = 3, .speed = 3., .trail = 1
	}, 3., (int[10]) { 30, 60, 60, 60, 100, 200, 0, 0, 0, 0 });

	//static, facing backward
	run_test((struct TestParams) {
		.position = 3., .facing = MO_BACKWARD, .target = 9, .speed = 0., .trail = 1
	}, 3., (int[10]) { 0, 0, 0, 200, 100, 60, 0, 0, 0, 0 });

	//moving right, facing backward
	run_test((struct TestParams) {
		.position = 1.5, .facing = MO_BACKWARD, .target = 9, .speed = 2.75, .trail = 1
	}, 4.25, (int[10]) { /*0*/0, /*1*/100, /*2*/200, /*3*/200, /*4*/200, /*5*/125 /*0.75 * 100 + 0.25 * 200*/, /*6*/70 /*0.75 * 60 + 0.25 * 100*/, /*7*/15, /*8*/0, /*9*/0 });

	//already after target -- this is no longer possible
	//run_test((struct TestParams) {
	//	.position = 3, .facing = MO_FORWARD, .target = 4, .speed = 2.75, .trail = 1
	//}, 3, (int[10]) { /*0*/0, /*1*/0, /*2*/0, /*3*/60, /*4*/100, /*5*/200, /*6*/0, /*7*/0, /*8*/0, /*9*/0 });

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
        .position = 0.5, .facing = MO_FORWARD, .target = 3, .speed = 3., .trail = 0
    }, 3., (int[10]) { 0, 0, 0, 60, 100, 200, 0, 0, 0, 0 });

    //static, facing backward
    run_test((struct TestParams) {
        .position = 2., .facing = MO_BACKWARD, .target = 9, .speed = 0., .trail = 0
    }, 2., (int[10]) { 0, 0, 200, 100, 60, 0, 0, 0, 0, 0 });

    //moving right, facing backward
    run_test((struct TestParams) {
        .position = 1.5, .facing = MO_BACKWARD, .target = 9, .speed = 2.75, .trail = 0
    }, 4.25, (int[10]) { /*0*/0, /*1*/0, /*2*/0, /*3*/0, /*4*/150, /*5*/125 /*0.75 * 100 + 0.25 * 200*/, /*6*/70 /*0.75 * 60 + 0.25 * 100*/, /*7*/15, /*8*/0, /*9*/0 });

    //already after target -- this is no longer possible
    //run_test((struct TestParams) {
    //    .position = 3, .facing = MO_FORWARD, .target = 4, .speed = 2.75, .trail = 0
    //}, 3, (int[10]) { /*0*/0, /*1*/0, /*2*/0, /*3*/60, /*4*/100, /*5*/200, /*6*/0, /*7*/0, /*8*/0, /*9*/0 });

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
