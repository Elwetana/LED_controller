#ifndef __MOVING_OBJECT_H__
#define __MOVING_OBJECT_H__


//returns +1 if a >= b, -1 if a < b
#define SGN(a,b) 1 - 2 * ((a) - (b) < 0.0001)    

#define MAX_OBJECT_LENGTH  16

enum MovingObjectFacing
{
    MO_BACKWARD = -1,
    MO_FORWARD = 1
};

typedef struct CanvasPixel
{
    int zbuffer;
    int stencil;
    int object_index;
} pixel_t;

/*! Canvas for painting helper information (not actual colours), like z-buffer */
pixel_t* canvas;

/*! Clear canvas and reset led colors */
void Canvas_clear(ws2811_led_t* leds);

/*! Init MovingObject with basic values and single colour */
void MovingObject_init_stopped(int mi, double position, enum MovingObjectFacing facing, uint32_t length, int zdepth);

/*! Init MovingObject with movement data */
void MovingObject_init_movement(int mi, double speed, int target, void(*on_arrival)(int));

/*!
 * @brief Apply colours in `colors` to the moving_object.color
 * @param mi        index of moving object
 * @param colors    array of colours, it must be initialized with moving_object.length colours
*/
void MovingObject_apply_colour(int mi, ws2811_led_t* colors);
void MovingObject_stop(int mi);

int MovingObject_get_length(int mi);
double MovingObject_get_position(int mi);


/*!
 * @brief Set the facing, if the facing is changed, we have to adjust the position
 * @param facing    desired facing
*/
void MovingObject_set_facing(int mi, enum MovingObjectFacing facing);

int MovingObject_calculate_move_results(int mi);

/*!
 * @brief Get final position of the moving_object.body and its direction
 * @param mi            index of the object
 * @param left_end      left end of the object movement, i.e. either move_result.body_end or trail_start, 
 *                      depending on the direction
 * @param right_end     right end of the object, ditto
 * @param dir           direction of movement, 1 is right, -1 is left
*/
void MovingObject_get_move_results(int mi, int* left_end, int* right_end, int* dir);

void MovingObject_target_hit(int mi, int new_body_end, void(*new_callback)(int));

/*!
* Render the object
* \param render_trail    1 -- all intermediate leds will be lit, 0 -- only the end position will be rendered
* \return               0 when the object arrives at _target_, 1 otherwise
*/
int MovingObject_render(int mi, ws2811_led_t* leds, int render_trail);

int MovingObject_update(int mi);

int unit_tests();
#endif /* __MOVING_OBJECT_H__ */