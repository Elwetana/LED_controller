#ifndef __PAINT_SOURCE_H__
#define __PAINT_SOURCE_H__


/**
The original idea was that the players/users will be using a gamepad to paint on the LED chain. There would be buttons to change
colour of the brush, and size of the brush and transparency and what not, and set state of the brush as an animation keyframe and
then create another one... The remnants of that idea can still be seen around, I did not have time to properly extirpate them all. 

Fortunately, I told about that to my wife and she said she couldn't be bothered to learn such a complicated control scheme and so
I wrote a control panel in JavaScript and it is part of the other repostitory, LED_programs. So everything this source does is to 
listen to the message from the Python server and then updates all the LEDs. The only thing it does on its own is animation, but we
have no fancy keyframes and such, we can just run the pattern along the chain and/or change the lightness slighlty.

Originally, I intended this to be really just a painting application, no games, no secret messages. In the end it ended like it
always does.
 */


typedef struct paint_SPaintSource
{
	BasicSource basic_source;
	uint64_t start_time;
	int cur_frame;
} paint_PaintSource_t;

extern paint_PaintSource_t paint_source;

#endif /* __PAINT_SOURCE_H__ */
