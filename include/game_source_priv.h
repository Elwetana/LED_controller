#ifndef __GAME_SOURCE_PRIV_H__
#define __GAME_SOURCE_PRIV_H__

struct
{
	uint32_t player_start_position;
	uint32_t player_ship_size;
	uint32_t color_index_player;
	uint32_t player_health_levels;
	double player_ship_speed;
	uint32_t color_index_R;
	uint32_t color_index_G;
	uint32_t color_index_B;
	uint32_t color_index_C;
	uint32_t color_index_M;
	uint32_t color_index_Y;
	uint32_t color_index_W;
} config;


typedef struct GameObject
{
	moving_object_t body;
	struct MoveResults mr;
	enum StencilFlags stencil_flag;
	ws2811_led_t source_color[MAX_OBJECT_LENGTH];
	int health;
} game_object_t;

game_object_t* player_object;

#endif /* __GAME_SOURCE_PRIV_H__ */
