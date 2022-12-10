#ifndef __M3_GAME_H__
#define __M3_GAME_H__

void Match3_print_info(int led);

const int Match3_Game_catch_bullet(int led);
const int Match3_Game_swap_jewels(int led, int dir);

void Match3_Game_render_field();
void Match3_Game_render_leds(int frame, ws2811_t* ledstrip);
void Match3_Game_init();
void Match3_Game_destruct();

#endif /* __M3_GAME_H__ */