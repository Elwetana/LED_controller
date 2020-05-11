#define _CRT_SECURE_NO_WARNINGS

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __linux__
#include "ws2811.h"
#else
#include "fakeled.h"
#endif // __linux__
#include "common_source.h"
#include "fire_source.h"
#include "perlin_source.h"
#include "color_source.h"
#include "chaser_source.h"
#include "morse_source.h"
#include "source_manager.h"
#include "listener.h"

enum SourceType string_to_SourceType(char* source)
{
    if (!strncasecmp("EMBERS", source, 6)) {
        return EMBERS_SOURCE;
    }
    else if (!strncasecmp("PERLIN", source, 6)) {
        return PERLIN_SOURCE;
    }
    else if (!strncasecmp("COLOR", source, 5)) {
        return COLOR_SOURCE;
    }
    else if (!strncasecmp("CHASER", source, 6)) {
        return CHASER_SOURCE;
    }
    else if (!strncasecmp("MORSE", source, 5)) {
        return MORSE_SOURCE;
    }
    else {
        printf("Unknown source");
        exit(-1);
    }
}

static SourceFunctions source_functions[N_SOURCE_TYPES];

struct LedParam {
    int led_count;
    int time_speed;
};
static struct LedParam led_param;
static void read_config();

void SourceManager_init(enum SourceType source_type, int led_count, int time_speed)
{
    read_config();
    Listener_init();

    led_param.led_count = led_count;
    led_param.time_speed = time_speed;
    /*SourceManager_init_source = FireSource_init;
    SourceManager_update_leds = FireSource_update_leds;
    SourceManager_destruct_source = FireSource_destruct;*/
    source_functions[EMBERS_SOURCE] = fire_functions;
    source_functions[PERLIN_SOURCE] = perlin_functions;
    source_functions[COLOR_SOURCE] = color_functions;
    source_functions[CHASER_SOURCE] = chaser_functions;
    source_functions[MORSE_SOURCE] = morse_functions;
    set_source(source_type);
}

void set_source(enum SourceType source_type)
{
    SourceManager_init_source = source_functions[source_type].init;
    SourceManager_update_leds = source_functions[source_type].update;
    SourceManager_destruct_source = source_functions[source_type].destruct;
    SourceManager_init_source(led_param.led_count, led_param.time_speed);
}

inline int ishex(int x)
{
    return (x >= '0' && x <= '9') ||
           (x >= 'a' && x <= 'f') ||
           (x >= 'A' && x <= 'F');
}

int decode(const char* s, char* dec)
{
    char* o;
    const char* end = s + strlen(s);
    int c;

    for (o = dec; s <= end; o++) 
    {
        c = *s++;
        if (c == '+') c = ' ';
        else if (c == '%' && (!ishex(*s++) || !ishex(*s++) || !sscanf(s - 2, "%2x", &c)))
            return -1;
        if (dec) *o = (char)c;
    }
    return o - dec;
}


