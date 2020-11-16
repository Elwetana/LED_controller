#ifndef __CHASER_SOURCE_H__
#define __CHASER_SOURCE_H__

#define N_HEADS		14

typedef struct ChaserSource
{
	BasicSource basic_source;
	int heads[N_HEADS];
	int cur_heads[N_HEADS];
} ChaserSource;

extern ChaserSource chaser_source;

#endif /* __CHASER_SOURCE_H__ */
