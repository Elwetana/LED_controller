
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <linux/input.h>


#include <unistd.h>
#include <linux/input.h>
#include <fcntl.h>

/*
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

int main(int argc, char* argv[])
{
    struct input_event ie;
    int input = open("/dev/input/event0", O_RDONLY | O_NONBLOCK);
    while(1)
    {
        //char buffer[24];
        
        int len = read(input, &ie, sizeof(struct input_event));
        if(len > 0) 
        {
            if(ie.type == 1)
            {
                printf("Type: %d, Code: %d, Value: %d\n", ie.type, ie.code, ie.value);
            }
        }
        else
        {
            //printf(".");
        }
    }
}
