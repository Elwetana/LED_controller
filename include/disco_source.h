#ifndef __DISCO_SOURCE_H__
#define __DISCO_SOURCE_H__

#ifdef __linux__
#include <alsa/asoundlib.h>
#include <aubio/aubio.h>
#else
#include "sound/fakealsa.h"
#include "aubio.h"
#endif // __linux__


#define INT_TO_FLOAT         3.0517578125e-05f // = 1. / 32768.
#define ONSET_THRESHOLD      0.1f     //a value between 0.1 (more detections) and 1 (less); default=0.3
#define FQ_BASS             250		  //< upper bound for basses
#define FQ_MID_BASS        1000       //< upper bound for bass to mid
#define FQ_MID_TREBLE      3000       //< lower boudn for mid to treble
#define FQ_TREBLE          6000		  //< lower bound for trebles
#define FQ_NORM               2.5f    //< this will normalize sum of magnitudes
#define BPM_SLOW            75        //< lower than this is slow bpm
#define BPM_FAST            95        //< faster than this is fast bpm
#define FFT_WINDOW           4        //< how many times is the whole FFT window wider than one frame
#define FQ_STAT_LEN        100        //< how many bins we have to gather statistics
#define BPM_MAX            200

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

extern DiscoSource disco_source;
extern struct ArgOptions arg_options;

#endif /* __DISCO_SOURCE_H__ */