#define INT_TO_FLOAT 3.0517578125e-05 // = 1. / 32768.
#define ONSET_THRESHOLD     0.1       //a value between 0.1 (more detections) and 1 (less); default=0.3
#define FQ_BASS             300		  //< upper bound for basses
#define FQ_TREBLE          2000		  //< lower bound for trebles
#define FQ_NORM            2500.0f    //< this will normalize sum of magnitudes
#define GRAD_STEPS           10		  //< how many steps there are for each frequence and tempo

typedef struct DiscoSource
{
	BasicSource basic_source;
    int beat_decay;
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
