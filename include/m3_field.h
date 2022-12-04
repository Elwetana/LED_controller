#ifndef __M3_FIELD_H__
#define __M3_FIELD_H__

#define C_MAX_FIELD_LENGTH 1024
#define N_MAX_SEGMENTS 32


typedef unsigned char jewel_type;

//! playing field
typedef struct TJewel {
    jewel_type type;
    double sin_phase;
    double cos_phase;
    int unique_id;
} jewel_t;

void Segments_print_info(int segment);
void Segments_update();
const int Segments_get_next_moving(int segment);
const int Segments_get_next_collapsing(int segment);
const double Segments_get_position(int segment);      //!< returns position relative to the LEDs
const int Segments_get_hole_position(int segment);
const int Segments_get_length(int segment);           //!< get number of jewels in segment, including collapsing jewels
const int Segments_get_direction(int segment);
const jewel_t Segments_get_jewel(int segment, int position); //!< gets jewel at position (position is relative to the start of the segment)
const jewel_type Segments_get_jewel_type(int segment, int position);
const jewel_type Segments_get_last_jewel_type(int segment);
void Segments_add_shift(int segment, int amount);
void Segments_set_discombobulation(int segment, int discombobulation);
const int Segments_get_discombobulation(int segment);
const double Segments_get_collapse_progress(int segment);
const int Segments_get_field_index(int segment, int position);
const int Segments_get_jewel_id(int segment, int position);

//const int Field_evaluate(const int field_index, const int segment);
void Field_init();
void Field_insert_and_evaluate(const int segment, const int position, jewel_type jewel_type, int led_discombobulation);
void Field_swap_and_evaluate(const int swap_segment, const int left_position, int led_discombobulation);
void Field_destruct();


#endif /* __M3_FIELD_H__ */
