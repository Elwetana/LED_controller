#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "colours.h"

#ifndef min
#define min(x,y)  ((x) < (y)) ? (x) : (y)
#define max(x,y)  ((x) > (y)) ? (x) : (y)
#endif

void hsl_copy(const hsl_t* hsl_in, hsl_t* hsl_out)
{
    hsl_out->h = hsl_in->h;
    hsl_out->s = hsl_in->s;
    hsl_out->l = hsl_out->l;
}

void rgb2hsl(ws2811_led_t rgb, hsl_t* hsl)
{
    int r = ((rgb >> 16) & 0xFF);
    int g = ((rgb >> 8) & 0xFF);
    int b = (rgb & 0xFF);

    int vmin = min(r, min(g, b));  // Min. value of RGB
    int vmax = max(r, max(g, b));  // Max. value of RGB
    int diff = vmax - vmin;        // Delta RGB value

    int vsum = vmin + vmax;

    hsl->l = (float)vsum / 2.0f / 255.0f;

    if(diff < FLOAT_ERROR)  // This is a gray, no chroma...
    {
        hsl->h = 0;
        hsl->s = 0;
        return;
    }

    /**
     * Chromatic data...
     **/

    // Saturation
    if(hsl->l < 0.5f)
        hsl->s = (float)diff / (float)vsum;
    else
        hsl->s = (float)diff / (float)(2 * 255 - vsum);

    float dr = (((float)(vmax - r) / 6.0f) + ((float)diff / 2.0f)) / (float)diff;
    float dg = (((float)(vmax - g) / 6.0f) + ((float)diff / 2.0f)) / (float)diff;
    float db = (((float)(vmax - b) / 6.0f) + ((float)diff / 2.0f)) / (float)diff;

    float h = 0;
    if(vmax == r)
        h = db - dg;
    if(vmax == g)
        h = (1.0f / 3.0f) + dr - db;
    if(vmax == b)
        h = (2.0f / 3.0f) + dg - dr;

    if(h < 0) h += 1;
    if(h > 1) h -= 1;
    hsl->h = h;
}

float _hue2rgb(float v1, float v2, float vH)
{
    while(vH < 0) vH += 1.0f;
    while(vH > 1) vH -= 1.0f;

    if(6.0f * vH < 1.0f) return v1 + (v2 - v1) * 6.0f * vH;
    if(2.0f * vH < 1.0f) return v2;
    if(3.0f * vH < 2.0f) return v1 + (v2 - v1) * ((2.0f / 3.0f) - vH) * 6.0f;

    return v1;
}

ws2811_led_t hsl2rgb(hsl_t* hsl)
{
    if(hsl->s == 0)
        return float2int(hsl->l) << 16 | float2int(hsl->l) << 8 | float2int(hsl->l);

    float v2;
    if(hsl->l < 0.5f)
        v2 = hsl->l * (1.0f + hsl->s);
    else
        v2 = (hsl->l + hsl->s) - (hsl->s * hsl->l);

    float v1 = 2.0f * hsl->l - v2;

    float r = _hue2rgb(v1, v2, (hsl->h + 1.0f / 3.0f));
    float g = _hue2rgb(v1, v2, hsl->h);
    float b = _hue2rgb(v1, v2, hsl->h - (1.0f / 3.0f));

    return float2int(r) << 16 | float2int(g) << 8 | float2int(b);
}

void lerp_hsl(const hsl_t* hsl1, const hsl_t* hsl2, const float t, hsl_t* hsl_out)
{
    for (int i = 0; i < 3; ++i)
    {
        hsl_out->f[i] = hsl1->f[i] + t * (hsl2->f[i] - hsl1->f[i]);
    }

    if (((hsl2->h - hsl1->h)*(hsl2->h - hsl1->h)) > 0.25f) //std::abs(hsl1a.x - hsl2.x) > 0.5f
    {
        hsl_out->h += (hsl1->h > hsl2->h) ? -1 : 1;
    }
    if (hsl_out->h < 0)
    {
        hsl_out->h += 1;
    }
}

void fill_gradient(ws2811_led_t* gradient, int offset, ws2811_led_t from_color, ws2811_led_t to_color, int steps, int max_index)
{
    hsl_t hsl_from;
    hsl_t hsl_to;
    rgb2hsl(from_color, &hsl_from);
    rgb2hsl(to_color, &hsl_to);
    hsl_t step_delta;
    int delta_steps = (steps > 1) ? steps - 1 : steps;
    for (int i = 0; i < 3; ++i)
    {
        step_delta.f[i] = (hsl_to.f[i] - hsl_from.f[i]) / (float)delta_steps;
    }
    if((hsl_to.h - hsl_from.h) * (hsl_to.h - hsl_from.h) > 0.25f)
    {
        step_delta.h += 1.0f / (float)delta_steps;
    }
    for(int step = 0; step < steps; ++step)
    {
        if(offset + step > max_index) return;
        hsl_t hsl;
        for(int i = 0; i < 3; i++)
        {
            hsl.f[i] = hsl_from.f[i] + step_delta.f[i] * step;
        }
        gradient[offset + step] = hsl2rgb(&hsl);
    }
}

void test_rgb2hsl()
{
    const int n_tests = 5;
    ws2811_led_t inputs[5] = {0x00FF0000, 0x00808000, 0x00000080, 0x00A0A0A0, 0x0050C0F0};
    hsl_t outputs[5] = {
        {.h=0.0f,        .s=1.0f,  .l=0.5f },
        {.h=1.0f/6.0f,   .s=1.0f,  .l=0.25f},
        {.h=4.0f/6.0f,   .s=1.0f,  .l=0.25f},
        {.h=0.0f,        .s=0.0f,  .l=0.63f},
        {.h=198.f/360.f, .s=0.84f, .l=0.63f}
    };
    hsl_t hsl;
    for(int i = 0; i < n_tests; ++i)
    {
        rgb2hsl(inputs[i], &hsl);
        printf("Expected %f %f %f,\nReceived %f %f %f\n\n", outputs[i].h, outputs[i].s, outputs[i].l, hsl.h, hsl.s, hsl.l);
        ws2811_led_t rgb = hsl2rgb(&outputs[i]);
        printf("Expected %x,\nReceived %x\n\n", inputs[i], rgb);
        printf("---\n");
    }
}
