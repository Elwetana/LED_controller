#ifndef __GAME_SOURCE_H__
#define __GAME_SOURCE_H__

typedef struct GameSource
{
	BasicSource basic_source;
	int first_update;
} GameSource;

extern GameSource game_source;

#endif /* __GAME_SOURCE_H__ */
