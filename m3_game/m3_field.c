#define _CRT_SECURE_NO_WARNINGS

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <assert.h>
#ifdef __linux__
#include "ws2811.h"
#include <unistd.h>
#include <linux/input.h>
#include <fcntl.h>
#else
#include "fakeled.h"
#endif // __linux__


#include "controller.h"
#include "common_source.h"
#include "m3_game_source.h"
#include "m3_field.h"

#define N_MAX_SEGMENTS 16
const int C_MATCH_3_LENGTH = 3;


jewel_t field[C_MAX_FIELD_LENGTH];
int field_length = 0;

enum ESegmentType {
    ST_MOVING,
    ST_COLLAPSING
};

typedef struct TMovingSegment {
    double speed;   //!< in leds/second
    int discombobulation;
} moving_segment_t;

typedef struct TCollapsingSegment {
    double collapse_progress;
} collapasing_segment_t;

//! Segments of the field that move as one block
typedef struct TSegment {
    int start;      //!< this is index into field
    int length;
    double shift;   //!< offset againt start, first led of the field is start + shift
    int debug;
    enum ESegmentType segment_type;
    union {
        moving_segment_t moving;
        collapasing_segment_t collapsing;
    };
} segment_t;

segment_t segments[N_MAX_SEGMENTS];
int n_segments = 0;

//! @brief Get the next segment (i.e. after the right one). To get the first one, call it with -1. 
//! @param segment 
//! @return segment index equal to \p segment or higher, that is of the type Moving, -1 if none was found
const int Segments_get_next_moving(int segment)
{
    assert(segment < n_segments);
    do
    {
        if (++segment == n_segments)
            return -1;
    } while (segments[segment].segment_type != ST_MOVING);
    return segment;
}

const int Segments_get_prev_moving(int segment)
{
    assert(segment < n_segments);
    assert(segment >= 0);
    do {
        if (--segment < 0) 
            return -1;
    } while (segments[segment].segment_type != ST_MOVING);
    return segment;
}

const int Segments_get_next_collapsing(int segment)
{
    assert(segment < n_segments);
    do {
        if (++segment == n_segments)
            return -1;
    } while (segments[segment].segment_type != ST_COLLAPSING);
    return segment;
}

const double Segments_get_position(int segment)
{
    assert(segment < n_segments);
    return segments[segment].shift;
}

const int Segments_get_length(int segment)
{
    assert(segment < n_segments);
    return segments[segment].length;
}

const int Segments_get_direction(int segment)
{
    assert(segment < n_segments);
    assert(segments[segment].segment_type == ST_MOVING);
    if (segments[segment].moving.speed > 0)
    {
        //moving left to right, the hole appears on right and travels to left, we shift jewels that are after the hole
        //offset is increasing
        return +1;
    }
    else if (segments[segment].moving.speed < 0)
    {
        //moving right to left, hole appears on left and travels right, we shift jewels that are before hole
        //offset is decreasing
        return -1;
    }
    return 0;
}

const jewel_t Segments_get_jewel(int segment, int position)
{
    assert(segment < n_segments);
    assert(position < Segments_get_length(segment));
    return field[segments[segment].start + position];
}

const unsigned char Segments_get_jewel_type(int segment, int position)
{
    assert(segment < n_segments);
    assert(position < Segments_get_length(segment));
    return field[segments[segment].start + position].type;
}

const unsigned char Segments_get_last_jewel_type(int segment)
{
    assert(segment < n_segments);
    int segment_length = Segments_get_length(segment);
    int pos = segments[segment].start + segment_length - 1;
    return (pos >= 0) ? field[pos].type : 0xFF;
}

void Segments_add_shift(int segment, int amount)
{
    assert(segment < n_segments);
    assert(segments[segment].segment_type == ST_MOVING);
    segments[segment].shift += amount;
}

void Segments_set_discombobulation(int segment, int discombobulation)
{
    assert(segment < n_segments);
    assert(segments[segment].segment_type == ST_MOVING);
    segments[segment].moving.discombobulation = discombobulation;
}

