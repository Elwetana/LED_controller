#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#ifdef __linux__
#include "ws2811.h"
#include <alsa/asoundlib.h>
#include <aubio/aubio.h>
#else
#include "fakeled.h"
#include "sound/fakealsa.h"
#endif // __linux__

#include "common_source.h"
#include "disco_source_priv.h"
#include "disco_source.h"
#include "led_main.h"

SourceFunctions disco_functions = {
    .init = DiscoSource_init,
    .update = DiscoSource_update_leds,
    .destruct = DiscoSource_destruct
};

static DiscoSource disco_source;

void sound_hw_init(unsigned int framerate)
{
    int err;
    snd_pcm_hw_params_t* hw_params;

    char* pcm_name;
    //pcm_name=strdup("plughw:0,0");
    pcm_name = strdup("default:CARD=sndrpihifiberry"); //!< Name of the card, this could be in config or command line, but I don't really care right now
    disco_source.samplerate = 44100;
    disco_source.format = SND_PCM_FORMAT_S16_LE;
    int dir = 0;                      //!< exact_rate == samplerate --> dir = 0, exact_rate < samplerate  --> dir = -1, exact_rate > samplerate  --> dir = 1 

    /* 
     * On relationship between framerate, samplerate and samples_per_frame
     * - samples_per_frame must be power of 2
     * - frame rate is given
     * - therefore only samplerate can be changed, it is samples_per_frame * framerate
     * The samplerate above is our target, but the actual samplerate will be different
     */

    disco_source.samples_per_frame = disco_source.samplerate / framerate;
    if (disco_source.samples_per_frame < 512) disco_source.samples_per_frame = 512;
    else if (disco_source.samples_per_frame < 1024) disco_source.samples_per_frame = 1024;
    else disco_source.samples_per_frame = 2048;
    disco_source.samplerate = disco_source.samples_per_frame * framerate;

    // Now we can try opening audio device and setting all parameters
    if ((err = snd_pcm_open(&disco_source.capture_handle, pcm_name, SND_PCM_STREAM_CAPTURE, 0)) < 0) {
        fprintf(stderr, "cannot open audio device %s (%s)\n", pcm_name,snd_strerror(err));
        exit(1);
    }
    fprintf(stderr, "PCM open\n");

    snd_pcm_hw_params_malloc(&hw_params);
    fprintf(stderr, "HW params allocated\n");

    if ((err = snd_pcm_hw_params_any(disco_source.capture_handle, hw_params)) < 0) {
        fprintf(stderr, "cannot initialize hardware parameter structure (%s)\n", snd_strerror(err));
        exit(1);
    }
    fprintf(stderr, "HW params initialized\n");

    if ((err = snd_pcm_hw_params_set_access(disco_source.capture_handle, hw_params, SND_PCM_ACCESS_RW_INTERLEAVED)) < 0) {
        fprintf(stderr, "cannot set access type (%s)\n", snd_strerror(err));
        exit(1);
    }
    fprintf(stderr, "Access set\n");

    if ((err = snd_pcm_hw_params_set_format(disco_source.capture_handle, hw_params, disco_source.format)) < 0) {
        fprintf(stderr, "cannot set sample format (%s)\n", snd_strerror(err));
        exit(1);
    }
    fprintf(stderr, "Format set\n");

    if ((err = snd_pcm_hw_params_set_rate_near(disco_source.capture_handle, hw_params, &disco_source.samplerate, &dir)) < 0) {
        fprintf(stderr, "cannot set sample rate (%s)\n", snd_strerror(err));
        exit(1);
    }
    fprintf(stderr, "Sample rate set to %i\n", disco_source.samplerate);
    //TODO check dir and recalculate samples_per_frame if required

    if ((err = snd_pcm_hw_params_set_channels(disco_source.capture_handle, hw_params, 2)) < 0) {
        fprintf(stderr, "cannot set channel count (%s)\n", snd_strerror(err));
        exit(1);
    }
    fprintf(stderr, "Channels set\n");

    if ((err = snd_pcm_hw_params(disco_source.capture_handle, hw_params)) < 0) {
        fprintf(stderr, "cannot set parameters (%s)\n", snd_strerror(err));
        exit(1);
    }
    fprintf(stderr, "Parameters set to handle\n");

    snd_pcm_hw_params_free(hw_params);
    fprintf(stderr, "HW initialized\n");

    if ((err = snd_pcm_prepare(disco_source.capture_handle)) < 0) {
        fprintf(stderr, "cannot prepare audio interface for use (%s)\n", snd_strerror(err));
        exit(1);
    }
    fprintf(stderr, "Interface prepared\n");
}

