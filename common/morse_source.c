#define _CRT_SECURE_NO_WARNINGS

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <ctype.h>
#ifdef __linux__
#include "ws2811.h"
#include <ctype.h>
#else
#include "fakeled.h"
#endif // __linux__

#include "common_source.h"
#include "morse_source.h"


void MorseSource_get_code(char* buf, const char c)
{
    char* code = morse_source.cmorse[(int)c - 65];
    strcpy(buf, code);
}

static void MorseSource_read_font()
{
    FILE* ffont = fopen("font_5x7.bmp", "r");
    fseek(ffont, 0x0A, 0);
    char buf[4];
    fread(buf, 4, 1, ffont);
    int offset = buf[0] | buf[1] << 8 | buf[2] << 16 | buf[3] << 24;
    fseek(ffont, offset, 0);
    for (int y = 0; y < MORSE_FONT_CHAR_H; ++y)
    {
        unsigned char row[(MORSE_FONT_CHAR_W + 1) * 26];
        fread(row, (MORSE_FONT_CHAR_W + 1) * 26, 1, ffont);
        int column = 0;
        for(int x = 0; x < (MORSE_FONT_CHAR_W + 1) * 26; ++x)
        {
            if (x % (MORSE_FONT_CHAR_W + 1) == MORSE_FONT_CHAR_W) continue;
            if ((unsigned char)(row[x]) == 0) {
                morse_source.font[6 - y][column] = 1;
            }
            column++;
        }
    }
}

void MorseSource_make_morse_char(struct MorseChar* mc, char* m, int add_padding)
{
    mc->len = 0;
    int ml = strlen(m);
    for (int i = 0; i < ml; ++i)
    {
        if (m[i] == '-') {
            for (int j = 0; j < 3; ++j) {
                mc->data[mc->len++] = 2;
            }
        }
        else {
            mc->data[mc->len++] = 1;
        }
        mc->data[mc->len++] = 0;
    }
    mc->len -= 1;
    if (add_padding != 0)
    {
        for (int j = 0; j < 3; ++j) 
        {
            mc->data[mc->len++] = 0;
        }

    }
}

static void MorseSource_convert_morse()
{
    for (int mi = 0; mi < 26; mi++)
    {
        char* m = morse_source.cmorse[mi];
        struct MorseChar* mc = &morse_source.morse[mi];
        MorseSource_make_morse_char(mc, m, 0);
    }
}

void MorseSource_assign_text(const char* new_text)
{
    if (morse_source.text != NULL) free(morse_source.text);
    morse_source.text_length = strlen(new_text);
    morse_source.text = malloc(sizeof(char) * (morse_source.text_length + 1));
    for (int i = 0; i < morse_source.text_length; ++i)
        morse_source.text[i] = toupper(new_text[i]);
    morse_source.text[morse_source.text_length] = 0x0;
    printf("Set new Morse text: %s\n", morse_source.text);
}

void MorseSource_change_mode(enum EMorseMode new_mode)
{
    morse_source.mode = new_mode;
}

enum EMorseMode string_to_MorseMode(const char* txt)
{
    if (strcasecmp(txt, "scroll") == 0)
        return MM_MORSE_SCROLL;
    if (strcasecmp(txt, "text") == 0)
        return MM_TEXT_SCROLL;
    if (strcasecmp(txt, "blink") == 0)
        return MM_MORSE_BLINK;
    return MM_NO_MODE;
}

void MorseSource_debug_init()
{
    for (int row = 0; row < MORSE_FONT_CHAR_H; row++)
    {
        char s[MORSE_FONT_CHAR_W * 26 + 1];
        for (int i = 0; i < MORSE_FONT_CHAR_W * 26; ++i) {
            if (morse_source.font[row][i] == 0) {
                s[i] = ' ';
            }
            else {
                s[i] = 'X';
            }
        }
        s[MORSE_FONT_CHAR_W * 26] = 0;
        printf("%s\n", s);
    }
    for (int i = 0; i < 26; i++)
    {
        struct MorseChar* mc = &morse_source.morse[i];
        printf("Char %i: ", i);
        for (int x = 0; x < mc->len; ++x)
        {
            printf(" %c", mc->data[x]);
        }
        printf("\n");
    }
    printf("TEXT: %s\n", morse_source.text);
}

