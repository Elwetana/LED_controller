#ifndef __MORSE_SOURCE_H__
#define __MORSE_SOURCE_H__

#define MORSE_FONT_CHAR_W  5
#define MORSE_FONT_CHAR_H  7
#define MORSE_FRAMES_PER_MODE  1000

struct MorseChar {
    int len;
    char data[16]; //<- no character has more than 4 dashes: 4*3 + 4 spaces = 16
};

typedef struct MorseSource {
    BasicSource basic_source;
    char* cmorse[26];
    char font[MORSE_FONT_CHAR_H][26 * MORSE_FONT_CHAR_W];
    struct MorseChar morse[26];
    char* text;
    int text_length;
    int mode;
} MorseSource;

enum EMorseMode
{
	MM_MORSE_SCROLL,
	MM_TEXT_SCROLL,
	MM_MORSE_BLINK,
	MM_NO_MODE
};

extern MorseSource morse_source;


//TODO smazat
void MorseSource_assign_text(const char* new_text);
void MorseSource_change_mode(int new_mode);

#endif /* __MORSE_SOURCE_H__ */