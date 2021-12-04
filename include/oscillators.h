#ifndef __OSCILLATORS_H__
#define __OSCILLATORS_H__


void RGM_Oscillators_init();
void RGM_Oscillators_clear();
int RGM_Oscillators_update_leds(ws2811_t* ledstrip);
void RGM_Oscillators_destruct();

void RGM_Oscillators_player_hit(int player_index, enum ERAD_COLOURS colour);
void RGM_Oscillators_player_move(int player_index, signed char dir);


#endif  /* __OSCILLATORS_H__ */

