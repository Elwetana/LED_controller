#ifndef __CONTROLLER_H__
#define __CONTROLLER_H__

/*
* This is extremely limited library for reading Xbox Controller input on Windows and Linux. Currently it only reads button presses,
* it ignores triggers and joysticks.
*/

enum EButtons
{
	DPAD_L = 300,
	DPAD_R = 301,
	DPAD_U = 302,
	DPAD_D = 303,
	XBTN_A = 304,
	XBTN_B = 305,
	XBTN_X = 307,
	XBTN_Y = 308,
	XBTN_ERROR = 309,
	XBTN_LB = 310,
	XBTN_RB = 311,
	XBTN_Back  = 314,
	XBTN_Start = 315,
	XBTN_Xbox  = 316,
	XBTN_L3 = 317,
	XBTN_R3 = 318
};

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
