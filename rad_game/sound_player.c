#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef __linux__
#include <alsa/asoundlib.h>
#else
#include "fakealsa.h"
#include "faketime.h"
#endif

#include "sound_player.h"

struct SoundEffect
{
    short* data;
    int n_samples; //< this is not length of data; length of data is n_samples * channels
    int position;
};

static unsigned int samplerate;
static unsigned int channels;
static snd_pcm_t *pcm_handle;
static snd_pcm_uframes_t period_size;
static char* buff;
static uint64_t start_ns;
static uint64_t last_update_ns; 
static uint64_t delta_disk_us = 0; 
static uint64_t delta_pcm_us = 0;
static long samples_supplied = 0;
static int samples_in_buffer;
static FILE* fin;

static struct SoundEffect effects[SE_N_EFFECTS];
static enum ESoundEffects current_effect = SE_N_EFFECTS;

#ifdef __linux__

static void init_hw()
{
    int err;
    snd_pcm_hw_params_t* hw_params;

    char* pcm_name;
    pcm_name = strdup("default:CARD=sndrpihifiberry"); //!< Name of the card, this could be in config or command line, but I don't really care right now
    int format = SND_PCM_FORMAT_S16_LE;
    int dir = 0;                     				   //!< exact_rate == samplerate --> dir = 0, exact_rate < samplerate  --> dir = -1, exact_rate > samplerate  --> dir = 1 

    // Open the PCM device in playback mode
    if ((err = snd_pcm_open(&pcm_handle, pcm_name, SND_PCM_STREAM_PLAYBACK, 0)) < 0) {
        fprintf(stderr, "cannot open audio device %s for playback (%s)\n", pcm_name,snd_strerror(err));
        exit(1);
    }
#ifdef DEBUG_SOUND    
    fprintf(stderr, "PCM open\n");
#endif

	// Allocate parameters object and fill it with default values
    snd_pcm_hw_params_malloc(&hw_params);
    fprintf(stderr, "HW params allocated\n");

    if ((err = snd_pcm_hw_params_any(pcm_handle, hw_params)) < 0) {
        fprintf(stderr, "cannot initialize hardware parameter structure (%s)\n", snd_strerror(err));
        exit(1);
    }
#ifdef DEBUG_SOUND
    fprintf(stderr, "HW params initialized\n");
#endif

    if ((err = snd_pcm_hw_params_set_access(pcm_handle, hw_params, SND_PCM_ACCESS_RW_INTERLEAVED)) < 0) {
        fprintf(stderr, "cannot set access type (%s)\n", snd_strerror(err));
        exit(1);
    }
#ifdef DEBUG_SOUND
    fprintf(stderr, "Access set\n");
#endif

    if ((err = snd_pcm_hw_params_set_format(pcm_handle, hw_params, format)) < 0) {
        fprintf(stderr, "cannot set sample format (%s)\n", snd_strerror(err));
        exit(1);
    }
#ifdef DEBUG_SOUND
    fprintf(stderr, "Format set\n");
#endif

    if ((err = snd_pcm_hw_params_set_channels(pcm_handle, hw_params, channels)) < 0) {
        fprintf(stderr, "cannot set channel count (%s)\n", snd_strerror(err));
        exit(1);
    }
#ifdef DEBUG_SOUND
    fprintf(stderr, "Channels set\n");
#endif

    if ((err = snd_pcm_hw_params_set_rate_near(pcm_handle, hw_params, &samplerate, &dir)) < 0) {
        fprintf(stderr, "cannot set sample rate (%s)\n", snd_strerror(err));
        exit(1);
    }
#ifdef DEBUG_SOUND
    fprintf(stderr, "Sample rate set to %i\n", samplerate);
#endif
    //TODO check dir and recalculate samples_per_frame if required

	// Write parameters
    if ((err = snd_pcm_hw_params(pcm_handle, hw_params)) < 0) {
        fprintf(stderr, "cannot set parameters (%s)\n", snd_strerror(err));
        exit(1);
    }
#ifdef DEBUG_SOUND
    fprintf(stderr, "Parameters set to handle\n");
#endif

    snd_pcm_hw_params_free(hw_params);
#ifdef DEBUG_SOUND
    fprintf(stderr, "HW initialized\n");
#endif

    if ((err = snd_pcm_prepare(pcm_handle)) < 0) {
        fprintf(stderr, "cannot prepare audio interface for use (%s)\n", snd_strerror(err));
        exit(1);
    }
#ifdef DEBUG_SOUND
    fprintf(stderr, "Interface prepared\n");
#endif
	printf("PCM state: %s\n", snd_pcm_state_name(snd_pcm_state(pcm_handle)));

    unsigned int tmp;
	snd_pcm_hw_params_get_channels(hw_params, &tmp);
    assert(tmp == channels);

	snd_pcm_hw_params_get_rate(hw_params, &tmp, 0);
    assert(tmp == samplerate);

	snd_pcm_hw_params_get_period_size(hw_params, &period_size, 0);
#ifdef DEBUG_SOUND
	snd_pcm_hw_params_get_period_time(hw_params, &tmp, NULL);
    printf("Period time: %i, size %i\n", tmp, (int)period_size);
#endif
}
#else

static void init_hw()
{
}
#endif __linux__