void MorseSource_update_leds_morse(int frame, ws2811_t* ledstrip)
{
    //clear the strip
    int led_count = morse_source.basic_source.n_leds;
    for (int led = 0; led < led_count; ++led)
        ledstrip->channel[0].leds[led] = morse_source.basic_source.gradient.colors[0];

    // now render the text
    int offset = 1;
    int mframe = frame % led_count;
    for (int index = 0; index < morse_source.text_length; ++index)
    {
        char c = morse_source.text[index];
        if (c == ' ') {
            offset += 7;
            continue;
        }
        int letter_color = (index % 6) + 2; //<- 6 = number of letter colors, color 0 and 1 are reserved
        char* code = morse_source.cmorse[(int)c - 65];
        int code_length = strlen(code);
        for (int ddi = 0; ddi < code_length; ++ddi)
        {
            char dd = code[ddi];
            if (dd == '.')
            {
                ledstrip->channel[0].leds[(led_count + offset++ - mframe) % led_count] = morse_source.basic_source.gradient.colors[letter_color];
            }
            if (dd == '-')
            {
                ledstrip->channel[0].leds[(led_count + offset++ - mframe) % led_count] = morse_source.basic_source.gradient.colors[letter_color];
                ledstrip->channel[0].leds[(led_count + offset++ - mframe) % led_count] = morse_source.basic_source.gradient.colors[letter_color];
                ledstrip->channel[0].leds[(led_count + offset++ - mframe) % led_count] = morse_source.basic_source.gradient.colors[letter_color];
            }
            offset += 1;
        }
        offset += 3;
    }
    offset -= 3;
    ledstrip->channel[0].leds[(led_count - mframe) % led_count] = morse_source.basic_source.gradient.colors[1];
    ledstrip->channel[0].leds[(led_count + offset - mframe) % led_count] = morse_source.basic_source.gradient.colors[1];
}

int MorseSource_get_gradient_index_blink(int led, int frame)
{
    int msg_padding = 3;
    int shift = frame / 16;
    led = (led + shift) % morse_source.basic_source.n_leds;

    int i = led % (morse_source.text_length + msg_padding);
    if (i >= morse_source.text_length)
        return 1;
    int letter_color = (i % 6) + 2;
    char c = morse_source.text[i];
    if (c == ' ')
        return 0;
    i = (char)c - 65;
    struct MorseChar* mc = &morse_source.morse[i];
    int ft = frame % 16;
    if (ft >= mc->len)
        return 0;
    int y = mc->data[ft];
    if (y > 0)
        return letter_color;
    return 0;
}

int MorseSource_get_gradient_index_scroll(int led, int frame)
{
    int font_row = frame % (MORSE_FONT_CHAR_H + 10);
    int char_number = led / (MORSE_FONT_CHAR_W + 1);
    char_number = char_number % (morse_source.text_length + 1);
    if (char_number >= morse_source.text_length)
        return 1;
    int letter_color = (char_number % 6) + 2;
    int font_column = led % (MORSE_FONT_CHAR_W + 1);
    if (font_column == MORSE_FONT_CHAR_W)  // this is interspace column
        return 1;
    if (morse_source.text[char_number] == ' ')  // space is all empty
        return 0;
    if (font_row >= MORSE_FONT_CHAR_H)  // this is leading
        return 0;
    font_column += ((int)morse_source.text[char_number] - 65) * MORSE_FONT_CHAR_W;
    return (int)morse_source.font[font_row][font_column] * letter_color;
}

