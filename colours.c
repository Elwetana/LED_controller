#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#include "colours.h"

#ifndef min
#define min(x,y)  ((x) < (y)) ? (x) : (y)
#define max(x,y)  ((x) > (y)) ? (x) : (y)
#endif

ws2811_led_t alpha_blend_rgb(ws2811_led_t upper, ws2811_led_t lower, double upper_alpha)
{
    int r = (int)(((upper >> 16) & 0xFF) * upper_alpha + ((lower >> 16) & 0xFF) * (1. - upper_alpha));
    int g = (int)(((upper >> 8) & 0xFF) * upper_alpha + ((lower >> 8) & 0xFF) * (1. - upper_alpha));
    int b = (int)((upper & 0xFF) * upper_alpha + (lower & 0xFF) * (1. - upper_alpha));
    return r << 16 | g << 8 | b;
}

ws2811_led_t mix_rgb_color(ws2811_led_t rgb1, ws2811_led_t rgb2, double t)
{
    int r = (int)(((rgb1 >> 16) & 0xFF) * t + ((rgb2 >> 16) & 0xFF) * (1. - t));
    int g = (int)(((rgb1 >>  8) & 0xFF) * t + ((rgb2 >>  8) & 0xFF) * (1. - t));
    int b = (int)((rgb1 & 0xFF) * t + (rgb2 & 0xFF) * (1 - t));
    return r << 16 | g << 8 | b;
}

ws2811_led_t multiply_rgb_color(ws2811_led_t rgb, double t)
{
    int r = (int)(((rgb >> 16) & 0xFF) * t);
    int g = (int)(((rgb >> 8) & 0xFF) * t);
    int b = (int)((rgb & 0xFF) * t);
    return r << 16 | g << 8 | b;
}

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

    while(h < 0) h += 1;
    while(h > 1) h -= 1;
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

/*!
* \returns  hsl1 for t == 0 and hsl2 for t == 1
*/
void lerp_hsl(const hsl_t* hsl1, const hsl_t* hsl2, const float t, hsl_t* hsl_out)
{
    for (int i = 1; i < 3; ++i)
    {
        hsl_out->f[i] = hsl1->f[i] + t * (hsl2->f[i] - hsl1->f[i]);
    }
    if (((hsl2->h - hsl1->h) * (hsl2->h - hsl1->h)) > 0.25f) //std::abs(hsl1a.x - hsl2.x) > 0.5f
    {
        float h2 = hsl2->h + (hsl1->h > hsl2->h ? 1. : -1.);
        hsl_out->h = hsl1->h + t * (h2 - hsl1->h);
    }
    else
    {
        hsl_out->h = hsl1->h + t * (hsl2->h - hsl1->h);
    }
    if (hsl_out->h < 0)
    {
        hsl_out->h += 1;
    }
    if (hsl_out->h > 1)
    {
        hsl_out->h -= 1;
    }
}

/*!
* \returns  rgb1 for t == 0 and rgb2 for t == 1
*/
ws2811_led_t lerp_rgb(const ws2811_led_t rgb1, const ws2811_led_t rgb2, const float t)
{
    hsl_t hsl1, hsl2, hsl_out;
    rgb2hsl(rgb1, &hsl1);
    rgb2hsl(rgb2, &hsl2);
    lerp_hsl(&hsl1, &hsl2, t, &hsl_out);
    return hsl2rgb(&hsl_out);
}

void fill_gradient(ws2811_led_t* gradient, int offset, ws2811_led_t from_color, ws2811_led_t to_color, int steps, int next_steps, int max_index)
{
    assert(steps > 0);
    hsl_t hsl_from;
    hsl_t hsl_to;
    rgb2hsl(from_color, &hsl_from);
    rgb2hsl(to_color, &hsl_to);
    hsl_t step_delta;
    int delta_steps;
    if (steps == 1)
        delta_steps = 1;
    else if (next_steps != 0)
        delta_steps = steps;
    else
        delta_steps = steps - 1;
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
    ws2811_led_t c1 = 0xef0001;
    ws2811_led_t c2 = 0xee0d00; // with offset 0.500000 gave e8ef
    hsl_t h1, h2, h31, h32;
    rgb2hsl(c1, &h1);
    rgb2hsl(c2, &h2);
    lerp_hsl(&h1, &h2, 0.1, &h31);
    ws2811_led_t c31 = hsl2rgb(&h31);
    lerp_hsl(&h2, &h1, 0.9, &h32);
    ws2811_led_t c32 = hsl2rgb(&h32);
    assert(c31 == c32);

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
