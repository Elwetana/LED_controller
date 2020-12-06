#ifndef __CONTROLLER_H__
#define __CONTROLLER_H__

/*
* This is extremely limited library for reading Xbox Controller input on Windows and Linux. Currently it only reads button presses,
* it ignores triggers and joysticks.
*/

#define C_BTN_OFFSET 300

/*! The actual codes defined in linux/input.h start with 304 for BTN_A 
* For better lookup we need codes to start from 0 */
enum EButtons
{
	DPAD_L		=  0,
	DPAD_R		=  1,
	DPAD_U		=  2,
	DPAD_D		=  3,
	XBTN_A		=  4,
	XBTN_B		=  5,

	XBTN_X		=  7,
	XBTN_Y		=  8,
	XBTN_ERROR	=  9,
	XBTN_LB		= 10,
	XBTN_RB		= 11,


	XBTN_Back	= 14,
	XBTN_Start	= 15,
	XBTN_Xbox	= 16,
	XBTN_L3		= 17,
	XBTN_R3		= 18,
};

#define C_MAX_XBTN  19 //this must be define for statically allocated arrays

/*! Released = 0, Pressed = 1 */
enum EState
{
	BT_released,
	BT_pressed
};

void Controller_init();
//Returns 1 when button was read, there may be potentially more buttons to read if they were pressed since last update
int Controller_get_button(enum EButtons* button, enum EState* state);
char* Controller_get_button_name(enum EButtons button);

#endif /* __CONTROLLER_H__ */
