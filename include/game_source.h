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

extern GameSource game_source;


#endif /* __GAME_SOURCE_H__ */
