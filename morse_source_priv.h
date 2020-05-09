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

void MorseSource_init(int n_leds, int time_speed);
void MorseSource_destruct();
//returns 1 if leds were updated, 0 if update is not necessary
int MorseSource_update_leds(int frame, ws2811_t* ledstrip);
