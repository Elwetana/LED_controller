#ifndef __DDR_GAME_H__
#define __DDR_GAME_H__


void RGM_DDR_init();
void RGM_DDR_clear();
int  RGM_DDR_update_leds(ws2811_t* ledstrip);
//void RGM_DDR_destruct();

void RGM_DDR_player_hit(int player_index, enum ERAD_COLOURS colour);
void RGM_DDR_player_move(int player_index, signed char dir);

void RGM_DDR_render_ready(ws2811_t* ledstrip);
void RGM_DDR_get_ready_interval(int player_index, int* left_led, int* right_led);

#endif  /* __DDR_GAME_H__ */

