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
//returns 1 if leds were updated, 0 if update is not necessary
int PerlinSource_update_leds(int frame, ws2811_t* ledstrip);
