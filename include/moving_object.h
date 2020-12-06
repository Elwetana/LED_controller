#ifndef __MOVING_OBJECT_H__
#define __MOVING_OBJECT_H__


//returns +1 if a > b, -1 if a < b and 0 if a == b
#define SGN(a,b)  ((a) > (b)) - ((a) < (b))

#define MAX_N_OBJECTS     256
#define MAX_OBJECT_LENGTH  16


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
    int object_index;
} pixel_t;

/*! Canvas for painting helper information (not actual colours), like z-buffer */
pixel_t* canvas;

/*! Arrival method for moving_object_t, mark as deleted on arrival */
void MovingObject_arrive_delete(moving_object_t* object);

/*! Arrival method for moving_object_t, set speed to 0 on arrival */
void MovingObject_arrive_stop(moving_object_t* object);

/*!
* Move and render the object
* \param stencil_index  will be written to stencil.
* \param render_path    1 -- all intermediate leds will be lit, 0 -- only the end position will be rendered
* \return               0 when the object arrives at _target_, 1 otherwise
*/
int MovingObject_process(moving_object_t* object, int stencil_index, ws2811_led_t* leds, int render_path);

#endif /* __MOVING_OBJECT_H__ */