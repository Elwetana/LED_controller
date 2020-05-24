#define INT_TO_FLOAT 3.0517578125e-05 // = 1. / 32768.

typedef struct DiscoSource
{
	BasicSource basic_source;
	snd_pcm_t* capture_handle;
	snd_pcm_format_t format;
	unsigned int samplerate;			//!< Sample rate
	unsigned int samples_per_frame;		//!< effectively length of buffer
	int16_t* hw_read_buffer;			//!< buffer for capture data, our format S16_LE is signed 16 bit integer in Little Endian
	aubio_tempo_t* aubio_tempo;			//!< aubio tempo object, shrouded in mystery
	fvec_t* tempo_in;					//!< input buffer for tempo, apparently array of floats in range <-1; 1>
	fvec_t* tempo_out;					//!< tempo object writes something here, God only knows what
} DiscoSource;


void DiscoSource_init(int n_leds, int time_speed);
void DiscoSource_destruct();
//returns 1 if leds were updated, 0 if update is not necessary
int DiscoSource_update_leds(int frame, ws2811_t* ledstrip);