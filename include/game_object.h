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
	uint32_t color_index_game_over;
	uint32_t color_index_stargate;
	uint32_t color_index_health;
	double enemy_spawn_chance;
	double enemy_speed;
    double decoration_speed;
} config;


#define MAX_N_OBJECTS     256

extern const int C_PLAYER_OBJ_INDEX;

enum GameModes
{
	GM_LEVEL1,
	GM_LEVEL1_WON,
	GM_PLAYER_LOST
};



void GameObjects_init();
void GameObject_init(int gi, int health, int stencil_flag);
int GameObjects_update_leds(int frame, ws2811_t* ledstrip);

enum GameModes GameObjects_get_current_mode();
void GameObjects_next_level();

void GameObjects_player_reached_gate();
void GameObjects_set_mode_player_lost();
void GameObject_delete_object(int gi);
void GameObject_mark(int gi, int mark);
int GameObject_get_mark(int gi);
int GameObject_take_hit(int gi);
int GameObject_heal(int gi);
int GameObject_get_health(int gi);

void GameObject_debug_projectile();
void GameObject_debug_win();

#endif /* __GAME_SOURCE_PRIV_H__ */
