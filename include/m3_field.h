#ifndef __M3_FIELD_H__
#define __M3_FIELD_H__

#define C_MAX_FIELD_LENGTH 1024

//! playing field
typedef struct TJewel {
    jewel_type type;
    double sin_phase;
    double cos_phase;
    int unique_id;
} jewel_t;

typedef struct TMatch3BulletInfo {
    int bullet_index;
    int segment_position;
} match3_BulletInfo_t;

void Segments_print_info(int segment);
int Segments_update(void);
int Segments_get_next_moving(int segment);
int Segments_get_next_collapsing(int segment);
double Segments_get_position(int segment);      //!< returns position relative to the LEDs
int Segments_get_hole_position(int segment);
int Segments_get_length(int segment);           //!< get number of jewels in segment, including collapsing jewels
int Segments_get_direction(int segment);
jewel_t Segments_get_jewel(int segment, int position); //!< gets jewel at position (position is relative to the start of the segment)
jewel_type Segments_get_jewel_type(int segment, int position);
jewel_type Segments_get_last_jewel_type(int segment);
void Segments_add_shift(int segment, int amount);

void Segments_reset_bullets(int segment);
void Segments_add_bullet(int segment, int bullet_index, int segment_position);
int Segments_get_n_bullets(int segment);
match3_BulletInfo_t Segments_get_bullet(int segment, int segment_bullet_index);

void Segments_set_discombobulation(int segment, int discombobulation);
int Segments_get_discombobulation(int segment);
double Segments_get_collapse_progress(int segment);
int Segments_get_field_index(int segment, int position);
int Segments_get_jewel_id(int segment, int position);

//const int Field_evaluate(const int field_index, const int segment);
void Field_init(match3_LevelDefinition_t level_definition);
void Field_init_with_clue(const jewel_type field_def[], int def_length);
void Field_insert_and_evaluate(const int insert_segment, const int position, jewel_type jewel_type, int bullet_index);
int Field_swap_and_evaluate(const int swap_segment, const int left_position);
void Field_destruct(void);


#endif /* __M3_FIELD_H__ */
