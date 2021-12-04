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
*	*	*	*
* 
* Game progress 
* 
*   -- get ready - press start to play -- every player has to press "Start" button
*   -- play the level
*   -- finish level 
*	-- show score and if you reach the the required points, say the code for the next level
*		--if you reach the the required points go to next level
*		-- otherwise repeat the current one
*   -- either way, go back to get ready mode
* 
* Game script:
*	List of (song to play, mode to use, score to reach)
* 
*/
enum ERadGameModes
{
	RGM_Oscillators,    //!< goal is to make the whole led chain blink to rhythm
	RGM_DDR,            //!< just like Dance Dance Revolution
	RGM_Osc_Ready,		//!< getting ready to play Osciallators
	RGM_DDR_Ready,		//!< getting ready to play DDR
	RGM_Show_Score,		//!< after finishing level
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
void Player_start_pressed(int player_index);

typedef struct RadGameSource
{
	BasicSource basic_source;
	uint64_t start_time;
	int cur_frame;
	int n_players;
} RadGameSource;

extern RadGameSource rad_game_source;

struct RadGameSong
{
	char* filename;
	double* bpms;
	long* bpm_switch; //!< time in ms from the start of the song
	int n_bpms;
};

typedef struct SRadGameSongs
{
	struct RadGameSong* songs;
	double freq;		//!< frequency (in Hz) of the current song
	int n_songs;
	int current_song;
	long time_offset;	//< in ms
} RadGameSongs;

extern RadGameSongs rad_game_songs;

void start_current_song();

typedef struct SRadMovingObject
{
	double position;
	double speed;		//!< in leds/s
	int custom_data;	//!< user defined
	signed char moving_dir; //< 0 when not moving, +1 when moving right, -1 when moving left
} RadMovingObject;

void RadMovingObject_render(RadMovingObject* mo, int color, ws2811_t* ledstrip);

//return current ready player, if there is none, return 0xF
int Ready_get_current_player();
void Ready_clear_current_player();
int Ready_get_ready_players();
uint64_t Ready_get_effect_start();
void Ready_set_effect_start(uint64_t t);

#endif /* __RAD_GAME_SOURCE_H__ */
