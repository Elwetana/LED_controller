#ifndef __PERLIN_SOURCE_H__
#define __PERLIN_SOURCE_H__

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

extern PerlinSource perlin_source;

#endif /* __PERLIN_SOURCE_H__ */
