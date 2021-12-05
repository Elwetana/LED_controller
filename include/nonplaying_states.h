#ifndef __NONPLAYING_STATES_H__
#define __NONPLAYING_STATES_H__


void Ready_player_hit(int player_index, enum ERAD_COLOURS colour);
void Ready_player_move(int player_index, signed char dir);

void RGM_DDR_Ready_clear();
int RGM_DDR_Ready_update_leds(ws2811_t* ledstrip);

void RGM_Osc_Ready_clear();
int RGM_Oscillators_Ready_update_leds(ws2811_t* ledstrip);

void RGM_Show_Score_clear();
int RGM_Show_Score_update_leds(ws2811_t* ledstrip);

#endif  /* __NONPLAYING_STATES_H__ */


