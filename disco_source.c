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

#include "colours.h"
#include "common_source.h"
#include "disco_source_priv.h"
#include "disco_source.h"
#include "led_main.h"

//#define AUBIODBG
#define DISCODBG

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
static int n_bands;
static float fq_norm = FQ_NORM;
static uint_t* band_boundaries;
#ifdef AUBIODBG
static aubio_sink_t *snk = NULL;
FILE* fsnk = NULL; 
#endif

void sound_hw_init()
{
    /*
     * On relationship between framerate, samplerate and samples_per_frame
     * - samples_per_frame must be power of 2
     * - frame rate is given
     * - therefore only samplerate can be changed, it is samples_per_frame * framerate
     * The samplerate above is our target, but the actual samplerate will be different
     */
    unsigned int framerate = 1e6 / arg_options.frame_time;
    disco_source.samplerate = 44100;
    disco_source.samples_per_frame = disco_source.samplerate / framerate;
    if (disco_source.samples_per_frame < 512) disco_source.samples_per_frame = 512;
    else if (disco_source.samples_per_frame < 1024) disco_source.samples_per_frame = 1024;
    else disco_source.samples_per_frame = 2048;
    disco_source.samplerate = disco_source.samples_per_frame * framerate;

#ifdef __linux__
    int err;
    snd_pcm_hw_params_t* hw_params;

    char* pcm_name;
    //pcm_name=strdup("plughw:0,0");
    pcm_name = strdup("default:CARD=sndrpihifiberry"); //!< Name of the card, this could be in config or command line, but I don't really care right now
    disco_source.format = SND_PCM_FORMAT_S16_LE;
    int dir = 0;                      //!< exact_rate == samplerate --> dir = 0, exact_rate < samplerate  --> dir = -1, exact_rate > samplerate  --> dir = 1 

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
#else
    fprintf(stderr, "Running on Windows\n");
    //TODO add windows sound
#endif
}

