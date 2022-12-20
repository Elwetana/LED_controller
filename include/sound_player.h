#ifndef __SOUND_PLAYER_H__
#define __SOUND_PLAYER_H__


enum ESoundEffects
{
	SE_Reward01,
	SE_Reward02,
	SE_Reward03,
	SE_Reward04,
	SE_Player1,
	SE_Player2,
	SE_Player3,
	SE_Player4,
	SE_GetReady,
	SE_PressStart,
	SE_Lose,
	SE_Win,
	SE_WinGame,
	SE_M3_BulletFired,
	SE_M3_BallImpact,
	SE_M3_JewelsDing01,
	SE_M3_JewelsDing02,
	SE_M3_JewelsDing03,
	SE_M3_Tick,
	SE_M3_Fanfare,
	SE_N_EFFECTS
};

/*!
 * @brief Init hardware and start to play
 * @param samplerate sample per seconds, typicall 44100
 * @param channels mono/stereo, 1 or 2
 * @param frame_time desired length of one update in us
 * @param fileToPlay path to the file to starty playing
 */
void SoundPlayer_start(char* filename);
void SoundPlayer_start_looped(char* filename);

void SoundPlayer_init(int frame_time);

/*!
 * @brief keep playing the current file
 * @return -1 when nothing is playing, -2 when only effect is playing, elapsed time in track in us 
 */
long SoundPlayer_play(enum ESoundEffects new_effect);
enum ESoundEffects SoundPlayer_get_current_effect();

void SoundPlayer_stop();

void SoundPlayer_destruct();

#endif /* __SOUND_PLAYER_H */
