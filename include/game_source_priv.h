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
	uint32_t color_index_K;
	double enemy_spawn_chance;
} config;


#define MAX_N_OBJECTS     256

extern const int C_PLAYER_OBJ_INDEX;

typedef struct GameObject
{
	moving_object_t body;
	struct MoveResults mr;
	enum StencilFlags stencil_flag;
	int health;
	struct PulseObject pulse;
} game_object_t;

game_object_t* player_object;
game_object_t game_objects[MAX_N_OBJECTS];

void GameSource_set_mode_player_lost();

#endif /* __GAME_SOURCE_PRIV_H__ */
