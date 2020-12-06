#ifndef __GAME_SOURCE_H__
#define __GAME_SOURCE_H__

/**
* Description of game modes:
*	- Asteroids: player has to navigate the asteroid field and reach the hyperdrive gate before it closes
*/

typedef struct GameSource
{
	BasicSource basic_source;
	int first_update;
} GameSource;

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

extern GameSource game_source;

#endif /* __GAME_SOURCE_H__ */
