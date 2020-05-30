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
#include "aubio.h"
#endif // __linux__

#include "common_source.h"
#include "disco_source_priv.h"
#include "disco_source.h"
#include "led_main.h"

#define AUBIODBG

SourceFunctions disco_functions = {
    .init = DiscoSource_init,
    .update = DiscoSource_update_leds,
    .destruct = DiscoSource_destruct
};

static DiscoSource disco_source;
struct fq_sum {
    float sum;
    int count;
};
static struct fq_sum* fq_sums = NULL;
static struct fq_sum** bin_to_sum = NULL;
static int* fq_colors = NULL;
static int n_bands;
static uint_t current_sample;
static float fq_norm = FQ_NORM;
#ifdef AUBIODBG
static aubio_sink_t *snk = NULL;
FILE* fsnk = NULL; 
#endif

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
#ifdef AUBIODBG
    char* sink_path;
    sink_path = strdup("/home/pi/test.wav");
    snk = new_aubio_sink(sink_path, disco_source.samplerate);
    char *fsink_path = strdup("/home/pi/test.raw");
    fsnk = fopen(fsink_path, "wb");
#endif
}

void DiscoSource_init(int n_leds, int time_speed)
{
    BasicSource_init(&disco_source.basic_source, n_leds, time_speed, source_config.colors[DISCO_SOURCE]);
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
    if (fq_sums != NULL) {
        free(fq_sums);
        free(bin_to_sum);
        free(fq_colors);
    }
}

void DiscoSource_freq_map_init(cvec_t* fftgrain)
{
    int freq_bands[] = { 0, FQ_BASS, FQ_TREBLE };
    n_bands = sizeof(freq_bands) / sizeof(int);
    fq_sums = malloc(sizeof(struct fq_sum) * n_bands);
    bin_to_sum = malloc(sizeof(struct fq_sum*) * fftgrain->length);
    for (uint_t iGrain = 0; iGrain < fftgrain->length; iGrain++) {
        smpl_t freq = aubio_bintofreq(iGrain, disco_source.samplerate, 2 * fftgrain->length + 1);
        int i_band = n_bands - 1;
        while (freq < freq_bands[i_band])
            i_band--;
        bin_to_sum[iGrain] = fq_sums + i_band;
    }
    fq_colors = malloc(sizeof(int) * n_bands);
}

/** 
 +-+-+- Tempo LEDs, set to gradient[0:10] depending on phase
 | | | +-+-+- Bass LEDs, set to gradient[10:20] depending on intensity
 | | | | | | +-+-+- Mid LEDs, set to gradient[20:30]
 | | | | | | | | | +-+-+- Treble LEDs, set to gradient [30:40]
 . . . . . . . . . . . .
 0 1 2 3 4 5 6 7 8 9 0 1
*/
int DiscoSource_update_leds(int frame, ws2811_t* ledstrip)
{
    (void)frame;
    static float last_phase;
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
#ifdef AUBIODBG    
    fwrite(disco_source.hw_read_buffer, sizeof(int16_t), 2 * disco_source.samples_per_frame, fsnk);
    aubio_sink_do(snk, disco_source.tempo_in, disco_source.samples_per_frame);
    if(frame == 1000) {
        aubio_sink_close(snk);
        fclose(fsnk);
        exit(0);
    }
    return 0;
#endif    
    int is_silence = aubio_silence_detection(disco_source.tempo_in, aubio_tempo_get_silence(disco_source.aubio_tempo));
    if (is_silence) {
        return 0;
    }
    aubio_tempo_do(disco_source.aubio_tempo, disco_source.tempo_in, disco_source.tempo_out);
    current_sample += disco_source.samples_per_frame;

    /* Frequency calculations */
    cvec_t* fftgrain = aubio_tempo_get_fftgrain(disco_source.aubio_tempo);
    if (fq_sums == NULL) {
        DiscoSource_freq_map_init(fftgrain);
    }
    memset(fq_sums, 0, sizeof(struct fq_sum) * n_bands);
    for (uint_t iGrain = 0; iGrain < fftgrain->length; iGrain++) {
        bin_to_sum[iGrain]->sum += fftgrain->norm[iGrain];
        bin_to_sum[iGrain]->count++;
    }

    float fq_max = 0;
    for (int iBand = 0; iBand < n_bands; ++iBand) {
        if(fq_sums[iBand].sum > fq_max) fq_max = fq_sums[iBand].sum;
        int c = (int)(fq_sums[iBand].sum / fq_norm * GRAD_STEPS);
        c = c >= GRAD_STEPS ? GRAD_STEPS - 1 : c;
        fq_colors[iBand] = c + (iBand + 1) * GRAD_STEPS; //first GRAD_STEPS steps are for tempo
    }
    if(fq_max > (fq_norm * 1.5f)) fq_norm = fq_max / 1.5f;
    printf("Bass: %f, Mid: %f, High: %f\n", fq_sums[0].sum, fq_sums[1].sum, fq_sums[2].sum);
    printf("Bass: %i, Mid: %i, High: %i\n", fq_colors[0], fq_colors[1], fq_colors[2]);

    /* Tempo calculations */
    uint_t last_beat = aubio_tempo_get_last(disco_source.aubio_tempo);
    smpl_t beat_length = aubio_tempo_get_period(disco_source.aubio_tempo);

    /* we must get our current position in beat
     * current_sample - last_beat = how many samples we are in the new beat
     * last_beat + beat_length - current_sample = anticipated next beat  */
    float samples_remaining = last_beat + beat_length - current_sample;

    if (samples_remaining <= 0) {
        //samples_remaining += beat_length;
        if(samples_remaining < 0) {
            //printf("beat %f, last %i, current %i neg samples %f c-l %i\n", beat_length, last_beat, current_sample, samples_remaining, (current_sample - last_beat));
            return 0;
        }
    }
    float phase = samples_remaining / (float)beat_length; //this is 1 at the start of the beat and 0 at the end of the beat
    if (phase > (last_phase)) { //do something at the end of beat
    }
    last_phase = phase;
    int phase_index = (int)(phase * GRAD_STEPS);
    //printf(".%i\n", phase_index);
    for (int led = 0; led < disco_source.basic_source.n_leds; ++led)
    {
        int pos = led % (3 * (n_bands + 1));  // we need 3 leds per fq. band and one for beats
        if(pos < 3) 
            ledstrip->channel[0].leds[led] = disco_source.basic_source.gradient.colors[phase_index];
        else {
            int iBand = (pos - 3) / 3;
            ledstrip->channel[0].leds[led] = disco_source.basic_source.gradient.colors[fq_colors[iBand]];
        }
    }
    return 1;
}
