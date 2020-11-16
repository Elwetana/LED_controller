#pragma once
typedef struct _snd_pcm snd_pcm_t;
typedef struct _snd_pcm_hw_params snd_pcm_hw_params_t;

extern int snd_pcm_hw_params(snd_pcm_t* pcm, snd_pcm_hw_params_t* params);

typedef enum _snd_pcm_format {
	SND_PCM_FORMAT_S16_LE,
	/** Signed 16 bit Big Endian */
} snd_pcm_format_t;

/** PCM stream (direction) */
typedef enum _snd_pcm_stream {
	/** Playback stream */
	SND_PCM_STREAM_PLAYBACK = 0,
	/** Capture stream */
	SND_PCM_STREAM_CAPTURE,
	SND_PCM_STREAM_LAST = SND_PCM_STREAM_CAPTURE
} snd_pcm_stream_t;

/** PCM access type */
typedef enum _snd_pcm_access {
	/** mmap access with simple interleaved channels */
	SND_PCM_ACCESS_MMAP_INTERLEAVED = 0,
	/** mmap access with simple non interleaved channels */
	SND_PCM_ACCESS_MMAP_NONINTERLEAVED,
	/** mmap access with complex placement */
	SND_PCM_ACCESS_MMAP_COMPLEX,
	/** snd_pcm_readi/snd_pcm_writei access */
	SND_PCM_ACCESS_RW_INTERLEAVED,
	/** snd_pcm_readn/snd_pcm_writen access */
	SND_PCM_ACCESS_RW_NONINTERLEAVED,
	SND_PCM_ACCESS_LAST = SND_PCM_ACCESS_RW_NONINTERLEAVED
} snd_pcm_access_t;

extern snd_pcm_format_width(snd_pcm_format_t format);