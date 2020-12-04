
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>


#include <unistd.h>
#include <linux/input.h>
#include <fcntl.h>

/*
 * type == 3, Code ==
 * 304 A
 * 305 B
 * 307 X
 * 308 Y
 * 310 LB
 * 311 RB
 * 314 Back
 * 315 Start
 * 316 Xbox
 * 317 L3
 * 318 R3
*/

char* EV_codes[] = {"EV_SYN", "EV_KEY", "EV_REL", "EV_ABS", "EV_MSC"}; //4
char* BTN_names[320];

void init_bttns()
{
    BTN_names[0x130] = "BTN_A";
    BTN_names[0x131] = "BTN_B";
    BTN_names[0x132] = "BTN_C";
    BTN_names[0x133] = "BTN_X";
}
/*#define BTN_C           0x132
#define BTN_NORTH       0x133
#define BTN_X           BTN_NORTH
#define BTN_WEST        0x134
#define BTN_Y           BTN_WEST
#define BTN_Z           0x135
#define BTN_TL          0x136
#define BTN_TR          0x137
#define BTN_TL2         0x138
#define BTN_TR2         0x139
#define BTN_SELECT      0x13a
#define BTN_START       0x13b
#define BTN_MODE        0x13c
#define BTN_THUMBL      0x13d
#define BTN_THUMBR      0x13e
*/

int main(int argc, char* argv[])
{
    struct input_event ie;
    int input = open("/dev/input/event0", O_RDONLY | O_NONBLOCK);
    int last_update = 0;
    while(1)
    {
        int len = read(input, &ie, sizeof(struct input_event));
        while(len > 0) 
        {
            if(ie.type == EV_KEY) 
            {
                if(ie.value == 1) printf("Key pressed %i\n", ie.code);
                else if(ie.value == 0) printf("Key released %i\n", ie.code);
                else printf("Unknown value %i\n", ie.value);
            }
            else if(ie.type == EV_ABS)
            {
                if(ie.code == ABS_HAT0X)
                {
                    if(ie.value == -1) printf("D-pad left pressed\n");
                    else if(ie.value == 1) printf("D-pad right pressed\n");
                    else if(ie.value == 0) printf("D-pad left/right released\n");
                    else printf("Unknown value %i\n", ie.value);
                }
                else if(ie.code == ABS_X)
                {
                    //printf("Horizontal move with value %i\n", ie.value);
                }
                else if(ie.code == ABS_RX)
                {
                    //printf("R-Horizontal move with value %i\n", ie.value);
                }
                else if(ie.code == ABS_HAT0Y)
                {
                    if(ie.value == -1) printf("D-pad up pressed\n");
                    else if(ie.value == 1) printf("D-pad down pressed\n");
                    else if(ie.value == 0) printf("D-pad up/down released\n");
                    else printf("Unknown value %i\n", ie.value);
                }
                else if(ie.code == ABS_Y)
                {
                    //printf("Vertical move with value %i\n", ie.value);
                }
                else if(ie.code == ABS_RY)
                {
                    //printf("R-Vertical move with value %i\n", ie.value);
                }
                else if(ie.code == ABS_Z)
                {
                    //this is left trigger, value is 0 .. 255 amount of pressure
                }
                else if(ie.code == ABS_RZ)
                {
                    //this is right trigger, value is 0 .. 255 amount of pressure
                }
                else
                {
                    printf("Unknown EV_ABS code: %i, value: %i\n", ie.code, ie.value);
                }
            }
            else if(ie.type != EV_SYN)
            {
                printf("Unknown type: %i\n", ie.type);
            }
            len = read(input, &ie, sizeof(struct input_event));
            last_update = 1;
        }
        if(last_update) 
        {
            //printf("\n");
            last_update = 0;
        }
 
    }
}
