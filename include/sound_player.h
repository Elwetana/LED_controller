#ifndef __SOUND_PLAYER_H__
#define __SOUND_PLAYER_H__


enum ESoundEffects
{
	SE_Reward,
	SE_N_EFFECTS
};

/*!
 * @brief Init hardware and start to play
 * @param samplerate sample per seconds, typicall 44100
 * @param channels mono/stereo, 1 or 2
 * @param frame_time desired length of one update in us
 * @param fileToPlay path to the file to starty playing
 */
void SoundPlayer_init(unsigned int samplerate, unsigned int channels, int frame_time, char* filename);

/*!
 * @brief keep playing the current file
 * @return current timestamp (i.e. how much of the song was already played) in us
 */
long SoundPlayer_play(enum ESoundEffects new_effect);

#endif /* __SOUND_PLAYER_H */
