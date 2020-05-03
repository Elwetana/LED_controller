#ifndef __PERLIN_SOURCE_H__
#define __PERLIN_SOURCE_H__

#ifdef __cplusplus
extern "C" {
#endif

#define PERLIN_FREQ_N  4
struct noise_t
{
	float amplitude;
	float phase;
};

typedef struct PerlinSource
{
	BasicSource basic_source;
	struct noise_t* noise[PERLIN_FREQ_N];
	int noise_freq[PERLIN_FREQ_N];
	double noise_weight[PERLIN_FREQ_N];
} PerlinSource;

void PerlinSource_init(int n_leds, int time_speed);
void PerlinSource_destruct();
void PerlinSource_update_leds(int frame, ws2811_t* ledstrip);

#ifdef __cplusplus
}
#endif

#endif /* __PERLIN_SOURCE_H__ */
