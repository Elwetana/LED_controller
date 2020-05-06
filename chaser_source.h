#ifndef __CHASER_SOURCE_H__
#define __CHASER_SOURCE_H__

#ifdef __cplusplus
extern "C" {
#endif

#define N_HEADS		14

typedef struct ChaserSource
{
	BasicSource basic_source;
	int heads[N_HEADS];
	int cur_heads[N_HEADS];
} ChaserSource;

void ChaserSource_init(int n_leds, int time_speed);
void ChaserSource_destruct();
//returns 1 if leds were updated, 0 if update is not necessary
int ChaserSource_update_leds(int frame, ws2811_t* ledstrip);

#ifdef __cplusplus
}
#endif

#endif /* __CHASER_SOURCE_H__ */
