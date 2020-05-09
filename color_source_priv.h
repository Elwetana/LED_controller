typedef struct ColorSource
{
	BasicSource basic_source;
	int first_update;
} ColorSource;

void ColorSource_init(int n_leds, int time_speed);
void ColorSource_destruct();
//returns 1 if leds were updated, 0 if update is not necessary
int ColorSource_update_leds(int frame, ws2811_t* ledstrip);

