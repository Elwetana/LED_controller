#ifndef __MORSE_SOURCE_H__
#define __MORSE_SOURCE_H__

#define MORSE_FONT_CHAR_W  5
#define MORSE_FONT_CHAR_H  7
#define MORSE_FRAMES_PER_MODE  1000

struct MorseChar {
    int len;
    char data[19]; //<- no character has more than 4 dashes: 4*3 + 4 spaces = 16 + 3 dits as right padding
};

enum EMorseMode
{
    MM_MORSE_SCROLL,
    MM_TEXT_SCROLL,
    MM_MORSE_BLINK,
    MM_NO_MODE
};

typedef struct MorseSource {
    BasicSource basic_source;
    char* cmorse[26];
    char font[MORSE_FONT_CHAR_H][26 * MORSE_FONT_CHAR_W];
    struct MorseChar morse[26];
    char* text;
    int text_length;
    enum EMorseMode mode;
} MorseSource;

extern MorseSource morse_source;

//! @brief Fills buf with string of dashes and dots, i.e a=.-, b=-... and so on
//! @param buf buffer with length at least 5
//! @param c uppercase letter A to Z
void MorseSource_get_code(char* buf, const char c);
void MorseSource_make_morse_char(struct MorseChar* mc, char* m, int add_padding);

#endif /* __MORSE_SOURCE_H__ */