const unsigned int Segments_get_discombobulation(int segment)
{
    assert(segment < n_segments);
    assert(segments[segment].segment_type == ST_MOVING);
    return segments[segment].moving.discombobulation;
}

const double Segments_get_collapse_progress(int segment)
{
    assert(segment < n_segments);
    assert(segments[segment].segment_type == ST_COLLAPSING);
    return segments[segment].collapsing.collapse_progress;
}

static double calculate_segment_speed(int segment);

//! \brief Split segment at position, create new collapsing segment, stop all segments before and including segment
//!     field: 0 1 2 3 4 X X 7 8 9 . . . 
//!   segment:     ^     ^ ^ ^
//!                |     | |  position = 5
//!                |  collapse_length = 2
//!                start = 2, length = 8
//! 
//! \param  segment new segment will appear after it
//! \param position new segment will start at position
static void collapse_segment(const int segment, const int position, const int collapse_length)
{
    assert(!(position < collapse_length));
    int n_inserts = 2;
    int segment_length = Segments_get_length(segment);
    int collapse_index = segment + 1;
    int new_moving_index = segment + 2;
    if (position == segment_length - 1) //collapse is at the end
    {
        n_inserts--;
        new_moving_index = -1;
    }
    if (position == collapse_length)    //collapse at the beginning
    {
        n_inserts--;
        collapse_index = segment;
    }

    n_segments += n_inserts;
    for (int si = n_segments - 1; si > segment + n_inserts - 1; --si)
    {
        segments[si] = segments[si - n_inserts];
        if(segments[si].segment_type == ST_MOVING)
            segments[si].moving.speed = 0;
    }
    segment_t collapsing_segment = {
        .start = segments[segment].start + position - collapse_length,
        .length = collapse_length,
        .shift = segments[segment].shift + position - collapse_length,
        .segment_type = ST_COLLAPSING,
        .collapsing.collapse_progress = 1.0,
        .debug = match3_game_source.basic_source.current_time / 1000L / 1000L
    };
    if (collapse_index > segment) //the segment will be trimmed
    {
        segments[segment].length = position - collapse_length;
    }
    if (new_moving_index > -1) //new segment will be inserted to the right of the collapsing segment
    {
        segment_t moving_segment = {
            .start = segments[segment].start + position,
            .length = segment_length - position,
            .shift = segments[segment].shift + position,
            .segment_type = ST_MOVING,
            .moving.speed = 0,
            .moving.discombobulation = Segments_get_discombobulation(segment)
        };
        segments[new_moving_index] = moving_segment;
    }
    segments[collapse_index] = collapsing_segment;
    if (new_moving_index > -1)
    {
        double new_speed = calculate_segment_speed(new_moving_index);
        if (new_speed < 0.) segments[new_moving_index].shift += 1.;
    }
}

//! @brief This happens when the whole segment collapses, it is then completely removed, or when two segments are merged
//! @param left_segment segment to delete
//! @param shift_length only necessary when shifting field before deleting segment, otherwise must be 0
static void delete_segment(const int left_segment, int shift_length)
{
    //assert(segments[left_segment].segment_type == ST_COLLAPSING); -- this is only true when removing collapsed, not when merging
    for (int segment = left_segment; segment < n_segments - 1; ++segment)
    {
        segments[segment] = segments[segment + 1];
        segments[segment].start -= shift_length;
    }
    n_segments--;
}

