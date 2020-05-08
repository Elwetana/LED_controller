#ifndef __CHASER_SOURCE_H__
#define __CHASER_SOURCE_H__

void ChaserSource_init(int n_leds, int time_speed);
void ChaserSource_destruct();
//returns 1 if leds were updated, 0 if update is not necessary
int ChaserSource_update_leds(int frame, ws2811_t* ledstrip);

extern SourceFunctions chaser_functions;

#endif /* __CHASER_SOURCE_H__ */
