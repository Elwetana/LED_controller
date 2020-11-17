#ifndef __XMAS_SOURCE_H__
#define __XMAS_SOURCE_H__

typedef struct XmasSource
{
	BasicSource basic_source;
	int first_update;
	int led_index;
	//neighbors of i-the led and their distance:
	// index of upper neighbor, distance to upper neighbor, index of right neighbor, distance to right neighbor, ...
	// order is up, right, down, left
	int (*geometry)[8];
} XmasSource;

extern XmasSource xmas_source;

#endif /* __XMAS_SOURCE_H__ */