static void merge_segments(const int left_segment, int right_segment)
{
    assert(left_segment < n_segments - 1); //there must be at least one segment to the right
    assert(right_segment < n_segments);
    assert(segments[left_segment].segment_type == ST_MOVING);
    assert(segments[right_segment].segment_type == ST_MOVING);
    assert(right_segment == Segments_get_next_moving(left_segment));

    //the jewels in segment have to be continous, we might overwrite some collapsing segment, if that happens we will just delete it
    while (right_segment - left_segment > 1)
    {
        delete_segment(right_segment - 1, 0);
        right_segment--;
    }

    //we have to shift jewels in the field so that the dead and collapsing jewel in the left segment are overwritten
    int shift_length = segments[right_segment].start - segments[left_segment].start - Segments_get_length(left_segment);
    if (shift_length > 0)
    {
        int start_pos = segments[right_segment].start - shift_length;
        for (int fi = start_pos; fi < field_length - shift_length; ++fi)
        {
            field[fi] = field[fi + shift_length];
        }
        field_length -= shift_length;
    }
    //new segment length will be total of the two
    segments[left_segment].length += segments[right_segment].length;

    //now we have to update start of all segments to the right, at the same time we also overwrite the right segment
    delete_segment(right_segment, shift_length);
}

//! @brief Evaluate field around position in segment, if the neighbouring jewels are of the same type, collapse them
//! @param  position  position of the jewel with the type for which we check
//! @param   segment  index of segment where the jewel is
//! @return probably nothing interesting
static const void evaluate_field(const int position, const int segment)
{
    assert(segments[segment].segment_type == ST_MOVING);
    unsigned char type = Segments_get_jewel_type(segment, position);
    int length = 1;
    int segment_length = Segments_get_length(segment);

    int pos_end = position + 1;
    while (pos_end < segment_length && Segments_get_jewel_type(segment, pos_end) == type) {
        pos_end++;
    }
    int pos_start = position - 1;
    while (pos_start >= 0 && Segments_get_jewel_type(segment, pos_start) == type) {
        pos_start--;
    }
    //now the number of the jewels of the same type is (pos_end - pos_start - 1). This must be at least C_MATCH_3_LENGTH for collapse to occur
    int same_length = pos_end - pos_start - 1;
    if (same_length < C_MATCH_3_LENGTH)
    {
        printf("Not enough jewels in match %i\n", same_length);
        return -1;
    }

    //we have a match, we have to split the segment and start collapse
    printf("Segment %i split at position %i\n", segment, pos_end);
    collapse_segment(segment, pos_end, same_length);
}

//! @brief Inserts a new \p jewel at \p position in \p insert_segment. All jewels in the field at \p position and higher 
//! will be shifted right. All segments starts will be updated. In the end, call evaluate_field to see if there will be 
//! a collapse
//! @param position 
//! @param insert_segment 
//! @param jewel 
void Field_insert_and_evaluate(const int position, const int insert_segment, jewel_t jewel)
{
    assert(insert_segment < n_segments);
    assert(position < Segments_get_length(insert_segment));
    assert(segments[insert_segment].segment_type == ST_MOVING);

    field_length += (field_length == C_MAX_FIELD_LENGTH - 1) ? 0 : 1; //if we are at the max field length, the last jewel will be simply overwritten
    int fi_insert = segments[insert_segment].start + position;
    for (int fi = field_length - 1; fi > fi_insert; --fi)
    {
        field[fi] = field[fi - 1];
    }
    field[fi_insert] = jewel;
    for (int segment = insert_segment + 1; segment < n_segments; ++segment)
    {
        segments[segment].start += 1;
    }
    segments[insert_segment].moving.discombobulation--;

    evaluate_field(position, insert_segment);
}

static void set_segment_speed(int segment, double target_speed, double time_delta)
{
    assert(segments[segment].segment_type == ST_MOVING);
    if (segments[segment].moving.speed == target_speed)
        return;
    double max_change = match3_config.max_accelaration * time_delta;
    //printf("Max change %f\n", max_change);
    segments[segment].moving.speed = (segments[segment].moving.speed < target_speed) ?
        min(target_speed, segments[segment].moving.speed + max_change) :
        max(target_speed, segments[segment].moving.speed - max_change);
}

