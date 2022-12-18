#ifndef __M3_GAME_H__
#define __M3_GAME_H__

void Match3_print_info(int led);
void Match3_get_segment_and_position(const int segment_info, int* segment, int* position);
int Match3_get_segment_info(const int segment, const int position);

int Match3_Game_catch_bullet(int led);
int Match3_Game_swap_jewels(int led, int dir);

void Match3_Game_render_select(void);
void Match3_Game_render_field(void);
void Match3_Game_render_clue_field(char* morse_char, int char_type);
void Match3_Game_render_end(void);
void Match3_Game_render_leds(int frame, ws2811_t* ledstrip);
void Match3_Game_init(void);
void Match3_Game_destruct(void);

#endif /* __M3_GAME_H__ */
