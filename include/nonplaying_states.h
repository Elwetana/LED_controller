#ifndef __NONPLAYING_STATES_H__
#define __NONPLAYING_STATES_H__


void Ready_player_hit(int player_index, enum ERAD_COLOURS colour);
void Ready_player_move(int player_index, signed char dir);

int Ready_update_leds(ws2811_t* ledstrip, void (*get_intervals)(int, int*, int*));


#endif  /* __NONPLAYING_STATES_H__ */


