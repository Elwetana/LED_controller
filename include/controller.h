#ifndef __CONTROLLER_H__
#define __CONTROLLER_H__

/*
* This is extremely limited library for reading Xbox Controller input on Windows and Linux. Currently it only reads button presses;
* joysticks are converted to yes/no queries
*/

#define C_BTN_OFFSET 300

#define C_MAX_CONTROLLERS 4

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

	//left stick
    XBTN_LST_L  = 19,
    XBTN_LST_R  = 20,
	XBTN_LST_U  = 21,
	XBTN_LST_D  = 22,

	//right stick
	XBTN_RST_L  = 23,
	XBTN_RST_R  = 24,
	XBTN_RST_U  = 25,
	XBTN_RST_D  = 26,

	//analog triggers
	XBTN_LT		= 27,
	XBTN_RT		= 28
};

#define C_MAX_XBTN  29 //this must be define for statically allocated arrays

/*! Released = 0, Pressed = 1 */
enum EState
{
	BT_released,
	BT_pressed,
    BT_down
};

void Controller_init(void);
//! Returns 1 when a button was read, there may be potentially more buttons to read if they were pressed since last update
//! @param t current time (ignored on Windows, necessary to distinguish DOWN and PRESSED events on linux)
int Controller_get_button(uint64_t t, enum EButtons* button, enum EState* state, int player_index);
char* Controller_get_button_name(enum EButtons button);
int Controller_get_n_players(void);

#endif /* __CONTROLLER_H__ */
