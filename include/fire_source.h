#ifndef __FIRE_SOURCE_H__
#define __FIRE_SOURCE_H__

enum EmberType {
    BIG,
    SMALL,
    SPARK,
    N_EMBER_TYPES
};

typedef struct EmberData {
    float amp;
    float amp_rand;
    float x_space;
    float osc_amp;
    float osc_freq;
    float osc_freq_rand;
    float sigma;
    float sigma_rand;
    float decay;
    float decay_rand;
} EmberData;

typedef struct Ember {
    int i;
    float x;
    float amp;
    float osc_amp;
    float osc_freq;
    float osc_shift;
    float sigma;
    float decay;
    int age;
    enum EmberType type;
    float* cos_table;
    int cos_table_length;
    float** contrib_table;
} Ember;

typedef struct FireSource
{
    BasicSource basic_source;
    EmberData ember_data[N_EMBER_TYPES];
    Ember* embers;
    int n_embers_per_type[N_EMBER_TYPES];
    int n_embers;
} FireSource;

extern FireSource fire_source;

#endif /* __FIRE_SOURCE_H__ */