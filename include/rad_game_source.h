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
	RGM_Game_Won,		//!< Players won
	RGM_N_MODES
};

enum ERAD_COLOURS
{
    DC_RED,
    DC_GREEN,
    DC_BLUE,
    DC_YELLOW
};

typedef struct RadGameSource
{
	BasicSource basic_source;
	uint64_t start_time;
	int cur_frame;
	int n_players;
	void (*Player_hit_color)(int, enum ERAD_COLOURS);
	void (*Player_move)(int, signed char);
	void (*Player_start)(int);
} RadGameSource;

extern RadGameSource rad_game_source;
double RadGameSource_time_from_start_seconds();
void RadGameSource_set_start();

struct RadGameSong
{
	char* filename;
	double* bpms;
	long* bpm_switch; //!< time in us from the start of the song
	int n_bpms;
    long delay;
    int signature;
};

typedef struct SRadGameSongs
{
	struct RadGameSong* songs;
	double freq;		//!< frequency (in Hz) of the current song
	int n_songs;
	int current_song;
	int current_bpm_index;
	double current_beat;
	long last_update;
	long time_offset;	//< in ms
} RadGameSongs;

extern RadGameSongs rad_game_songs;
/*!
 * @brief check if the song frequency haven't changed
 * @param t current song elapsed time in microseconds
 * @return 1 if there is new frequency, 0 if the frequency is the same
*/
int RadGameSong_update_freq(long t);
void RadGameSong_start_random();

typedef struct SRadMovingObject
{
	double position;
	double speed;		//!< in leds/s
	int custom_data;	//!< user defined
	int target_beat;
	signed char moving_dir; //< 0 when not moving, +1 when moving right, -1 when moving left
} RadMovingObject;

void RadMovingObject_render(RadMovingObject* mo, int color, ws2811_t* ledstrip);

void GameMode_clear();
int GameMode_get_current_player();
void GameMode_clear_current_player();
void GameMode_lock_current_player();
int GameMode_get_ready_players();
long GameMode_get_score();
long GameMode_get_state();
void GameMode_set_state(long s);
int GameMode_get_last_result();
char* GameMode_get_code_wav();

void RadGameLevel_ready_finished();
void RadGameLevel_level_finished(long points);
void RadGameLevel_score_finished();


#endif /* __RAD_GAME_SOURCE_H__ */
