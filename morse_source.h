#ifndef __MORSE_SOURCE_H__
#define __MORSE_SOURCE_H__

enum EMorseMode
{
	MM_MORSE_SCROLL,
	MM_TEXT_SCROLL,
	MM_MORSE_BLINK,
	MM_NO_MODE
};

extern SourceFunctions morse_functions;
void MorseSource_assign_text(const char* new_text);
void MorseSource_change_mode(int new_mode);

#endif /* __MORSE_SOURCE_H__ */