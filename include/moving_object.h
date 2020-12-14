#ifndef __MOVING_OBJECT_H__
#define __MOVING_OBJECT_H__


//returns +1 if a >= b, -1 if a < b
#define SGN(a,b) 1 - 2 * ((a) - (b) < 0.0001)    

#define MAX_OBJECT_LENGTH  200

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
 * @brief Set render mode
 * @param mi    object index
 * @param mode  0: render antialiased, 1: render with trail (and antialiased), 2: render aligned
*/
void MovingObject_set_render_mode(int mi, int mode);

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

/*!
 * @brief Adjust projectile movement so that it is aligned with target, stop it and rewrite its callback function
 * @param mi_bullet 
 * @param mi_target 
 * @param new_callback 
*/
void MovingObject_target_hit(int mi_bullet, int mi_target, void(*new_callback)(int));

/*!
* Render the object
* \return               always 1, if there's error, 0
*/
int MovingObject_render(int mi, ws2811_led_t* leds);

int MovingObject_update(int mi);

int unit_tests();
#endif /* __MOVING_OBJECT_H__ */
