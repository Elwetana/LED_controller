#ifndef __M3_GAME_SOURCE_H__
#define __M3_GAME_SOURCE_H__

enum EM3_BUTTONS
{
	M3_A,
	M3_B
};


typedef struct Match3GameSource
{
	BasicSource basic_source;
	uint64_t start_time;
	int cur_frame;
	int n_players;
	void (*Player_press_button)(int, enum EM3_BUTTONS);
	//void (*Player_hit_color)(int, enum ERAD_COLOURS);
	void (*Player_move)(int, signed char);
	//void (*Player_start)(int);
} Match3GameSource;

extern Match3GameSource match3_game_source;


#endif /* __M3_GAME_SOURCE_H__ */