int MorseSource_update_leds(int frame, ws2811_t* ledstrip)
{
    int frames_per_dot = 10;
    int frames_per_row = 10;
    enum EMorseMode cur_mode = morse_source.mode;
    if (cur_mode == MM_NO_MODE) {
        cur_mode = (frame / MORSE_FRAMES_PER_MODE) % MM_NO_MODE; //<- 3 = number of modes
    }
    switch (cur_mode)
    {
    case MM_MORSE_SCROLL:
        MorseSource_update_leds_morse(frame, ledstrip);
        break;
    case MM_TEXT_SCROLL:
	    if(frame % frames_per_dot != 0) return 0;
        for (int led = 0; led < morse_source.basic_source.n_leds; ++led)
        {
            int y = MorseSource_get_gradient_index_scroll(led, frame / frames_per_dot);
            ledstrip->channel[0].leds[led] = morse_source.basic_source.gradient.colors[y];
        }
        break;
    case MM_MORSE_BLINK:
	    if(frame % frames_per_row != 0) return 0;
        for (int led = 0; led < morse_source.basic_source.n_leds; ++led)
        {
            int y = MorseSource_get_gradient_index_blink(led, frame / frames_per_row);
            ledstrip->channel[0].leds[led] = morse_source.basic_source.gradient.colors[y];
        }
        break;
    case MM_NO_MODE:
        break;
    }
    return 1;
}

// The whole message is e.g. LED MSG TEXT?HI%20URSULA, or LED MSG MODE?BLINK
// This function will only receive the part after LED MSG
void MorseSource_process_message(const char* msg)
{
    char* sep = strchr(msg, '?');
    if (sep == NULL)
    {
        printf("Message does not contain target %s\n", msg);
        return;
    }
    if ((sep - msg) >= 32)
    {
        printf("Target is too long or poorly formatted: %s\n", msg);
        return;
    }
    if ((strlen(sep + 1) >= 64))
    {
        printf("Message too long or poorly formatted: %s\n", msg);
        return;
    }
    char target[32];
    char payload[64];
    strncpy(target, msg, sep - msg);
    strncpy(payload, sep + 1, 64);
    target[sep - msg] = 0x0;
    if (!strncasecmp(target, "TEXT", 4))
    {
        MorseSource_assign_text(payload);
        printf("Setting new MorseSource text: %s\n", payload);
    }
    else if (!strncasecmp(target, "MODE", 4))
    {
        enum EMorseMode mode = string_to_MorseMode(payload);
        if (mode == MM_NO_MODE)
        {
            printf("Morse mode not found %s\n", payload);
            return;
        }
        MorseSource_change_mode(mode);
        printf("Setting new MorseSource mode: %s\n", payload);
    }
    else
        printf("Unknown target: %s, payload was: %s\n", target, payload);
}

void MorseSource_destruct()
{
    if (morse_source.text != NULL) free(morse_source.text);
    morse_source.text = NULL;
}

void MorseSource_init(int n_leds, int time_speed, uint64_t current_time)
{
    BasicSource_init(&morse_source.basic_source, n_leds, time_speed, source_config.colors[MORSE_SOURCE], current_time);
    MorseSource_read_font();
    MorseSource_convert_morse();
    MorseSource_assign_text("HELLO WORLD");
    MorseSource_change_mode(MM_NO_MODE);
    //MorseSource_debug_init();
}

void MorseSource_construct()
{
    BasicSource_construct(&morse_source.basic_source);
    morse_source.basic_source.init = MorseSource_init;
    morse_source.basic_source.update = MorseSource_update_leds;
    morse_source.basic_source.destruct = MorseSource_destruct;
    morse_source.basic_source.process_message = MorseSource_process_message;
}

MorseSource morse_source = {
    .basic_source.construct = MorseSource_construct,
    .cmorse = { ".-", "-...", "-.-.", "-..", ".", "..-.", "--.", "....", "..", ".---","-.-", ".-..", "--",
              "-.", "---", ".--.", "--.-", ".-.", "...", "-", "..-", "...-", ".--", "-..-", "-.--", "--.." },
    .morse = { {0, {0}} },
    .text = NULL,
    .text_length = 0,
    .mode = MM_NO_MODE
};
