#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef __linux__
#  include <time.h>
#  include <unistd.h>
#  include <getopt.h>
#else
#  include "faketime.h"
#  include "fakesignal.h"
#  include "getopt.h"
#endif
#include <math.h>
#include <signal.h>

#include "ws2811.h"
#include "fire_source.h"
#include "colours.h"

// defaults for cmdline options
#define TARGET_FREQ             WS2811_TARGET_FREQ
#define GPIO_PIN                18
#define DMA                     10
#define STRIP_TYPE              WS2811_STRIP_GRB

#define LED_COUNT               454

#define FPS_SAMPLES             50
#define FRAME_TIME              40000

int led_count = LED_COUNT;

static uint8_t running = 1;

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

struct ArgOptions
{
    int clear_on_exit;
    int time_speed;
};

static struct ArgOptions arg_options = 
{ 
    .clear_on_exit = 0,
    .time_speed = 1
};

void parseargs(int argc, char **argv)
{
	int index;
	int c;

	static struct option longopts[] =
	{
		{"help", no_argument, 0, 'h'},
		{"clear", no_argument, 0, 'c'},
		{"version", no_argument, 0, 'v'},
        {"speed", required_argument, 0, 's'},
		{0, 0, 0, 0}
	};

    static const char shortopts[] = "hcvs:";

	while (1)
	{
		index = 0;
		c = getopt_long(argc, argv, shortopts, longopts, &index);

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
                "-s (--speed)   - simulation speed\n"
				, argv[0]);
			exit(-1);
		case 'c':
			arg_options.clear_on_exit = 1;
			break;
        case 's':
			if (optarg)
            {
				arg_options.time_speed = atoi(optarg);
            }
            break;
        //TODO more cases
        }
    }
}

int main(int argc, char *argv[])
{
    void (*init_source)(int, int);
    void (*update_leds)(int, ws2811_t*);
    void (*destruct_source)();

    printf("Starting\n");
    ws2811_return_t ret;
    parseargs(argc, argv);

    //this could be set up by command line parameters or by communication with some controller
    init_source = init_FireSource;
    update_leds = update_leds_FireSource;
    destruct_source = destruct_FireSource;

    init_source(led_count, 1);
    printf("Init source\n");

    setup_handlers();

    if ((ret = ws2811_init(&ledstring)) != WS2811_SUCCESS)
    {
        fprintf(stderr, "ws2811_init failed: %s\n", ws2811_get_return_t_str(ret));
        return ret;
    }
    printf("Init successful\n");
    srand(0); //for testing we want random to be stable

    uint64_t last_update_ns = 0;
    long frame = 0;
    uint64_t fps_time_ns = 0;

    while (running)
    {
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC_RAW, &now);
        uint64_t current_ns = now.tv_sec * (long long)1e9 + now.tv_nsec;
        long delta_us = (current_ns - last_update_ns) / (long)1e3;
        frame++;
        if(frame % FPS_SAMPLES == 0)
        {
            double fps = (double)FPS_SAMPLES / (double)(current_ns - fps_time_ns) * 1e9;
            printf("FPS: %f\n", fps);
            fps_time_ns = current_ns;
        }
        long sleep_time = 1000;
        if(delta_us < FRAME_TIME)
        {
            sleep_time = FRAME_TIME - delta_us;
        }
        //printf("D %li, S %li\n", delta_us, sleep_time);
        usleep(sleep_time);
        update_leds(frame, &ledstring);
        if ((ret = ws2811_render(&ledstring)) != WS2811_SUCCESS)
        {
            fprintf(stderr, "ws2811_render failed: %s\n", ws2811_get_return_t_str(ret));
            break;
        }
    }

    if (arg_options.clear_on_exit) 
    {
        for(int i = 0; i < led_count; i++)
        {
            ledstring.channel[0].leds[i] = 0x00; //GBR: FF0000 - blue, FF00 - green, FF - red;  RGB: FF0000 - green, FF00 - red, FF - blue
        }
    	ws2811_render(&ledstring);
    }

    destruct_source();
    ws2811_fini(&ledstring);

    printf ("\n");
    return ret;
}