void aubio_init()
{
    int buffer_length = disco_source.samples_per_frame * snd_pcm_format_width(disco_source.format) / 8 * 2;
    disco_source.hw_read_buffer = malloc(buffer_length);
    fprintf(stderr, "Buffer allocated with length %i\n", buffer_length);

    disco_source.tempo_out = new_fvec(1);
    disco_source.tempo_in = new_fvec(disco_source.samples_per_frame);
    disco_source.aubio_tempo = new_aubio_tempo("default", FFT_WINDOW * disco_source.samples_per_frame, disco_source.samples_per_frame, disco_source.samplerate);
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

void set_bin_to_sum()
{
    uint_t length = FFT_WINDOW * disco_source.samples_per_frame;
    int iBand = n_bands - 1;
    printf("Setting fq bands:");
    for (int iGrain = length - 1; iGrain >= 0; --iGrain) {
        if (iGrain < band_boundaries[iBand]) {
            iBand--;
            printf(" %i", iGrain);
        }
        bin_to_sum[iGrain] = fq_sums + iBand;
    }
    printf("\n");
}

void freq_map_init()
{
    uint_t length = FFT_WINDOW * disco_source.samples_per_frame;
    int freq_bands[] = { 0, FQ_BASS, FQ_MID_BASS, FQ_MID_TREBLE, FQ_TREBLE };
    n_bands = sizeof(freq_bands) / sizeof(int);
    fq_sums = malloc(sizeof(struct fq_sum) * n_bands);
    bin_to_sum = malloc(sizeof(struct fq_sum*) * length);
    band_boundaries = malloc(sizeof(uint_t) * n_bands);

    int iBand = n_bands - 1;
    for (int iGrain = length - 1; iGrain >= 0; --iGrain) {
        smpl_t freq = aubio_bintofreq((float)iGrain, (float)disco_source.samplerate, 2.0f * length + 1.0f);
        if (freq < freq_bands[iBand]) {
            band_boundaries[iBand] = iGrain;
            iBand--;
        }
    }
    band_boundaries[0] = 0;
    set_bin_to_sum();
}

void DiscoSource_init(int n_leds, int time_speed)
{
    BasicSource_init(&disco_source.basic_source, n_leds, time_speed, source_config.colors[DISCO_SOURCE]);
    sound_hw_init();
    aubio_init();
    freq_map_init();
}

void DiscoSource_destruct()
{
    del_aubio_tempo(disco_source.aubio_tempo);
    del_fvec(disco_source.tempo_in);
    del_fvec(disco_source.tempo_out);
#ifdef __linux__
    snd_pcm_close(disco_source.capture_handle);
#endif
    free(disco_source.hw_read_buffer);
    free(fq_sums);
    free(bin_to_sum);
    free(band_boundaries);
}

/**
 * Read data from hardware and convert signed 16 bit integer to floats in disco_source.tempo_in
 */
int read_sound_data()
{
#ifdef __linux__
    int err;
    if ((err = snd_pcm_readi(disco_source.capture_handle, disco_source.hw_read_buffer, disco_source.samples_per_frame)) != (int)disco_source.samples_per_frame) {
        fprintf(stderr, "Read from audio interface failed (%s)\n", snd_strerror(err));
        //exit(1);
        snd_pcm_close(disco_source.capture_handle);
        sound_hw_init();
        return 0;
    }
#endif
    for (unsigned int sample = 0; sample < disco_source.samples_per_frame; ++sample)
    {
        int16_t left_val = disco_source.hw_read_buffer[2 * sample];
        int16_t right_val = disco_source.hw_read_buffer[2 * sample + 1];
        float avg_val = ((left_val + right_val) / 2.0f) * INT_TO_FLOAT;
        //float avg_val = left_val * INT_TO_FLOAT;
        if (avg_val * avg_val > 1.0f) {
            fprintf(stderr, "Wrong sample: %f\n", avg_val);
        }
        fvec_set_sample(disco_source.tempo_in, avg_val, sample);
    }
#ifdef AUBIODBG    
    fwrite(disco_source.hw_read_buffer, sizeof(int16_t), 2 * disco_source.samples_per_frame, fsnk);
    aubio_sink_do(snk, disco_source.tempo_in, disco_source.samples_per_frame);
    if (frame == 1000) {
        aubio_sink_close(snk);
        fclose(fsnk);
        exit(0);
    }
    return 0;
#endif    
    return 1;
}

void recalibrate_fq_boundaries(smpl_t* fq_statistics)
{
    float magn_per_band = fq_statistics[99] / n_bands;
    float sum = 0;
    int bin = 0;
    int iBand = 1;
    while (1) {
        sum += fq_statistics[bin];
        while (sum > magn_per_band) {
            float overfill = sum - magn_per_band;
            band_boundaries[iBand++] = 100 * bin - (int)(100 * overfill/fq_statistics[bin]);
            sum = overfill;
        }
        bin++;
        if(iBand == n_bands)
            break;
    }
}


int DiscoSource_update_leds(int frame, ws2811_t* ledstrip)
{
#ifdef DISCODBG
    static int hsldist[8][11]; //0:3 -- last 1000 frames, 3 -- bpm, crashes, 4:8 -- total; column 11 is for totals
#endif
    (void)frame;
    if (!read_sound_data()) {
#ifdef DISCODBG
        hsldist[7][9]++;
#endif        
        return 0;
    }
    if (aubio_silence_detection(disco_source.tempo_in, aubio_tempo_get_silence(disco_source.aubio_tempo))) {
        return 0;
    }
    aubio_tempo_do(disco_source.aubio_tempo, disco_source.tempo_in, disco_source.tempo_out);
    uint_t current_sample = aubio_tempo_get_total_frames(disco_source.aubio_tempo);
    static float last_phase;
    static smpl_t fq_statistics[FQ_STAT_LEN];

    /* Frequency calculations */
    cvec_t* fftgrain = aubio_tempo_get_fftgrain(disco_source.aubio_tempo);
    memset(fq_sums, 0, sizeof(struct fq_sum) * n_bands);
    for (int iGrain = 0; iGrain < fftgrain->length; iGrain++) {
        bin_to_sum[iGrain]->sum += fftgrain->norm[iGrain];
        bin_to_sum[iGrain]->count++;
        fq_statistics[iGrain / 100] += fftgrain->norm[iGrain];
        fq_statistics[FQ_STAT_LEN - 1] += fftgrain->norm[iGrain];
    }

    float fq_max_sum = 0;
    int fq_max_band = 0;
    for (int iBand = 0; iBand < n_bands; ++iBand) {
        if(fq_sums[iBand].sum > fq_max_sum) {
            fq_max_sum = fq_sums[iBand].sum;
            fq_max_band = iBand;
        }
    }
    if(fq_max_sum > (fq_norm * 1.5f)) {
        fq_norm = fq_max_sum / 1.5f;
    }

    if (frame % 10000) {
        recalibrate_fq_boundaries(fq_statistics);
        set_bin_to_sum();
    }


    //printf("Bass: %f, Mid: %f, High: %f\n", fq_sums[0].sum, fq_sums[1].sum, fq_sums[2].sum);

    /* Tempo calculations */
    uint_t last_beat = aubio_tempo_get_last(disco_source.aubio_tempo);
    smpl_t beat_length = aubio_tempo_get_period(disco_source.aubio_tempo);

    /* we must get our current position in beat
     * current_sample - last_beat = how many samples we are in the new beat
     * last_beat + beat_length - current_sample = anticipated next beat  */
    float samples_remaining = last_beat + beat_length - current_sample;

    float phase;
    if (samples_remaining <= 0) {
        //samples_remaining += beat_length;
        phase = last_phase;
    }
    else {
        phase = samples_remaining / (float)beat_length; //this is 1 at the start of the beat and 0 at the end of the beat
    }
    if (phase > 1.0f) {
        phase = 1.0f;
    }
    if (phase > (last_phase)) { //do something at the end of beat
    }
    last_phase = phase;

    /* Calculate color. In color gradient we have only n_bands colors for different frequencies
     * Now we shall calculate the real color to use -- it will be used for all LEDs.
     * The hue is taken from our current band. The lightness is taken from phase, i.e. at the end
     * of beat (phase == 0), all LEDs are white. The saturation is taken from current intensity
     * (fq_sum_max), so when intensity is 0, all LEDs will be black. However, we additionally 
     * stipulate that saturation is always >= phase, so mid-beat, all LEDs will be murky hue based
     * on dominant band. To recap: end of phase => all white, start of phase & max intensity =>
     * pure hue, start of phase & no intensity => black, mid-beat & max intensity => lighter color,
     * mid-beat & no intensity => dark color.                                                       */
    float bpm = aubio_tempo_get_bpm(disco_source.aubio_tempo);
    int bpmrange = 0;
    if(bpm > BPM_SLOW) bpmrange++;
    if(bpm > BPM_FAST) bpmrange++;
    ws2811_led_t color = disco_source.basic_source.gradient.colors[bpmrange * 5 + fq_max_band];
    float hsl[3];
    rgb2hsl(color, hsl);
    hsl[2] = (1.0f - phase) * 0.5f;
    hsl[2] *= hsl[2];
    float intensity = fq_max_sum / fq_norm;
    hsl[1] = (intensity > 1) ? 1 : (intensity > (1.0f - phase) ? intensity : 1.0f - phase);
    //printf("band: %i color %x, s %f, l %f\n", fq_max_band, color, hsl[1], hsl[2]);
    color = hsl2rgb(hsl);
#ifdef DISCODBG
    hsldist[0][fq_max_band]++;
    for(int i = 1; i < 3; ++i) {
        int j = (int)(hsl[i] * 10);
        hsldist[i][j]++;
    }
    hsldist[3][bpmrange]++;
    if(frame % 1000 == 0) {
        printf("%i, bpm:%f, crashes: %i\n", current_sample, aubio_tempo_get_bpm(disco_source.aubio_tempo), hsldist[7][9]);
        for(int i = 0; i < 4; ++i) {
            hsldist[i+4][10] += 1000;
            printf("%c", "hslB"[i]);
            for(int j = 0; j < 10; ++j) {
                hsldist[i+4][j] += hsldist[i][j];
                printf(" %5.1f/%-4.0f%%", hsldist[i][j] / 10.0f, 100.0f * hsldist[i+4][j]/(float)hsldist[i+4][10]);
                hsldist[i][j] = 0;
            }
            printf("\n");
        }
    }
#endif    

    for (int led = 0; led < disco_source.basic_source.n_leds; ++led)
    {
        ledstrip->channel[0].leds[led] = color;
    }
    return 1;
}
