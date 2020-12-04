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
	BTN_A  = 304,
	BTN_B  = 305,
	BTN_X  = 307,
	BTN_Y  = 308,
	BTN_ERROR = 309,
	BTN_LB = 310,
	BTN_RB = 311,
	BTN_Back  = 314,
	BTN_Start = 315,
	BTN_Xbox  = 316,
	BTN_L3 = 317,
	BTN_R3 = 318
};

enum EState
{
	BT_released,
	BT_pressed
};

//Returns 1 when button was read, there may be potentially more buttons to read if they were pressed since last update
int Controller_get_button(enum EButtons* button, enum EState* state);
char* Controller_get_button_name(enum EButtons button);

#endif /* __CONTROLLER_H__ */
