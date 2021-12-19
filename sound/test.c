#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef __linux__

#include <alsa/asoundlib.h>
#include <aubio/aubio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <net/if.h>
#include <arpa/inet.h>

#define INT_TO_FLOAT 3.0517578125e-05 // = 1. / 32768.

uint32_t get_ip()
{
     int fd;
     struct ifreq ifr;
     fd = socket(AF_INET, SOCK_DGRAM, 0);

     /* I want to get an IPv4 IP address */
     ifr.ifr_addr.sa_family = AF_INET;

     /* I want IP address attached to "eth0" */
     strncpy(ifr.ifr_name, "wlan0", IFNAMSIZ-1);

     ioctl(fd, SIOCGIFADDR, &ifr);

     close(fd);

     /* display result */
     //printf("%s\n", inet_ntoa(((struct sockaddr_in *)&ifr.ifr_addr)->sin_addr));
     return ((struct sockaddr_in *)&ifr.ifr_addr)->sin_addr.s_addr;
}

int main(int argc, char* argv[])
{
    uint32_t ip = get_ip();
    unsigned char s1 = (ip & 0xFF);
    unsigned char s2 = (ip & 0xFF00) >> 8;
    unsigned char s3 = (ip & 0xFF0000) >> 16;
    unsigned char s4 = (ip & 0xFF000000) >> 24;
    printf("%i\n", ip);
    printf("%i.%i\n", s1, s2);

    return 0;

    int i;
    int err;
    snd_pcm_t *capture_handle;
    snd_pcm_hw_params_t *hw_params;

    unsigned int rate = 44100; /* Sample rate */
    int dir = 0;      /* exact_rate == rate --> dir = 0 */
                      /* exact_rate < rate  --> dir = -1 */
                      /* exact_rate > rate  --> dir = 1 */
	snd_pcm_format_t format = SND_PCM_FORMAT_S16_LE;

    int16_t *buffer;
	int buffer_frames = 1024;


    /* Name of the PCM device, like plughw:0,0          */
    /* The first number is the number of the soundcard, */
    /* the second number is the number of the device.   */
    char *pcm_name;
    pcm_name = strdup("default:CARD=sndrpihifiberry");
    //pcm_name=strdup("plughw:0,0");

    /* Open PCM. The last parameter of this function is the mode. */
    /* If this is set to 0, the standard mode is used. Possible   */
    /* other values are SND_PCM_NONBLOCK and SND_PCM_ASYNC.       */ 
    /* If SND_PCM_NONBLOCK is used, read / write access to the    */
    /* PCM device will return immediately. If SND_PCM_ASYNC is    */
    /* specified, SIGIO will be emitted whenever a period has     */
    /* been completely processed by the soundcard.                */
    if ((err = snd_pcm_open (&capture_handle, pcm_name, SND_PCM_STREAM_CAPTURE, 0)) < 0) {
        fprintf (stderr, "cannot open audio device %s (%s)\n", 
             pcm_name,
             snd_strerror (err));
        exit (1);
    }
    printf("PCM open\n");

    snd_pcm_hw_params_malloc(&hw_params);

    printf("HW params allocated\n");

    if ((err = snd_pcm_hw_params_any (capture_handle, hw_params)) < 0) {
        fprintf (stderr, "cannot initialize hardware parameter structure (%s)\n",
        snd_strerror (err));
        exit (1);
    }
    printf("HW params initialized\n");

    if ((err = snd_pcm_hw_params_set_access (capture_handle, hw_params, SND_PCM_ACCESS_RW_INTERLEAVED)) < 0) {
        fprintf (stderr, "cannot set access type (%s)\n",
        snd_strerror (err));
        exit (1);
    }
    printf("Access set\n");

    if ((err = snd_pcm_hw_params_set_format (capture_handle, hw_params, format)) < 0) {
        fprintf (stderr, "cannot set sample format (%s)\n",
        snd_strerror (err));
        exit (1);
    }
    printf("Format set\n");

    //int desired_rate = rate;
    if ((err = snd_pcm_hw_params_set_rate_near (capture_handle, hw_params, &rate, &dir)) < 0) {
        fprintf (stderr, "cannot set sample rate (%s)\n",
        snd_strerror (err));
        exit (1);
    }
    printf("Sample rate set to %i\n", rate);

    if ((err = snd_pcm_hw_params_set_channels (capture_handle, hw_params, 2)) < 0) {
        fprintf (stderr, "cannot set channel count (%s)\n",
        snd_strerror (err));
        exit (1);
    }
    printf("Channels set\n");

    if ((err = snd_pcm_hw_params (capture_handle, hw_params)) < 0) {
        fprintf (stderr, "cannot set parameters (%s)\n",
        snd_strerror (err));
        exit (1);
    }
    printf("Parameters set to handle\n");

    snd_pcm_hw_params_free (hw_params);

    printf("HW initialized\n");

    if ((err = snd_pcm_prepare (capture_handle)) < 0) {
        fprintf (stderr, "cannot prepare audio interface for use (%s)\n",
        snd_strerror (err));
        exit (1);
    }
    printf("Interface prepared\n");


    int buffer_length = buffer_frames * snd_pcm_format_width(format) / 8 * 2;
    buffer = malloc(buffer_length);

    fprintf(stdout, "buffer allocated with length %i\n", buffer_length);

    //int hop_size = buffer_frames / 4;
    fvec_t * tempo_out;
    tempo_out = new_fvec(2);
    fvec_t * in = new_fvec (buffer_frames); // input audio buffer
    aubio_tempo_t * o = new_aubio_tempo("default", 4 * buffer_frames, buffer_frames, rate);
    printf("Starting capture\n");
    uint64_t last_update_ns = 0;
    uint64_t fps_time_ns = 0;
    for (i = 0; i < 10000; ++i) {
        if ((err = snd_pcm_readi (capture_handle, buffer, buffer_frames)) != buffer_frames) {
            fprintf (stderr, "read from audio interface failed (%s)\n",
            snd_strerror (err));
            exit (1);
        }
        //printf("Reading %i\n", i);
        float sum = 0;
        for(int frame=0; frame < buffer_frames; ++frame) {
            int16_t left_sample = buffer[2*frame];
            int16_t right_sample = buffer[2*frame + 1];
            float sample = ((float)(left_sample + right_sample)) * INT_TO_FLOAT / 2.0f;
            if(sample * sample > 1.0f) {
                fprintf(stderr, "Wrong sample: %f\n", sample);
            }
            fvec_set_sample(in, sample, frame);
            sum += sample;
        }
        aubio_tempo_do(o, in, tempo_out);
        if(tempo_out->data[0] != 0) {
            float bpm = aubio_tempo_get_bpm(o);
            unsigned int lastbeat = aubio_tempo_get_last(o);
            printf("sum %f, Tempo 0: %f, bpm %f, ms %i\n", sum, tempo_out->data[0], bpm, lastbeat);
        }
        if(i % 50 == 0)
        {
	        struct timespec now;
    	    clock_gettime(CLOCK_MONOTONIC_RAW, &now);			
			uint64_t current_ns = now.tv_sec * (long long)1e9 + now.tv_nsec;
            double fps = (double)50 / (double)(current_ns - fps_time_ns) * 1e9;
            printf("FPS: %f\n", fps);
            fps_time_ns = current_ns;
        }
    }
    del_aubio_tempo(o);
    del_fvec(in);
    del_fvec(tempo_out);

    printf("Closing handle\n");
    snd_pcm_close (capture_handle);
    return 0; 
 }

#endif // __linux__
