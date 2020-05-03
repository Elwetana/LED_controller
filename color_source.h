#ifndef __COLOR_SOURCE_H__
#define __COLOR_SOURCE_H__

#ifdef __cplusplus
extern "C" {
#endif


typedef struct ColorSource
{
	BasicSource basic_source;
	int first_update;
} ColorSource;

void ColorSource_init(int n_leds, int time_speed, int color);
void ColorSource_destruct();
void ColorSource_update_leds(int frame, ws2811_t* ledstrip);

#ifdef __cplusplus
}
#endif

#endif /* __COLOR_SOURCE_H__ */
