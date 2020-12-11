#ifndef __PULSE_OBJECT_H__
#define __PULSE_OBJECT_H__


typedef struct GameObject game_object_t;


enum PulseModes
{
    PM_STEADY,  //!< no blinking, copy next_color to moving_object.color
    PM_REPEAT,  //!< keep pulsing with the same parameter
    PM_ONCE,    //!< switch to normal steady light after repetions cycles switch to steady and execute callback
    PM_FADE     //!< decrease amplitude at end, after repetions cycles switch to steady and execute callback
};

/*!
 * @brief Generate blinking, pulsing and so on
 * Use equation: t =  A * ((1. + cos(f * (t - t0) + phi + led * phi_led)) / 2.) ^ k;
 * Then use t to lerp between colors0 to colors1
 */
typedef struct PulseObject
{
	uint64_t start_time;    //!< in ms
	uint64_t end_time;      //!< also in ms, end_time > start_time
	int repetitions;		//!< for ONCE and FADE modes how many times to repeat the animation. For STEADY if -1, it means the colors have been set;
	enum PulseModes pulse_mode;
	double amplitude;		//!< this should be <0, 1>
	double frequency;       //!< f = 2*pi/t_period
	double phase;           //!< phi
	double led_phase;		//!< phi_led
	double spec_exponent;   //!< k
	hsl_t colors_0[MAX_OBJECT_LENGTH];
	hsl_t colors_1[MAX_OBJECT_LENGTH];
	ws2811_led_t next_color[MAX_OBJECT_LENGTH];
	void (*callback)(game_object_t*);
} pulse_object_t;

void PulseObject_update(game_object_t* object);

void PulseObject_init_steady(pulse_object_t* po);

void PulseObject_init_player_lost_health();

void PulseObject_init_projectile_explosion(game_object_t* go);

#endif  /* __PULSE_OBJECT_H__ */
