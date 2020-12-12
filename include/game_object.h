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


enum StencilFlags
{
    SF_Background,
    SF_Player,
    SF_PlayerProjectile,
    SF_Enemy,
    SF_EnemyProjectile,
    SF_N_FLAGS
};


void GameObjects_init();
void GameObject_init(int gi, int health, int stencil_flag);
int GameObject_update_leds(int frame, ws2811_t* ledstrip);
void GameSource_set_mode_player_lost();
void GameObject_delete_object(int gi);
int GameObject_take_hit(int gi);
int GameObject_heal(int gi);
int GameObject_get_health(int gi);


#endif /* __GAME_SOURCE_PRIV_H__ */
