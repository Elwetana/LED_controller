#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef __linux__
#  include <time.h>
#  include <unistd.h>
#else
#  include "faketime.h"
#  include "fakesignal.h"
#  include "getopt.h"
#endif
#include <math.h>
#include <signal.h>

#include "ws2811.h"
#include "source.h"


// defaults for cmdline options
#define TARGET_FREQ             WS2811_TARGET_FREQ
#define GPIO_PIN                18
#define DMA                     10
#define STRIP_TYPE              WS2811_STRIP_RGB		// WS2812/SK6812RGB integrated chip+leds
//#define STRIP_TYPE            WS2811_STRIP_GBR		// WS2812/SK6812RGB integrated chip+leds
//#define STRIP_TYPE            SK6812_STRIP_RGBW		// SK6812RGBW (NOT SK6812RGB)

#define LED_COUNT               454

#define FPS_SAMPLES             50
#define FRAME_TIME              20000

int led_count = LED_COUNT;

static uint8_t running = 1;
int clear_on_exit = 0;

ws2811_t ledstring =
{
    .freq = TARGET_FREQ,
    .dmanum = DMA,
    .channel =
    {
        [0] =
        {
            .gpionum = GPIO_PIN,
            .count = LED_COUNT,
            .invert = 0,
            .brightness = 255,
            .strip_type = STRIP_TYPE,
        }
    },
};

FireSource fire_source =
{
    .ember_data = {
        [0] = 
        {
            .amp = 0.4f,
            .amp_rand = 0.1f,
            .x_space = 100,
            .sigma = 30,
            .sigma_rand = 2,
            .osc_amp = 0.2f,
            .osc_freq = 0.005f,
            .osc_freq_rand = 0.01,
            .decay = 0.0,
            .decay_rand = 0
        },
        [1] =
        {
            .amp = 0.2f,
            .amp_rand = 0.05f,
            .x_space = 60.0f,
            .sigma = 9.0f,
            .sigma_rand = 2.0f,
            .osc_amp = 0.2f,
            .osc_freq = 0.01f,
            .osc_freq_rand = 0.005f,
            .decay = 0.0f,
            .decay_rand = 0.0f
        },
        [2] = 
        {
            .amp = 0.1f,
            .amp_rand = 0.2f,
            .x_space = 25.0f,
            .sigma = 3.0f,
            .sigma_rand = 1.0f,
            .osc_amp = 0.2f,
            .osc_freq = 0.01f,
            .osc_freq_rand = 0.01f,
            .decay = 0.001f,
            .decay_rand = 0.001f
        }
    }
};

static void ctrl_c_handler(int signum)
{
	(void)(signum);
    running = 0;
}

static void setup_handlers(void)
{
    struct sigaction sa =
    {
        .sa_handler = ctrl_c_handler,
    };

    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
}

void parseargs(int argc, char **argv, ws2811_t *ws2811)
{
	int index;
	int c;

	static struct option longopts[] =
	{
		{"help", no_argument, 0, 'h'},
		{"clear", no_argument, 0, 'c'},
		{"version", no_argument, 0, 'v'},
		{0, 0, 0, 0}
	};

	while (1)
	{
		index = 0;
		c = getopt_long(argc, argv, "hcv", longopts, &index);

		if (c == -1)
			break;

		switch (c)
		{
		case 0:
			/* handle flag options (array's 3rd field non-0) */
			break;

		case 'h':
			//fprintf(stderr, "%s version %s\n", argv[0], VERSION);
			fprintf(stderr, "Usage: %s \n"
				"-h (--help)    - this information\n"
				"-c (--clear)   - clear matrix on exit.\n"
				"-v (--version) - version information\n"
				, argv[0]);
			exit(-1);
		case 'c':
			clear_on_exit=1;
			break;

        //TODO more cases
        }
    }
}

int main(int argc, char *argv[])
{
    printf("Starting\n");
    ws2811_return_t ret;
    parseargs(argc, argv, &ledstring);
    init_FireSource(&fire_source, led_count, 1);

    setup_handlers();

    if ((ret = ws2811_init(&ledstring)) != WS2811_SUCCESS)
    {
        fprintf(stderr, "ws2811_init failed: %s\n", ws2811_get_return_t_str(ret));
        return ret;
    }
    printf("Init successful\n");
    srand(time(NULL));

    long last_update_ns = 0;
    long frame = 0;
    long fps_time_ns = 0;

    while (running)
    {
        struct timespec start_frame;
        clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &start_frame);
        long current_ns = start_frame.tv_sec * (long)1e9 + start_frame.tv_nsec;
        long delta_us = (current_ns - last_update_ns) / (long)1e3;
        last_update_ns = current_ns;
        frame++;
        if(frame % FPS_SAMPLES == 0)
        {
            float fps = (float)FPS_SAMPLES / (float)(current_ns - fps_time_ns) * 1e9;
            fps_time_ns = current_ns;
            printf("FPS: %f\n", fps);
        }
        int sleep_time = 1000;
        if(delta_us < FRAME_TIME)
        {
            sleep_time = FRAME_TIME - delta_us;
        }
        usleep(sleep_time);
        if ((ret = ws2811_render(&ledstring)) != WS2811_SUCCESS)
        {
            fprintf(stderr, "ws2811_render failed: %s\n", ws2811_get_return_t_str(ret));
            break;
        }
    }

    if (clear_on_exit) 
    {
        for(int i = 0; i < led_count; i++)
        {
            ledstring.channel[0].leds[i] = 0x0;
        }
    	ws2811_render(&ledstring);
    }

    ws2811_fini(&ledstring);

    printf ("\n");
    return ret;
}