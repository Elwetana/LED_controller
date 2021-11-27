#ifndef __RAD_GAME_SOURCE_H__
#define __RAD_GAME_SOURCE_H__

/**
* RaD = Rhythm & Dance
* 
* Description of game modes:
*
* Oscillators:
*	- every LED is an oscillator, with frequency f
* 
*		(1)		A sin(f t + p) = A sin(f t) cos(p) + A cos(f t) sin(p) = A (C sin(f t) + S cos(f t))
* 
*		where C = cos(p) and S = sin(p)
*		note that C and S are normalized, i.e. C^2 + S^2 = 1
* 
*		When calculating interference -- that's what we do when player hits a button -- we add the 
*       respective C's and S's and normalize them:
*
*		A_new = sqrt( (A1 C1 + A2 C2)^2 + (A1 S1 + A2 S2)^2)
*		C_new = (A1 C1 + A2 C2) / A_new
*		S_new = (A1 S1 + A2 S2) / A_new 
* 
*/
enum ERadGameModes
{
	RGM_Oscillators,    //!< goal is to make the whole led chain blink to rhythm
	RGM_DDR,            //!< just like Dance Dance Revolution
	RGM_N_MODES
};


void Player_move_left(int player_index);
void Player_move_right(int player_index);
void Player_freq_inc(int player_index);
void Player_freq_dec(int player_index);
void Player_time_offset_inc(int player_index);
void Player_time_offset_dec(int player_index);
void Player_hit_red(int player_index);
void Player_hit_green(int player_index);
void Player_hit_blue(int player_index);
void Player_hit_yellow(int player_index);


typedef struct RadGameSource
{
	BasicSource basic_source;
	uint64_t start_time;
	int n_players;
	enum ERadGameModes game_mode;
} RadGameSource;

extern RadGameSource rad_game_source;


#endif /* __RAD_GAME_SOURCE_H__ */
