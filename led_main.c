#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef __linux__
#  include <time.h>
#  include <unistd.h>
#  include <getopt.h>
#  include "ws2811.h"
#else
#  include "faketime.h"
#  include "getopt.h"
#  include "fakeled.h"
#endif
#include <math.h>
#include <signal.h>
#include <czmq.h>

#include "source_manager.h"
#include "led_main.h"

#define PRINT_FPS

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

#ifdef __linux__
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
#else
BOOL WINAPI consoleHandler(DWORD signal) 
{
    if (signal == CTRL_C_EVENT)
        running = 0;
    return TRUE;
}
static void setup_handlers(void)
{
    if (!SetConsoleCtrlHandler(consoleHandler, TRUE)) 
    {
        printf("\nERROR: Could not set control handler"); 
    }
}
#endif

struct ArgOptions arg_options = 
{ 
    .clear_on_exit = 0,
    .frame_time = FRAME_TIME,
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
        {"time_speed", required_argument, 0, 't'},
        {"source", required_argument, 0, 's'},
        {"frame_time", required_argument, 0, 'f'},
        {"nleds", required_argument, 0, 'n'},
        {"gpio", required_argument, 0, 'g'},
        {"strip", required_argument, 0, 'p'},
		{0, 0, 0, 0}
	};

    static const char shortopts[] = "hcvt:s:f:n:g:p:";

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
				"-h (--help)       - this information\n"
				"-c (--clear)      - clear matrix on exit.\n"
				"-v (--version)    - version information\n"
                "-t (--time_speed) - simulation speed\n"
                "-s (--source)     - source. Can be EMBERS, PERLIN, COLOR or CHASER\n"
                "-f (--frame_time) - length of one frame in us. FPS = 1 000 000 / frame_time\n"
                "-n (--nleds)      - number of leds on string (100 on disco LEDs, 454 in gazebo)\n"
                "-g (--gpio)       - GPIO to use (12 on disco light Raspberry, 18 on the Raspberry in gazebo)\n"
                "-p (--strip)      - strip type - rgb (disco LEDs) or grb (Gazebo)\n"
				, argv[0]);
			exit(-1);
		case 'c':
			arg_options.clear_on_exit = 1;
			break;
        case 't':
			if (optarg)
            {
				arg_options.time_speed = atoi(optarg);
            }
            break;
        case 's':
            if (optarg)
            {
                arg_options.source_type = string_to_SourceType(optarg);
            }
            break;
        case 'f':
            if(optarg)
            {
                arg_options.frame_time = atoi(optarg);
                printf("Frame time set to: %lli \n", arg_options.frame_time);
            }
            break;
        case 'n':
            if (optarg)
            {
                int n = atoi(optarg);
                ledstring.channel[0].count = n;
            }
            break;
        case 'g':
            if (optarg) {
                /*  PWM0, which can be set to use GPIOs 12, 18, 40, and 52. Only 12 (pin 32) and 18 (pin 12) are available on the B+/2B/3B
                    PWM1 which can be set to use GPIOs 13, 19, 41, 45 and 53. Only 13 is available on the B+/2B/PiZero/3B, on pin 33
                    PCM_DOUT, which can be set to use GPIOs 21 and 31. Only 21 is available on the B+/2B/PiZero/3B, on pin 40.
                    SPI0-MOSI is available on GPIOs 10 and 38. Only GPIO 10 is available on all models.                                      */
                int gpio = atoi(optarg);
                ledstring.channel[0].gpionum = gpio;
            }
            break;
        case 'p':
            if (optarg) {
                if (!strncasecmp("rgb", optarg, 4)) {
                    ledstring.channel[0].strip_type = WS2811_STRIP_RGB;
                }
                else if (!strncasecmp("grb", optarg, 4)) {
                    ledstring.channel[0].strip_type = WS2811_STRIP_GRB;
                }
                else {
                    printf("invalid strip %s. You will have to modify source code and recompile.\n", optarg);
                    exit(-1);
                }
            }
            break;
        }
    }
}


int main(int argc, char *argv[])
{
    printf("Starting\n");
    ws2811_return_t ret;
    parseargs(argc, argv);
    int led_count = ledstring.channel[0].count;

    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC_RAW, &now);
    uint64_t last_update_ns = now.tv_sec * (long long)1e9 + now.tv_nsec;
    long frame = 0;
    SourceManager_init(arg_options.source_type, led_count, arg_options.time_speed, last_update_ns);
    printf("Init source with %i leds\n", led_count);

    setup_handlers();

    if ((ret = ws2811_init(&ledstring)) != WS2811_SUCCESS)
    {
        fprintf(stderr, "ws2811_init failed: %s\n", ws2811_get_return_t_str(ret));
        return ret;
    }
    printf("Init successful\n");
    srand(0); //for testing we want random to be stable

#ifdef PRINT_FPS
    uint64_t fps_time_ns = 0;
#endif
    while (running)
    {
        clock_gettime(CLOCK_MONOTONIC_RAW, &now);
        uint64_t current_ns = now.tv_sec * (long long)1e9 + now.tv_nsec;
        uint64_t delta_us = (current_ns - last_update_ns) / (long)1e3;
        frame++;
#ifdef PRINT_FPS	
        if(frame % FPS_SAMPLES == 0)
        {
            double fps = (double)FPS_SAMPLES / (double)(current_ns - fps_time_ns) * 1e9;
            printf("FPS: %f\n", fps);
            fps_time_ns = current_ns;
        }
#endif	
        long sleep_time = 1000;
        if(delta_us < arg_options.frame_time)
        {
            sleep_time = (long)(arg_options.frame_time - delta_us);
        }
        //printf("D %lli, S %li\n", delta_us, sleep_time);
        usleep(sleep_time);

        // Now we have to save the current time so that we know how much time we spent working
        clock_gettime(CLOCK_MONOTONIC_RAW, &now);
        current_ns = now.tv_sec * (long long)1e9 + now.tv_nsec;
        SourceManager_set_time(current_ns, current_ns - last_update_ns);
        last_update_ns = current_ns;
        if (SourceManager_update_leds(frame, &ledstring))
        {
            if ((ret = ws2811_render(&ledstring)) != WS2811_SUCCESS)
            {
                fprintf(stderr, "ws2811_render failed: %s\n", ws2811_get_return_t_str(ret));
                break;
            }
        }
        //poll server for remote command
        check_message();
    }

    if (arg_options.clear_on_exit) 
    {
        for(int i = 0; i < led_count; i++)
        {
            ledstring.channel[0].leds[i] = 0x00; //GBR: FF0000 - blue, FF00 - green, FF - red;  RGB: FF0000 - green, FF00 - red, FF - blue
        }
    	ws2811_render(&ledstring);
    }

    SourceConfig_destruct();
    SourceManager_destruct_source();
    ws2811_fini(&ledstring);

    printf ("Finished\n");
    return ret;
}