static double calculate_segment_speed(int segment)
{
    if (n_segments == 1)
        return match3_config.normal_forward_speed;
    if (segment == 0)
        return match3_config.slow_forward_speed;
    if (Segments_get_length(segment) == 0)
        return 0.;
    int left_segment = Segments_get_prev_moving(segment);
    if(left_segment == -1)
        return match3_config.slow_forward_speed;
    unsigned char left_type = Segments_get_last_jewel_type(left_segment);
    unsigned char right_type = Segments_get_jewel_type(segment, 0);
    if (left_type == right_type)
        return match3_config.retrograde_speed;
    return 0.;
}


//! @brief Update speeds and positions of segments, merge segments that got close to each other
//! Rules for the speeds:
//!     -- if there is only one segment, it moves at normal speed
//!     -- otherwise, if two neighboring segments have the same jewel type at the matching ends, 
//!        the left segment moves at slow speed, and right segment at retrograde speed
//!     -- otherwise, the left segment moves at the slow speed and and the right segment stands
void Segments_update()
{
    double time_delta = (double)(match3_game_source.basic_source.time_delta / 1000L) / 1e6;

    //check speed of all segments and update as needed
    for (int segment = 0; segment < n_segments; ++segment)
    {
        if (segments[segment].segment_type != ST_MOVING)
            continue;
        double target_speed = calculate_segment_speed(segment);
        set_segment_speed(segment, target_speed, time_delta);
        if (segments[segment].moving.speed == 0)
            continue;
        segments[segment].shift += segments[segment].moving.speed * time_delta;
    }


    //update collapsing segments
    for (int segment = n_segments; segment >= 0; --segment)
    {
        if (segments[segment].segment_type != ST_COLLAPSING)
            continue;
        segments[segment].collapsing.collapse_progress -= time_delta / match3_config.collapse_time;
    }

    //finally, we can check for segments to merge and delete
    int segment = n_segments - 1;
    while (segment >= 0)
    {
        if (segments[segment].segment_type == ST_COLLAPSING && segments[segment].collapsing.collapse_progress < 0)
        {
            delete_segment(segment, 0);
            int t = match3_game_source.basic_source.current_time / 1000L / 1000L - segments[segment].debug;
            printf("Removing jewels at segment %i, length is %i after %i ms\n", segment, segments[segment].length, t);
        }
        segment--;
    }

    segment = n_segments - 1;
    while (segment > 0)
    {
        if (segments[segment].segment_type != ST_MOVING)
        {
            segment--;
            continue;
        }
        int left_segment = Segments_get_prev_moving(segment);
        if (left_segment == -1)
        {
            segment--;
            continue;
        }
        int left_length = Segments_get_length(left_segment) + Segments_get_discombobulation(left_segment);
        double visual_distance = trunc(Segments_get_position(segment)) - trunc(Segments_get_position(left_segment)) - left_length;
        //if the left segment is moving right, or right segment is moving left, we have to decrease the visual distance by one in each case
        if (segments[segment].moving.speed < 0) visual_distance -= 1;
        if (segments[left_segment].moving.speed > 0) visual_distance -= 1;
        printf("Distance between %i and %i is %f\n", left_segment, segment, visual_distance);
        if (visual_distance < 1.)
        {
            printf("Merging segments: left %i\n", left_segment);
            merge_segments(left_segment, segment);
            evaluate_field(left_length, left_segment);
        }
        segment--;
    }

    if (n_segments == 0)
        printf("YOU WIN\n");
}


void Field_init()
{
    field_length = match3_game_source.basic_source.n_leds / 2;
    for (int i = 0; i < field_length; ++i)
    {
        field[i].type = (unsigned char)trunc(random_01() * (double)(N_GEM_COLORS - 1));
        double shift = random_01() * M_PI;
        field[i].sin_phase = sin(shift);
        field[i].cos_phase = cos(shift);
    }
    segments[0].start = 0;
    segments[0].shift = 80.01;
    segments[0].length = field_length;
    segments[0].segment_type = ST_MOVING;
    segments[0].moving.speed = match3_config.normal_forward_speed;
    n_segments = 1;
}

void Field_destruct()
{
    //free(field);
}