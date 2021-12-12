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

void SoundPlayer_init(int frame_time);

/*!
 * @brief keep playing the current file
 * @return current timestamp (i.e. how much of the song was already played) in us
 */
long SoundPlayer_play(enum ESoundEffects new_effect);

void SoundPlayer_destruct();

#endif /* __SOUND_PLAYER_H */
