#ifndef __XMAS_SOURCE_H__
#define __XMAS_SOURCE_H__

typedef enum XMAS_MODE
{
	XM_DEBUG,
	XM_SNOWFLAKES,
	N_XMAS_MODES
} XMAS_MODE_t;

typedef struct XmasSource
{
	BasicSource basic_source;
	int first_update;
	int led_index;
	//neighbors of i-the led and their distance:
	// index of upper neighbor, distance to upper neighbor, index of right neighbor, distance to right neighbor, ...
	// order is up, right, down, left
	// the last column (9) is the height, i.e. how many leds there are below me
	int (*geometry)[9];
	int* heads;
	int* springs;
	int n_heads;
	int n_springs;
	XMAS_MODE_t mode;
} XmasSource;

extern XmasSource xmas_source;

#endif /* __XMAS_SOURCE_H__ */
