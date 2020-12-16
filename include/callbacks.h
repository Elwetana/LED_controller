#ifndef __CALLBACKS_H__
#define __CALLBACKS_H__

/*! Arrival method for moving_object_t, mark as deleted on arrival */
void OnArrival_delete(int index);

/*! Arrival method for moving_object_t, set speed to 0 on arrival */
void OnArrival_stop_and_explode(int index);

/*! Arrivale functions for bullet collisions */
void OnArrival_blink_and_continue(int i);
void OnArrival_blink_and_die(int i);
void OnEnd_resume(int i);


#endif /* __CALLBACKS_H__ */