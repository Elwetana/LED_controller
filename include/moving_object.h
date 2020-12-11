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

/*! Basic object that has position, length and speed
 *  the object is rendered from position in the direction, i.e. it's pushed */
typedef struct MovingObject
{
    double position;    //!< index of the leftmost LED, `position` + `length` - 1 is the index of the rightmost LED
    uint32_t length;
    double speed;       //<! in leds per second
    uint32_t target;
    enum MovingObjectFacing facing;
    int zdepth;
    ws2811_led_t color[MAX_OBJECT_LENGTH];  //!< must be initialized with `length` colors, color[0] is tail, color[length-1] is head, regardless of `facing`
    int deleted;
    void(*on_arrival)(struct MovingObject*);
} moving_object_t;


struct MoveResults
{
    int dir;             //!< -1 - moving left, +1 moving right
    int df_not_aligned;  //!<  0 if facing == dir, +1 if facing != dir
    int trail_start;
    double trail_offset; //!< number from <0,1>
    int body_start;
    int body_end;        //!< this is the last led, inclusive
    double body_offset;
    int target_reached;  //!< 0 - target not reached, 1 target was reached, -1 target was already reached without moving
    double end_position;
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
void MovingObject_init_stopped(moving_object_t* object, double position, enum MovingObjectFacing facing, uint32_t length, int zdepth);

/*! Arrival method for moving_object_t, mark as deleted on arrival */
void MovingObject_arrive_delete(moving_object_t* object);

/*! Arrival method for moving_object_t, set speed to 0 on arrival */
void MovingObject_arrive_stop(moving_object_t* object);

/*!
 * @brief Set the facing, if the facing is changed, we have to adjust the position
 * @param facing    desired facing
*/
void MovingObject_set_facing(moving_object_t* object, enum MovingObjectFacing facing);


int MovingObject_get_move_results(moving_object_t* object, struct MoveResults* move_results);

/*!
* Render the object
* \param render_trail    1 -- all intermediate leds will be lit, 0 -- only the end position will be rendered
* \return               0 when the object arrives at _target_, 1 otherwise
*/
int MovingObject_render(moving_object_t* object, struct MoveResults* move_results, ws2811_led_t* leds, int render_trail);

int MovingObject_update(moving_object_t* object, struct MoveResults* mr);

int unit_tests();
#endif /* __MOVING_OBJECT_H__ */