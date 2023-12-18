#ifndef __PAINT_SOURCE_H__
#define __PAINT_SOURCE_H__


/**
Modes:
 - Edit keyframe
 - Play

 Controls:
	
	== Edit ==

		Move brush			Left stick
		Hue					R1/L1
		Saturation			Right stick left/right
		Lightness			Right stick up/down
		Opacity				D-pad up/down
		Brush size			R2/L2
		Save keyframes		Y
		Apply				A
		Undo				B
		Redo				X
		Change keyframe		D-pad left/right
		Add keyframe		L3
		Delete keyframe		R3
		Enter Play mode		Start

	== Play ==

		Keyframe time
		Speed along chain
		Start/Stop moving along the chain
		Easing function between keyframes
		Load keyframes

 Display behaviour

	== Edit ==

	* Brush keeps blinking on/off

 */


typedef struct paint_SPaintSource
{
	BasicSource basic_source;
	uint64_t start_time;
	int cur_frame;
	int* leds;
} paint_PaintSource_t;

extern paint_PaintSource_t paint_source;

void Paint_BrushMove(int direction);
void Paint_HueChange(int direction);
void Paint_SaturationChange(int direction);
void Paint_LightnessChange(int direction);

#endif /* __PAINT_SOURCE_H__ */
