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
#include "disco_source.h"
#include "ip_source.h"
#include "xmas_source.h"
#include "game_source.h"
#include "rad_game_source.h"
#include "source_manager.h"
#include "listener.h"
#include "ini.h"

enum SourceType string_to_SourceType(const char* source)
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
    else if (!strncasecmp("DISCO", source, 5)) {
        return DISCO_SOURCE;
    }
    else if (!strncasecmp("IP", source, 2)) {
        return IP_SOURCE;
    }
    else if (!strncasecmp("XMAS", source, 4)) {
        return XMAS_SOURCE;
    }
    else if (!strncasecmp("GAME", source, 4)) {
        return GAME_SOURCE;
    }
    else if (!strncasecmp("RAD_GAME", source, 8)) {
        return RAD_GAME_SOURCE;
    }
    else {
        printf("Unknown source");
        exit(-1);
    }
}

static BasicSource* sources[N_SOURCE_TYPES];
static uint64_t* current_time;
static uint64_t* time_delta;
static enum SourceType active_source = N_SOURCE_TYPES;

struct LedParam {
    int led_count;
    int time_speed;
};
static struct LedParam led_param;
static void read_config();

void SourceManager_construct_sources()
{
    for(enum SourceType source_type = EMBERS_SOURCE; source_type < N_SOURCE_TYPES; ++source_type)
    {
        sources[source_type]->construct();
    }
}

static void set_source(enum SourceType source_type, uint64_t cur_time)
{
    SourceManager_update_leds = sources[source_type]->update;
    SourceManager_destruct_source = sources[source_type]->destruct;
    SourceManager_process_message = sources[source_type]->process_message;
    current_time = &sources[source_type]->current_time;
    time_delta = &sources[source_type]->time_delta;
    active_source = source_type;
    sources[source_type]->init(led_param.led_count, led_param.time_speed, cur_time);
}

void SourceManager_init(enum SourceType source_type, int led_count, int time_speed, uint64_t cur_time)
{
    led_param.led_count = led_count;
    led_param.time_speed = time_speed;

    sources[EMBERS_SOURCE] = &fire_source.basic_source;
    sources[PERLIN_SOURCE] = &perlin_source.basic_source;
    sources[COLOR_SOURCE]  = &color_source.basic_source;
    sources[CHASER_SOURCE] = &chaser_source.basic_source;
    sources[MORSE_SOURCE]  = &morse_source.basic_source;
    sources[DISCO_SOURCE]  = &disco_source.basic_source;
    sources[IP_SOURCE]     = &ip_source.basic_source;
    sources[XMAS_SOURCE]   = &xmas_source.basic_source;
    sources[GAME_SOURCE]   = &game_source.basic_source;
    sources[RAD_GAME_SOURCE] = &rad_game_source.basic_source;
    SourceManager_construct_sources();

    Listener_init();
    read_config();
    set_source(source_type, cur_time);
}

void SourceManager_set_time(uint64_t time_ns, uint64_t time_delta_ns)
{
    *current_time = time_ns;
    *time_delta = time_delta_ns;
}

void SourceManager_switch_to_source(enum SourceType source)
{
    SourceManager_destruct_source();
    set_source(source, current_time);
}

inline int ishex(int x)
{
    return (x >= '0' && x <= '9') ||
           (x >= 'a' && x <= 'f') ||
           (x >= 'A' && x <= 'F');
}

// Decode URL-encoded strings
// https://rosettacode.org/wiki/URL_decoding#C
static int64_t decode(const char* s, char* dec)
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


static void process_source_message(const char* param)
{
    char source_name[64];
    int color = -1;
    if (!strncasecmp("OFF", param, 3))
    {
        strcpy(source_name, "COLOR");
        color = 0; 
    }
    else
    {
        strncpy(source_name, param, 63);
    }
    SourceManager_destruct_source();
    uint64_t cur_time = *current_time;
    set_source(string_to_SourceType(source_name), cur_time);
    if (color == 0)
    {
        SourceManager_process_message("color?000000");
    }
    printf("Changing source to %s\n", param);
}

