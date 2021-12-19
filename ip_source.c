#define _CRT_SECURE_NO_WARNINGS

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#ifdef __linux__
#include "ws2811.h"
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <net/if.h>
#include <arpa/inet.h>
#else
#include "fakeled.h"
#include <Inaddr.h>
#endif // __linux__

#include "common_source.h"
#include "ip_source.h"

static uint32_t get_ip_address()
{
#ifdef __linux__
    int fd;
    struct ifreq ifr;
    fd = socket(AF_INET, SOCK_DGRAM, 0);
    /* I want to get an IPv4 IP address */
    ifr.ifr_addr.sa_family = AF_INET;
    /* I want IP address attached to "wlan0" */
    strncpy(ifr.ifr_name, "wlan0", IFNAMSIZ - 1);
    ioctl(fd, SIOCGIFADDR, &ifr);
    close(fd);
    /* display result */
    return ((struct sockaddr_in*)&ifr.ifr_addr)->sin_addr.s_addr;
#endif // __linux__
    uint32_t ip = 0;
    ip |= 129;
    ip |= 129 << 8;
    ip |= 129 << 16;
    ip |= 255 << 24;
    return ip;
}


//returns 1 if leds were updated, 0 if update is not necessary
int IPSource_update_leds(int frame, ws2811_t* ledstrip)
{
    (void)frame;
    if (ip_source.first_update > 0)
    {
        return 0;
    }

    uint32_t ip = get_ip_address();
    unsigned char addr[4];
    addr[0] = (ip & 0xFF);
    addr[1] = (ip & 0xFF00) >> 8;
    addr[2] = (ip & 0xFF0000) >> 16;
    addr[3] = (ip & 0xFF000000) >> 24;

    const int one_len = 50;

    for (int led = 0; led < ip_source.basic_source.n_leds; ++led)
    {
        ledstrip->channel[0].leds[led] = 0x0;
    }

    for (int l = 0; l < (ip_source.basic_source.n_leds / one_len); ++l)
    {
        for (int led = 0; led < 3; led++)
        {
            ledstrip->channel[0].leds[l * one_len + led] = 0x880000;
        }
        for (int addr_byte = 0; addr_byte < 4; ++addr_byte)
        {
            unsigned char c = addr[addr_byte];
            for (int bit_index = 0; bit_index < 8; ++bit_index)
            {
                ledstrip->channel[0].leds[l * one_len + 3 + addr_byte * 9 + bit_index] = (addr[addr_byte] & (1 << (7 - bit_index))) ? 0x888888 : 0x0;
            }
            ledstrip->channel[0].leds[l * one_len + 3 + addr_byte * 9 + 8] = 0x008800;
        }
        for (int led = 0; led < 3; led++)
        {
            ledstrip->channel[0].leds[l * one_len + 38 + led] = 0x88;
        }
    }

    ip_source.first_update = 1;
    return 1;
}


void IPSource_init(int n_leds, int time_speed, uint64_t current_time)
{
    BasicSource_init(&ip_source.basic_source, n_leds, time_speed, source_config.colors[IP_SOURCE], current_time);
    ip_source.first_update = 0;
}

void IPSource_construct()
{
    BasicSource_construct(&ip_source.basic_source);
    ip_source.basic_source.update = IPSource_update_leds;
    ip_source.basic_source.init = IPSource_init;
}

IPSource ip_source = {
    .basic_source.construct = IPSource_construct,
    .first_update = 0 
};
