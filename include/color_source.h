#ifndef __COLOR_SOURCE_H__
#define __COLOR_SOURCE_H__

typedef struct ColorSource
{
	BasicSource basic_source;
	int first_update;
} ColorSource;

extern ColorSource color_source;

#endif /* __COLOR_SOURCE_H__ */
