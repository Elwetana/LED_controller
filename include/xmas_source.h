#ifndef __XMAS_SOURCE_H__
#define __XMAS_SOURCE_H__

typedef enum XMAS_MODE
{
	XM_DEBUG,
	XM_SNOWFLAKES,
	XM_GLITTER,    //colours are here: https://coolors.co/1b4501-7b143b-dd992c-550d82-d1b6e2
	XM_ICICLES,
	XM_GLITTER2,
	XM_GRADIENT,
	XM_JOY_PATTERN,
	N_XMAS_MODES
} XMAS_MODE_t;

typedef struct XmasSource
{
	BasicSource basic_source;
	int first_update;
	int led_index;
	XMAS_MODE_t mode;
} XmasSource;

extern XmasSource xmas_source;

#endif /* __XMAS_SOURCE_H__ */
