#ifndef __M3_GAME_SOURCE_H__
#define __M3_GAME_SOURCE_H__

#define N_GEM_COLORS 6
#define DEBUG_M3

#ifdef DEBUG_M3
#define ASSERT_M3(a,b) assert((a))
#define ASSERT_M3_CONTINUE(a) assert((a))
#else
#define ASSERT_M3(a, b) if(!(a)) { printf("Assertion failed: " #a " at line %i, file %s", __LINE__, __FILE__); return (b); }
#define ASSERT_M3_CONTINUE(a) if(!(a)) printf("Assertion failed: " #a " at line %i, file %s", __LINE__, __FILE__)
#endif // DEBUG_M3

//this could be moved to more widely shared .h
#define ANSI_COLOR_RED     "\x1b[31m"
#define ANSI_COLOR_GREEN   "\x1b[32m"
#define ANSI_COLOR_YELLOW  "\x1b[33m"
#define ANSI_COLOR_BLUE    "\x1b[34m"
#define ANSI_COLOR_MAGENTA "\x1b[35m"
#define ANSI_COLOR_CYAN    "\x1b[36m"
#define ANSI_COLOR_RESET   "\x1b[0m"


enum EMatch3GamePhase
{
	M3GP_SELECT,
	M3GP_PLAY,
	M3GP_END
};


typedef struct Match3GameSource
{
	BasicSource basic_source;
	uint64_t start_time;
	int cur_frame;
	int n_players;
	enum EMatch3GamePhase current_phase;
} Match3GameSource;

extern Match3GameSource match3_game_source;


#define N_MAX_SEGMENTS 32
extern const int C_LED_Z;
extern const int C_SEGMENT_SHIFT;
extern const int C_BULLET_Z;


inline double miliseconds_from_start();
void m3_print_info(int led);

const int Match3_Emitor_get_length();
const int Match3_Emitor_fire();
const int Match3_Emitor_reload(int dir);
const int Match3_Game_catch_bullet(int led);
const int Match3_Game_swap_jewels(int led, int dir);


struct Match3Config
{
	int n_half_grad;				//!< the whole gradient is 2 * half_grad + 1 colours long, with the basic colour in the middle
									//i.e. 0  1 .. N_HALF_GRAD  N_HALF_GRAD+1 .. 2 * N_HALF_GRAD
									//     |           ^this is the basic colour              |
									//     ^ this is the darkest colour                       |
									//                                                        ^ this is the lightest colour
	double collapse_time;			//in seconds
	double gem_freq[N_GEM_COLORS];  //in Hertz
	int player_colour;				//0xAARRGGBB
	int collapse_colour;
	double player_move_cooldown;    //in miliseconds
	double player_catch_cooldown;   //in miliseconds
	int player_catch_distance;		//in leds
	int player_dit_length;          //in milliseconds
	int player_n_dits;              //it will be in config, no need for define
	unsigned char player_patterns[4][4]; //first index is player, second index is dit, each byte specifies pulse function for that dit
	double max_accelaration;		//how much can speed increase per one second
	double normal_forward_speed;	//normal forward speed (leds/second)
	double retrograde_speed;		//speed when backing down attracted, by jewel of the same type (leds/second)
	double slow_forward_speed;		//speed when not the first segment (leds/second)
	double bullet_speed;			//in leds per second
	double emitor_cooldown;			//in miliseconds
	double unswap_timeout;			//in miliseconds
};

extern const struct Match3Config match3_config;


#endif /* __M3_GAME_SOURCE_H__ */