void SourceManager_reload_color_config();

/*!
 * @brief Parses and acts on message from HTTP server. There are three types of messages:
 *  LED SOURCE <source> -- will be processed by `process_source_message` function and new source will be set
 *  LED MSG <url_encoded_message> -- will be processed by active source's `process_message` function
 *  LED RELOAD -- will call `SourceManager_reload_color_config` and, hopefully, reload color config
*/
void check_message()
{
    char* msg = Listener_poll_message();
    //Message examples:
    //  LED SOURCE EMBERS
    //  LED SOURCE COLOR?BFFBFF
    //  LED MSG MORSETEXT?HI%20URSULA
    if (msg == NULL)
    {
        return;
    }
    if (strlen(msg) > 64) 
    {
        printf("Message too long: %s, %zi", msg, strlen(msg));
        goto quit;
    }
    char command[64];
    char param[64];
    int n = sscanf(msg, "LED %s %s", command, param);
    if (n != 2)
    {
        printf("Unknown message received %s\n", msg);
        goto quit;
    }
    //make sure strings are null-terminated
    command[63] = 0x0;
    param[63] = 0x0;
    if (!strncasecmp(command, "SOURCE", 6))
    {
        process_source_message(param);
    }
    else if (!strncasecmp(command, "MSG", 3))
    {
        char message[64];
        if (decode(param, message) < 0)
        {
            printf("Malformatted URL-encoded text: %s\n", param);
            goto quit;
        }
        //printf("Sending message to source: %s\n", message);
        SourceManager_process_message(message);
    }
    else if (!strncasecmp(command, "RELOAD", 6))
    {
        SourceManager_reload_color_config();
    }
    else
    {
        printf("Unknown command received, command: %s, param %s\n", command, param);
        goto quit;
    }
quit:    
    free(msg);
}

SourceConfig source_config;

static void SourceConfig_init()
{
    source_config.colors = malloc(sizeof(SourceColors*) * N_SOURCE_TYPES);
    for (int i = 0; i < N_SOURCE_TYPES; ++i)
        source_config.colors[i] = NULL;
}

void SourceColors_destruct(SourceColors* source_colors)
{
    if (source_colors)
    {
        free(source_colors->colors);
        free(source_colors->steps);
        free(source_colors);
    }
}

void SourceConfig_add_color(char* source_name, SourceColors* source_colors)
{
    enum SourceType source_type = string_to_SourceType(source_name);
    SourceColors_destruct(source_config.colors[source_type]);
    source_config.colors[source_type] = source_colors;
}

void SourceConfig_destruct()
{
    for (int i = 0; i < N_SOURCE_TYPES; ++i)
    {
        if(source_config.colors[i])
            SourceColors_destruct(source_config.colors[i]); 
    }
    free(source_config.colors);
}

static char* skip_comments(char* buf, FILE* config)
{
    char* c = fgets(buf, 1024, config);
    while (c != NULL && strnlen(buf, 2) > 0 && (buf[0] == ';' || buf[0] == '#'))
    {
        c = fgets(buf, 1024, config);
    }
    return c;
}


static void read_color_config()
{
    FILE* config = fopen("config", "r");
    if (config == NULL) {
        printf("Config not found\n");
        exit(-4);
    }
    char buf[1024];
    while (skip_comments(buf, config) != NULL)
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
        skip_comments(buf, config);
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


void SourceManager_reload_color_config()
{
    read_color_config();
    BasicSource_build_gradient(sources[active_source], source_config.colors[active_source]->colors, source_config.colors[active_source]->steps, source_config.colors[active_source]->n_steps);
    printf("Colour config reloaded\n");
}

static int ini_file_handler(void* user, const char* section, const char* name, const char* value)
{
    (void)user;
    enum SourceType source_type = string_to_SourceType(section);
    return sources[source_type]->process_config(name, value);
}

static void read_config()
{
    SourceConfig_init();
    read_color_config();
    ini_parse("config.ini", ini_file_handler, NULL);
}
