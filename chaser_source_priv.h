#define N_HEADS		14

typedef struct ChaserSource
{
	BasicSource basic_source;
	int heads[N_HEADS];
	int cur_heads[N_HEADS];
} ChaserSource;

