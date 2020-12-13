#ifndef __PULSE_OBJECT_H__
#define __PULSE_OBJECT_H__

enum PulseModes
{
    PM_STEADY,  //!< no blinking, copy next_color to moving_object.color
    PM_REPEAT,  //!< keep pulsing with the same parameter
    PM_ONCE,    //!< switch to normal steady light after repetions cycles switch to steady and execute callback
    PM_FADE     //!< decrease amplitude at end, after repetions cycles switch to steady and execute callback
};

void PulseObject_update(int pi);

void PulseObject_init(int pi, double amp, enum PulseModes pm, int repetitions, int period, double phase, double led_phase, double spec, void(*on_end)(int));
void PulseObject_set_color_all(int pi, int color_index_0, int color_index_1, int next_color, int length);
void PulseObject_set_color(int pi, int color0, int color1, int color_next, int led);

void PulseObject_init_steady(int pi, int color_index, int length);


#endif  /* __PULSE_OBJECT_H__ */