void aubio_init()
{
    int buffer_length = disco_source.samples_per_frame * snd_pcm_format_width(disco_source.format) / 8 * 2;
    disco_source.hw_read_buffer = malloc(buffer_length);
    fprintf(stderr, "Buffer allocated with length %i\n", buffer_length);

    disco_source.tempo_out = new_fvec(1);
    disco_source.tempo_in = new_fvec(disco_source.samples_per_frame);
    disco_source.aubio_tempo = new_aubio_tempo("default", 4 * disco_source.samples_per_frame, disco_source.samples_per_frame, disco_source.samplerate);
    aubio_tempo_set_threshold(disco_source.aubio_tempo, ONSET_THRESHOLD);
    fprintf(stderr, "Aubio objects initiated\n");
}

void DiscoSource_init(int n_leds, int time_speed)
{
    BasicSource_init(&disco_source.basic_source, n_leds, time_speed, source_config.colors[DISCO_SOURCE]);
    disco_source.basic_source.gradient.colors[99] = 0xFF0000;
    unsigned int framerate = 1e6 / arg_options.frame_time;
    sound_hw_init(framerate);
    aubio_init();
}


void DiscoSource_destruct()
{
    del_aubio_tempo(disco_source.aubio_tempo);
    del_fvec(disco_source.tempo_in);
    del_fvec(disco_source.tempo_out);
    snd_pcm_close(disco_source.capture_handle);
    free(disco_source.hw_read_buffer);
}


int DiscoSource_update_leds(int frame, ws2811_t* ledstrip)
{
    int err;
    if ((err = snd_pcm_readi(disco_source.capture_handle, disco_source.hw_read_buffer, disco_source.samples_per_frame)) != (int)disco_source.samples_per_frame) {
        fprintf(stderr, "Read from audio interface failed (%s)\n", snd_strerror(err));
        exit(1);
    }

    for (unsigned int sample = 0; sample < disco_source.samples_per_frame; ++sample) 
    {
        int16_t left_val  = disco_source.hw_read_buffer[2 * sample];
        int16_t right_val = disco_source.hw_read_buffer[2 * sample + 1];
        float avg_val = ((left_val + right_val) / 2) * INT_TO_FLOAT;
        //float avg_val = left_val * INT_TO_FLOAT;
        if (avg_val * avg_val > 1.0f) {
            fprintf(stderr, "Wrong sample: %f\n", avg_val);
        }
        fvec_set_sample(disco_source.tempo_in, avg_val, sample);
    }
    aubio_tempo_do(disco_source.aubio_tempo, disco_source.tempo_in, disco_source.tempo_out);
    //TODO detect silence, return immediately

    unsigned int current_sample = frame * disco_source.samples_per_frame;
    unsigned int beat_length = aubio_tempo_get_period(disco_source.aubio_tempo);
    unsigned int last_beat = aubio_tempo_get_last(disco_source.aubio_tempo);
    if (last_beat > current_sample) {
        fprintf(stderr, "Last beat does not work as I thought it does\n");
    }
    /* we must get our current position in beat 
    current_sample - last_beat = how many samples we are in the new beat
    last_beat + beat_length - current_sample = anticipated next beat
    */
    int samples_remaining = last_beat + beat_length - current_sample;
    if(samples_remaining < 0) {
        samples_remaining += beat_length;
    }
    if (samples_remaining < 0 || (unsigned int)samples_remaining > beat_length) {
        fprintf(stderr, "Beat length does not work as I thougth it does. Samples remaining: %i, beat length: %i\n", samples_remaining, beat_length);
    }
    float phase = (float)samples_remaining / (float)beat_length;
    int color_index = (int)(50 * phase);
    if(disco_source.tempo_out->data[0] != 0) {
        //printf("Beat detected\n");
        disco_source.beat_decay = 2;
    }
    if(disco_source.beat_decay-- > 0) {
        color_index = 99;
    }
    for (int led = 0; led < disco_source.basic_source.n_leds; ++led)
    {
        ledstrip->channel[0].leds[led] = disco_source.basic_source.gradient.colors[color_index];
    }
    return 1;
}