static void start_playing(int frame_time, char* filename)
{
    //Calculate buff_size for our desired FPS
    // e.g. frame_time = 20000 (in us) is 50 fps
    // we have samplerate * channels * 2 samples per second
    // therefore we need to get
    //   (samplerate * channels * 2) / fps = (samplerate * channels * 2) * frame_time / 1e6 
    // samples to buffer per frame
    unsigned int periods_per_frame = 1 + samplerate * frame_time / 1e6 / period_size;
#ifdef DEBUG_SOUND
    printf("Multiplier set to: %i\n", periods_per_frame);
#endif
    samples_in_buffer = periods_per_frame * period_size;  /* 2 -> sample size */;
	buff = (char*) malloc(samples_in_buffer * channels * 2);

    //open input file
    //FILE* fin = fopen("GodRestYeMerryGentlemen.wav", "r");
    fin = fopen(filename, "r");
    fseek(fin, 44, SEEK_SET);
    
    struct timespec start;
    clock_gettime(CLOCK_MONOTONIC_RAW, &start);
    start_ns = start.tv_sec * (long long)1e9 + start.tv_nsec;
    last_update_ns = start_ns;
    delta_disk_us = 0; 
    delta_pcm_us = 0;
    samples_supplied = 0;
}

void load_effects()
{
    const int max_effect_length = 2; //!< in seconds
    char* tmp = (char)malloc(samplerate * channels * max_effect_length * 2); //*2 for sample size
    for (int i = 0; i < SE_N_EFFECTS; ++i)
    {
        FILE* feff = fopen("sound/reward2_2.wav", "r");
        fseek(feff, 44, SEEK_SET);
        int samples_read = fread(tmp, channels * 2, samplerate * max_effect_length, feff);
        fclose(feff);
        assert(sizeof(short) == 2);
        effects[i].data = (short*)malloc(sizeof(short) * samples_read * channels);
        for (int sample = 0; i < samples_read * channels; i++)
        {
            effects[i].data[sample] = tmp[sample * 2 + 1] << 8 | tmp[sample * 2];
        }
        //memcpy(effects[i].data, tmp, samples_read * channels * 2);
        effects[i].n_samples = samples_read;
        effects[i].position = -1;
    }
}

void SoundPlayer_init(unsigned int in_samplerate, unsigned int in_channels, int frame_time, char* filename)
{
    samplerate = in_samplerate;
    channels = in_channels;
    init_hw();
    load_effects();
    start_playing(frame_time, filename);
}

long SoundPlayer_play(enum ESoundEffects new_effect)
{
    if (new_effect != SE_N_EFFECTS)
    {
        current_effect = new_effect;
        effects[current_effect].position = 0;
    }
    int err;
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC_RAW, &now);
    uint64_t current_ns = now.tv_sec * (long long)1e9 + now.tv_nsec;
    uint64_t time_running_us = (current_ns - start_ns) / (long)1e3;
    long samples_consumed = time_running_us * samplerate / 1e6;
    if(samples_supplied - samples_consumed < samples_in_buffer) 
    {
        int samples_read = fread(buff, channels * 2, samples_in_buffer, fin);
        if (samples_read > 0) 
        {
#ifdef DEBUG_SOUND
            clock_gettime(CLOCK_MONOTONIC_RAW, &now);
            current_ns = now.tv_sec * (long long)1e9 + now.tv_nsec;
            delta_disk_us = (current_ns - last_update_ns) / (long)1e3;
            last_update_ns = current_ns;
#endif
            if (current_effect != SE_N_EFFECTS)
            {
                int effect_offset = effects[current_effect].position;
                int samples_to_modify = effects[current_effect].n_samples - effect_offset;
                if (samples_to_modify > samples_read)
                {
                    samples_to_modify = samples_read;
                }
                for (int sample = 0; sample < samples_to_modify * channels; sample++)
                {
                    short orig = buff[sample * 2 + 1] << 8 | buff[sample * 2];
                    short modified = (orig >> 1) + effects[current_effect].data[effect_offset + sample];
                    buff[sample * 2] = (char)(modified && 0xFF);
                    buff[sample * 2 + 1] = (char)((modified & 0xFF00) >> 8);
                }
                if (samples_to_modify < samples_read)
                {
                    effects[current_effect].position = -1;
                    current_effect = SE_N_EFFECTS;
                }
            }
#ifdef __linux__
            if ((err = snd_pcm_writei(pcm_handle, buff, samples_read)) == -EPIPE) 
            {
                printf("XRUN.\n");
                snd_pcm_prepare(pcm_handle);
            } 
            else if (err < 0) 
            {
                printf("ERROR. Can't write to PCM device. %s\n", snd_strerror(err));
            }
#endif
            samples_supplied += samples_in_buffer; 
#ifdef DEBUG_SOUND            
            clock_gettime(CLOCK_MONOTONIC_RAW, &now);
            current_ns = now.tv_sec * (long long)1e9 + now.tv_nsec;
            delta_pcm_us = (current_ns - last_update_ns) / (long)1e3;
#endif
        }
        else
        {
             printf("Player reached end of file.\n");
#ifdef __linux__
             snd_pcm_drain(pcm_handle);
             snd_pcm_close(pcm_handle);
#endif __linux__
             free(buff);
             return -1;
        }
    }
    else
    {
#ifdef DEBUG_SOUND            
        delta_disk_us = 0;
        delta_pcm_us = 0;
#endif
    }
#ifdef DEBUG_SOUND            
    printf("D %lli, P %lli\n", delta_disk_us, delta_pcm_us);
    clock_gettime(CLOCK_MONOTONIC_RAW, &now);
    current_ns = now.tv_sec * (long long)1e9 + now.tv_nsec;
    last_update_ns = current_ns;
#endif
	return time_running_us;
}
