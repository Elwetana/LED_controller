#ifndef __COLOR_SOURCE_H__
#define __COLOR_SOURCE_H__

void ColorSource_init(int n_leds, int time_speed);
void ColorSource_destruct();
//returns 1 if leds were updated, 0 if update is not necessary
int ColorSource_update_leds(int frame, ws2811_t* ledstrip);

extern SourceFunctions color_functions;

#endif /* __COLOR_SOURCE_H__ */
