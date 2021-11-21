#ifndef __RAD_GAME_SOURCE_H__
#define __RAD_GAME_SOURCE_H__

/**
* Description of game modes:
*	RaD = Rhythm & Dance
* 
* every LED is an oscillator, with frequency f
* 
* 
*	A sin(f t + p) = A sin(f t) cos(p) + A cos(f t) sin(p) = C sin(f t) + S cos(f t)
*   where C = A cos(p) and S = A sin(p)
* 
*	Wave interference:
* 
*	A1 sin(f t + p1) + A2 sin(f t + p2) = 
*   A1 sin(f t) cos(p1) + A1 cos(f t) sin(p1) + A2 sin(f t) cos(p2) + A2 cos(f t) sin(p2) =
*   (A1 cos(p1) + A2 cos(p2)) sin(f t) + (A1 sin(p1) + A2 sin(p2)) cos(f t) = 
*   (C1 + C2) sin(f t) + (S1 + S2) cos(f t) = 
*   CC * sin(f t) + SS * cos(f t)
* 
*	let AA = sqrt(CC CC + SS SS)
* 
*   then we can write
*	= AA (CC/AA sin(f t) + SS/AA cos(f t))
* 
*	let PP = acos(CC/AA) = asin(SS/AA)
*
*	we get:	
*   = AA sin(f t + PP)
* 
*/

void Player_move_left(int player_index);
void Player_move_right(int player_index);
void Player_strike(int player_index);
void Player_freq_inc(int player_index);
void Player_freq_dec(int player_index);

typedef struct RadGameSource
{
	BasicSource basic_source;
	int first_update;
	uint64_t start_time;
	int n_players;
} RadGameSource;

extern RadGameSource rad_game_source;


#endif /* __RAD_GAME_SOURCE_H__ */
