#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef __linux__


#include <alsa/asoundlib.h>
#include <aubio/aubio.h>

#define INT_TO_FLOAT 3.0517578125e-05 // = 1. / 32768.


int main(int argc, char* argv[])
{
    int err;
    snd_pcm_hw_params_t* hw_params;
    snd_pcm_t *pcm_handle;

    char* pcm_name;

    pcm_name = strdup("default:CARD=sndrpihifiberry"); //!< Name of the card, this could be in config or command line, but I don't really care right now
    int format = SND_PCM_FORMAT_S16_LE;
    unsigned int samplerate = 44100;
    int dir = 0;                     				   //!< exact_rate == samplerate --> dir = 0, exact_rate < samplerate  --> dir = -1, exact_rate > samplerate  --> dir = 1 
    int channels = 2;
    int seconds = 50;

    // Open the PCM device in playback mode
    if ((err = snd_pcm_open(&pcm_handle, pcm_name, SND_PCM_STREAM_PLAYBACK, 0)) < 0) {
        fprintf(stderr, "cannot open audio device %s for playback (%s)\n", pcm_name,snd_strerror(err));
        exit(1);
    }
    fprintf(stderr, "PCM open\n");


	// Allocate parameters object and fill it with default values
    snd_pcm_hw_params_malloc(&hw_params);
    fprintf(stderr, "HW params allocated\n");

    if ((err = snd_pcm_hw_params_any(pcm_handle, hw_params)) < 0) {
        fprintf(stderr, "cannot initialize hardware parameter structure (%s)\n", snd_strerror(err));
        exit(1);
    }
    fprintf(stderr, "HW params initialized\n");

    if ((err = snd_pcm_hw_params_set_access(pcm_handle, hw_params, SND_PCM_ACCESS_RW_INTERLEAVED)) < 0) {
        fprintf(stderr, "cannot set access type (%s)\n", snd_strerror(err));
        exit(1);
    }
    fprintf(stderr, "Access set\n");

    if ((err = snd_pcm_hw_params_set_format(pcm_handle, hw_params, format)) < 0) {
        fprintf(stderr, "cannot set sample format (%s)\n", snd_strerror(err));
        exit(1);
    }
    fprintf(stderr, "Format set\n");

    if ((err = snd_pcm_hw_params_set_channels(pcm_handle, hw_params, channels)) < 0) {
        fprintf(stderr, "cannot set channel count (%s)\n", snd_strerror(err));
        exit(1);
    }
    fprintf(stderr, "Channels set\n");

    if ((err = snd_pcm_hw_params_set_rate_near(pcm_handle, hw_params, &samplerate, &dir)) < 0) {
        fprintf(stderr, "cannot set sample rate (%s)\n", snd_strerror(err));
        exit(1);
    }
    fprintf(stderr, "Sample rate set to %i\n", samplerate);
    //TODO check dir and recalculate samples_per_frame if required

	// Write parameters
    if ((err = snd_pcm_hw_params(pcm_handle, hw_params)) < 0) {
        fprintf(stderr, "cannot set parameters (%s)\n", snd_strerror(err));
        exit(1);
    }
    fprintf(stderr, "Parameters set to handle\n");

    snd_pcm_hw_params_free(hw_params);
    fprintf(stderr, "HW initialized\n");

    if ((err = snd_pcm_prepare(pcm_handle)) < 0) {
        fprintf(stderr, "cannot prepare audio interface for use (%s)\n", snd_strerror(err));
        exit(1);
    }
    fprintf(stderr, "Interface prepared\n");
	printf("PCM state: %s\n", snd_pcm_state_name(snd_pcm_state(pcm_handle)));

    unsigned int tmp;
	snd_pcm_hw_params_get_channels(hw_params, &tmp);
	printf("channels: %i ", tmp);

	if (tmp == 1)
		printf("(mono)\n");
	else if (tmp == 2)
		printf("(stereo)\n");

	snd_pcm_hw_params_get_rate(hw_params, &tmp, 0);
	printf("Rate %i\n", tmp);

    snd_pcm_uframes_t period_size;
	snd_pcm_hw_params_get_period_size(hw_params, &period_size, 0);
    printf("Period %i\n", period_size);

	//Calculate buff_size for our desired FPS
    int frame_time = 20000; //in us, i.e. 50 fps
    // we have samplerate * channels * 2 samples per second
    // therefore we need to get
    //   (samplerate * channels * 2) / fps = (samplerate * channels * 2) * frame_time / 1e6 
    // samples to buffer per frame
    int periods_per_frame = 1 + samplerate * frame_time / 1e6 / period_size;
    periods_per_frame = 8;
    printf("Multiplier set to: %i\n", periods_per_frame);
    int samples_in_buffer = periods_per_frame * period_size;  /* 2 -> sample size */;
	char* buff = (char *) malloc(samples_in_buffer * channels * 2);

	snd_pcm_hw_params_get_period_time(hw_params, &tmp, NULL);
    printf("Period time: %i\n", tmp);

    //AUBIO setup
    fvec_t * tempo_out;
    tempo_out = new_fvec(2);
    fvec_t * in = new_fvec (samples_in_buffer); // input audio buffer
    aubio_tempo_t * o = new_aubio_tempo("default", 4 * samples_in_buffer, samples_in_buffer, samplerate);

    //open input file
    //FILE* fin = fopen("/home/pi/aubio/build/tests/44100Hz_44100f_sine441_stereo.wav", "r");
    FILE* fin = fopen("GodRestYeMerryGentlemen.wav", "r");
    fseek(fin, 44, SEEK_SET);

    //initialize loop variables
    struct timespec start;
    clock_gettime(CLOCK_MONOTONIC_RAW, &start);
    uint64_t start_ns = start.tv_sec * (long long)1e9 + start.tv_nsec;
    uint64_t last_update_ns = start_ns;
    struct timespec now;
    uint64_t current_ns = last_update_ns;
    uint64_t delta_disk_us = 0; 
    uint64_t delta_pcm_us = 0;

    int frame = 0;
    long samples_supplied = 0;
    while(1)
    {
        clock_gettime(CLOCK_MONOTONIC_RAW, &now);
        current_ns = now.tv_sec * (long long)1e9 + now.tv_nsec;
        uint64_t time_running_us = (current_ns - start_ns) / (long)1e3;
        long samples_consumed = time_running_us * samplerate / 1e6;
        if(samples_supplied - samples_consumed < samples_in_buffer) 
        {
            int samples_read = fread(buff, channels * 2, samples_in_buffer, fin);
            if (samples_read == 0) 
            {
                printf("End of file.\n");
                break;
            }

            float sum = 0;
            for(int sample_index = 0; sample_index < samples_read; ++sample_index) {
                int16_t left_sample = buff[2*sample_index];
                int16_t right_sample = buff[2*sample_index + 1];
                float sample = ((float)(left_sample + right_sample)) * INT_TO_FLOAT / 2.0f;
                if(sample * sample > 1.0f) {
                    fprintf(stderr, "Wrong sample: %f\n", sample);
                }
                fvec_set_sample(in, sample, sample_index);
                sum += sample;
            }
            aubio_tempo_do(o, in, tempo_out);
            if(tempo_out->data[0] != 0) {
                float bpm = aubio_tempo_get_bpm(o);
                unsigned int lastbeat = aubio_tempo_get_last(o);
                printf("sum %f, Tempo 0: %f, bpm %f, ms %i\n", sum, tempo_out->data[0], bpm, lastbeat);
            }


            clock_gettime(CLOCK_MONOTONIC_RAW, &now);
            current_ns = now.tv_sec * (long long)1e9 + now.tv_nsec;
            delta_disk_us = (current_ns - last_update_ns) / (long)1e3;
            last_update_ns = current_ns;

            //if ((err = snd_pcm_writei(pcm_handle, buff, periods_per_frame * period_size)) == -EPIPE) 
            if ((err = snd_pcm_writei(pcm_handle, buff, samples_read)) == -EPIPE) 
            {
                printf("XRUN.\n");
                snd_pcm_prepare(pcm_handle);
            } 
            else if (err < 0) 
            {
                printf("ERROR. Can't write to PCM device. %s\n", snd_strerror(err));
            }
            samples_supplied += samples_in_buffer; 
            clock_gettime(CLOCK_MONOTONIC_RAW, &now);
            current_ns = now.tv_sec * (long long)1e9 + now.tv_nsec;
            delta_pcm_us = (current_ns - last_update_ns) / (long)1e3;
        }
        else
        {
            delta_disk_us = 0;
            delta_pcm_us = 0;
        }
        long sleep_time = 1000;
        if((delta_disk_us + delta_pcm_us) < frame_time)
        {
            sleep_time = (long)(frame_time - delta_disk_us - delta_pcm_us);
			usleep(sleep_time);
        }
		else
		{
            sleep_time = 0;
        }
        //printf("D %lli, P %lli, S %li\n", delta_disk_us, delta_pcm_us, sleep_time);
        clock_gettime(CLOCK_MONOTONIC_RAW, &now);
        current_ns = now.tv_sec * (long long)1e9 + now.tv_nsec;
        last_update_ns = current_ns;
        frame++;
	}
    struct timespec end;
    clock_gettime(CLOCK_MONOTONIC_RAW, &end);
    uint64_t end_update_ns = end.tv_sec * (long long)1e9 + end.tv_nsec;

	double sec = ((end_update_ns - start.tv_sec * (long long)1e9 + start.tv_nsec) / (long)1e3) / (double)1e6;
	printf("It took %f s, fps %f\n", sec, (double)frame / sec);

	snd_pcm_drain(pcm_handle);
	snd_pcm_close(pcm_handle);
	free(buff);
    del_aubio_tempo(o);
    del_fvec(in);
    del_fvec(tempo_out);

	return 0;

    /*

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
    */
 }

#endif // __linux__
