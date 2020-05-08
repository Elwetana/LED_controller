#ifndef __PERLIN_SOURCE_H__
#define __PERLIN_SOURCE_H__

void PerlinSource_init(int n_leds, int time_speed);
void PerlinSource_destruct();
//returns 1 if leds were updated, 0 if update is not necessary
int PerlinSource_update_leds(int frame, ws2811_t* ledstrip);

extern SourceFunctions perlin_functions;

#endif /* __PERLIN_SOURCE_H__ */
