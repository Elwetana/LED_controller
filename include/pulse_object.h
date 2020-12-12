#ifndef __PULSE_OBJECT_H__
#define __PULSE_OBJECT_H__



void PulseObject_update(int pi);

void PulseObject_init_steady(int pi, int color_index, int length);

void PulseObject_init_player_lost_health();

void PulseObject_init_projectile_explosion(int pi);

#endif  /* __PULSE_OBJECT_H__ */
