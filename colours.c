#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "colours.h"

#ifndef min
#define min(x,y)  ((x) < (y)) ? (x) : (y)
#define max(x,y)  ((x) > (y)) ? (x) : (y)
#endif

void rgb2hsl(ws2811_led_t rgb, float* hsl)
{
    int r = ((rgb >> 16) & 0xFF);
    int g = ((rgb >> 8) & 0xFF);
    int b = (rgb & 0xFF);

    int vmin = min(r, min(g, b));  // Min. value of RGB
    int vmax = max(r, max(g, b));  // Max. value of RGB
    int diff = vmax - vmin;        // Delta RGB value

    int vsum = vmin + vmax;

    hsl[2] = (float)vsum / 2.0f / 255.0f; //this is l

    if(diff < FLOAT_ERROR)  // This is a gray, no chroma...
    {
        hsl[0] = 0;
        hsl[1] = 0;
        return;
    }

    /**
     * Chromatic data...
     **/

    // Saturation
    if(hsl[2] < 0.5f)
        hsl[1] = (float)diff / (float)vsum;
    else
        hsl[1] = (float)diff / (float)(2 * 255 - vsum);

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
    hsl[0] = h;
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

ws2811_led_t hsl2rgb(float* hsl)
{
    float h = hsl[0];
    float s = hsl[1];
    float l = hsl[2];

    if(s == 0)
        return float2int(l) << 16 | float2int(l) << 8 | float2int(l);

    float v2;
    if(l < 0.5f)
        v2 = l * (1.0f + s);
    else
        v2 = (l + s) - (s * l);

    float v1 = 2.0f * l - v2;    

    float r = _hue2rgb(v1, v2, (h + 1.0f / 3.0f));
    float g = _hue2rgb(v1, v2, h);
    float b = _hue2rgb(v1, v2, h - (1.0f / 3.0f));

    return float2int(r) << 16 | float2int(g) << 8 | float2int(b);
}

void lerp_hsl(const float* hsl1, const float* hsl2, const float t, float* hsl_out)
{
    for (int i = 0; i < 3; ++i)
    {
        hsl_out[i] = hsl1[i] + t * (hsl2[i] - hsl1[i]);
    }
    if (((hsl2[0] - hsl1[0])*(hsl2[0] - hsl1[0])) > 0.25f) //std::abs(hsl1a.x - hsl2.x) > 0.5f
    {
        hsl_out[0] += (hsl1[0] > hsl2[0]) ? -1 : 1;
    }
    if (hsl_out[0] < 0)
    {
        hsl_out[0] += 1;
    }
}

void fill_gradient(ws2811_led_t* gradient, int offset, ws2811_led_t from_color, ws2811_led_t to_color, int steps, int max_index)
{
    float hsl_from[3];
    float hsl_to[3];
    rgb2hsl(from_color, hsl_from);
    rgb2hsl(to_color, hsl_to);
    float step_delta[3];
    int delta_steps = (steps > 1) ? steps - 1 : steps;
    for(int i = 0; i < 3; i++)
    {
        step_delta[i] = (hsl_to[i] - hsl_from[i]) / (float)delta_steps;
    }
    if((hsl_to[0] - hsl_from[0]) * (hsl_to[0] - hsl_from[0]) > 0.25)
    {
        step_delta[0] += 1.0f / (float)delta_steps;
    }
    for(int step = 0; step < steps; ++step)
    {
        if(offset + step > max_index) return;
        float hsl[3];
        for(int i = 0; i < 3; i++)
        {
            hsl[i] = hsl_from[i] + step_delta[i] * step;
        }
        gradient[offset + step] = hsl2rgb(hsl);
    }
}

void test_rgb2hsl()
{
    const int n_tests = 5;
    ws2811_led_t inputs[5] = {0x00FF0000, 0x00808000, 0x00000080, 0x00A0A0A0, 0x0050C0F0};
    float outputs[5][3] = {
        {0.0f, 1.0f, 0.5f},
        {1.0f/6.0f, 1.0f, 0.25f},
        {4.0f/6.0f, 1.0f, 0.25f},
        {0.0f, 0.0f, 0.63f},
        {198.f/360.f, 0.84f, 0.63f}
    };
    float hsl[3];
    for(int i = 0; i < n_tests; ++i)
    {
        rgb2hsl(inputs[i], hsl);
        printf("Expected %f %f %f,\nReceived %f %f %f\n\n", outputs[i][0], outputs[i][1], outputs[i][2], hsl[0], hsl[1], hsl[2]);
        ws2811_led_t rgb = hsl2rgb(outputs[i]);
        printf("Expected %x,\nReceived %x\n\n", inputs[i], rgb);
        printf("---\n");
    }
}
