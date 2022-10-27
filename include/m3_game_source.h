#ifndef __M3_GAME_SOURCE_H__
#define __M3_GAME_SOURCE_H__


typedef struct Match3GameSource
{
	BasicSource basic_source;
	uint64_t start_time;
	int cur_frame;
	int n_players;
} Match3GameSource;

extern Match3GameSource match3_game_source;


#endif /* __M3_GAME_SOURCE_H__ */