void check_message()
{
    char* msg = Listener_poll_message();
    if (msg != NULL)
    {
        char command[32];
        char param[32];
        int n = sscanf(msg, "LED %s %s", command, param);
        if (n == 2)
        {
            if (!strncasecmp(command, "SOURCE", 6))
            {
                char source_name[32];
                int color = -1;
                char* sep = strchr(param, '?');
                if (sep != NULL)
                {
                    int name_length = sep - param;
                    strncpy(source_name, param, name_length);
                    source_name[name_length] = 0x0;
                    color = strtol(sep + 1, NULL, 16);
                }
                else if (!strncasecmp("OFF", param, 3))
                {
                    strcpy(source_name, "COLOR");
                    color = 0x0;
                }
                else
                {
                    strncpy(source_name, param, 32);
                }
                if (color != -1) // this is only possible for color source now
                {
                    SourceColors_destruct(source_config.colors[COLOR_SOURCE]);
                    SourceColors* sc = malloc(sizeof(SourceColors));
                    sc->colors = malloc(sizeof(ws2811_led_t) * 2);
                    sc->steps = malloc(sizeof(int) * 1);
                    sc->n_steps = 1;
                    sc->colors[0] = color;
                    sc->colors[1] = color;
                    sc->steps[0] = 1;
                    SourceConfig_add_color(source_name, sc);
                }
                SourceManager_destruct_source();
                set_source(string_to_SourceType(source_name));
                printf("Changing source to %s\n", param);
            }
            else if (!strncasecmp(command, "MSG", 3))
            {
                char* sep = strchr(param, '?');
                if (sep != NULL)
                {
                    char target[32];
                    char message[64];
                    strncpy(target, param, sep - param);
                    target[sep - param] = 0x0;
                    if ((strlen(sep + 1) < 64) && (decode(sep + 1, message) > 0))
                    {
                        if (!strncasecmp(target, "MORSETEXT", 9))
                        {
                            MorseSource_assign_text(message);
                            printf("Setting new MorseSource text: %s\n", message);
                        }
                        else if (!strncasecmp(target, "MORSEMODE", 9))
                        {
                            int mode = atoi(message);
                            MorseSource_change_mode(mode);
                            printf("Setting new MorseSource mode: %i\n", mode);
                        }
                        else
                            printf("Unknown target: %s, msg was: %s\n", target, message);
                    }
                    else
                        printf("Message too long or poorly formatted: %s\n", param);
                }
                else
                    printf("Message does not contain target %s\n", param);
            }
            else
                printf("Unknown command received, command: %s, param %s\n", command, param);
        }
        else {
            printf("Unknown message received %s\n", msg);
        }
        free(msg);
    }
}

SourceConfig source_config;

static void SourceConfig_init()
{
    source_config.colors = malloc(sizeof(SourceColors*) * N_SOURCE_TYPES);
}

void SourceConfig_add_color(char* source_name, SourceColors* source_colors)
{
    enum SourceType source_type = string_to_SourceType(source_name);
    source_config.colors[source_type] = source_colors;
}

void SourceColors_destruct(SourceColors* source_colors)
{
    if (source_colors)
    {
        free(source_colors->colors);
        free(source_colors->steps);
    }
    free(source_colors);
}

void SourceConfig_destruct()
{
    for (int i = 0; i < N_SOURCE_TYPES; ++i)
    {
        SourceColors_destruct(source_config.colors[i]);
    }
    free(source_config.colors);
}

static void read_config()
{
    SourceConfig_init();
    FILE* config = fopen("config", "r");
    if (config == NULL) {
        printf("Config not found\n");
        exit(-4);
    }
    char buf[255];
    while (fgets(buf, 255, config) != NULL)
    {
        SourceColors* sc = malloc(sizeof(SourceColors));
        char name[16];
        int n_steps;
        int n = sscanf(buf, "%s %i", name, &n_steps);
        if (n != 2) {
            printf("Error reading config -- source name\n");
            exit(-5);
        }
        printf("Reading config for: %s\n", name);

        sc->colors = malloc(sizeof(ws2811_led_t) * (n_steps + 1));
        sc->steps = malloc(sizeof(int) * n_steps);
        sc->n_steps = n_steps;
        fgets(buf, 255, config);
        int color, step, offset;
        char* line = buf;
        offset = 0;
        for (int i = 0; i < n_steps; i++)
        {
            n = sscanf(line, "%x %i%n", &color, &step, &offset);
            if (n != 2) {
                printf("Error reading config -- colors\n");
                exit(-6);
            }
            sc->colors[i] = color;
            sc->steps[i] = step;
            line += offset;
        }
        n = sscanf(line, "%x", &color);
        if (n != 1) {
            printf("Error reading config -- colors end\n");
            exit(-6);
        }
        sc->colors[n_steps] = color;
        SourceConfig_add_color(name, sc);
    }
}
