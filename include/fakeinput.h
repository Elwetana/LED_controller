#ifndef __FAKE_INPUT_H__
#define __FAKE_INPUT_H__

#define EV_SYN			0x00
#define EV_KEY			0x01
#define EV_REL			0x02
#define EV_ABS			0x03

#define ABS_X			0x00
#define ABS_Y			0x01
#define ABS_Z			0x02
#define ABS_RX			0x03
#define ABS_RY			0x04
#define ABS_RZ			0x05

#define ABS_HAT0X		0x10
#define ABS_HAT0Y		0x11

struct input_event {
	unsigned int type;
	unsigned int code;
	int value;
};

struct input_event ie;
int read(int input, struct input_event* inev, int size)
{
	(void)input;
	(void)inev;
	(void)size;
	return 1;
}


#endif  /* __FAKE_INPUT__ */
