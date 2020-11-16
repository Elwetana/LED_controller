#ifndef __XMAS_SOURCE_H__
#define __XMAS_SOURCE_H__

typedef struct XmasSource
{
	BasicSource basic_source;
	int first_update;
	int led_index;
} XmasSource;

extern XmasSource xmas_source;

#endif /* __XMAS_SOURCE_H__ */
