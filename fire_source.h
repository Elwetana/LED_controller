#ifndef __FIRE_SOURCE_H__
#define __FIRE_SOURCE_H__

extern SourceFunctions fire_functions;

void FireSource_init(int n_leds, int time_speed);
void FireSource_destruct();
//returns 1 if leds were updated, 0 if update is not necessary
int FireSource_update_leds(int frame, ws2811_t* ledstrip);

#endif /* __FIRE_SOURCE_H__ */