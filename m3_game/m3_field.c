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


#include "colours.h"
#include "controller.h"
#include "sound_player.h"
#include "common_source.h"
#include "m3_game_source.h"
#include "m3_field.h"

const int C_MATCH_3_LENGTH = 3;


jewel_t field[C_MAX_FIELD_LENGTH];
int field_length = 0;
double speed_bias = 1.;
double round_collapse_time;
jewel_type lastCollapseType = N_GEM_COLORS;

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
    match3_BulletInfo_t bullets[N_MAX_BULLETS];
    int n_bullets;
    int debug;
    enum ESegmentType segment_type;
    union {
        moving_segment_t moving;
        collapasing_segment_t collapsing;
    };
} segment_t;

segment_t segments[N_MAX_SEGMENTS];
int n_segments = 0;

struct {
    int left_jewel_id;
    int right_jewel_id;
    double timeout;
} unswaps[2 * C_MAX_CONTROLLERS];
int n_unswaps = 0;

/* Forward declarations */
static double calculate_segment_speed(int segment);
static jewel_t make_jewel(jewel_type type);

void Segments_print_info(int segment)
{
    ASSERT_M3(segment < n_segments, (void)0);
    printf("Segment id: %i, shift: %f, start: %i, length: %i ", segment, segments[segment].shift, segments[segment].start, segments[segment].length);
    switch (segments[segment].segment_type)
    {
    case ST_MOVING:
        printf("speed: %f, discomb.: %i\n", segments[segment].moving.speed, segments[segment].moving.discombobulation);
        break;
    case ST_COLLAPSING:
        printf("collapse: %f\n", segments[segment].collapsing.collapse_progress);
        break;
    }
}

//! @brief Get the next segment (i.e. after the right one). To get the first one, call it with -1. 
//! @param segment 
//! @return segment index equal to \p segment or higher, that is of the type Moving, -1 if none was found
int Segments_get_next_moving(int segment)
{
    ASSERT_M3(segment < n_segments, -1);
    do
    {
        if (++segment == n_segments)
            return -1;
    } while (segments[segment].segment_type != ST_MOVING);
    return segment;
}

int Segments_get_prev_moving(int segment)
{
    ASSERT_M3(segment < n_segments, -1);
    ASSERT_M3(segment >= 0, -1);
    do {
        if (--segment < 0) 
            return -1;
    } while (segments[segment].segment_type != ST_MOVING);
    return segment;
}

int Segments_get_next_collapsing(int segment)
{
    ASSERT_M3(segment < n_segments, -1);
    do {
        if (++segment == n_segments)
            return -1;
    } while (segments[segment].segment_type != ST_COLLAPSING);
    return segment;
}

double Segments_get_position(int segment)
{
    ASSERT_M3(segment < n_segments, 0);
    return segments[segment].shift;
}

int Segments_get_length(int segment)
{
    ASSERT_M3(segment < n_segments, 1);
    return segments[segment].length;
}

int Segments_get_direction(int segment)
{
    ASSERT_M3(segment < n_segments, 0);
    ASSERT_M3(segments[segment].segment_type == ST_MOVING, 0);
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

jewel_t Segments_get_jewel(int segment, int position)
{
    ASSERT_M3(segment < n_segments, field[segments[0].start]);
    ASSERT_M3(position < Segments_get_length(segment), field[segments[segment].start]);
    return field[segments[segment].start + position];
}

jewel_type Segments_get_jewel_type(int segment, int position)
{
    ASSERT_M3(segment < n_segments, (jewel_type)0);
    ASSERT_M3(position < Segments_get_length(segment), (jewel_type)0);
    return field[segments[segment].start + position].type;
}

jewel_type Segments_get_last_jewel_type(int segment)
{
    ASSERT_M3(segment < n_segments, (jewel_type)0);
    int segment_length = Segments_get_length(segment);
    int pos = segments[segment].start + segment_length - 1;
    return (pos >= 0) ? field[pos].type : 0xFF;
}

int Segments_get_field_index(int segment, int position)
{
    ASSERT_M3(segment < n_segments, segments[0].start);
    ASSERT_M3(position < Segments_get_length(segment), segments[segment].start);
    return segments[segment].start + position;
}

static int Field_get_segment(int field_index)
{
    for (int si = 0; si < n_segments; ++si)
    {
        int start = segments[si].start;
        int len = Segments_get_length(si);
        if (field_index >= start && field_index < start + len)
            return si;
    }
    return -1;
}

int Segments_get_jewel_id(int segment, int position)
{
    ASSERT_M3(segment < n_segments, -1);
    ASSERT_M3(position < Segments_get_length(segment), -1);
    return field[Segments_get_field_index(segment, position)].unique_id;
}

void Segments_add_shift(int segment, int amount)
{
    ASSERT_M3(segment < n_segments, (void)0);
    printf("Shifting segment %i by %i\n", segment, amount);
    segments[segment].shift += amount;
}

void Segments_reset_bullets(int segment)
{
    ASSERT_M3(segment < n_segments, (void)0);
    segments[segment].n_bullets = 0;
}

void Segments_add_bullet(int segment, int bullet_index, int segment_position)
{
    ASSERT_M3(segment < n_segments, (void)0);
    int n = segments[segment].n_bullets;
    segments[segment].bullets[n].bullet_index = bullet_index;
    segments[segment].bullets[n].segment_position = segment_position;
    segments[segment].n_bullets = n + 1;
}

int Segments_get_n_bullets(int segment)
{
    ASSERT_M3(segment < n_segments, (void)0);
    return segments[segment].n_bullets;
}

match3_BulletInfo_t Segments_get_bullet(int segment, int segment_bullet_index)
{
    ASSERT_M3(segment < n_segments, (void)0);
    ASSERT_M3(segment_bullet_index < segments[segment].n_bullets, (void)0);
    return segments[segment].bullets[segment_bullet_index];
}

void Segments_set_discombobulation(int segment, int discombobulation)
{
    ASSERT_M3(segment < n_segments, (void)0);
    ASSERT_M3(segments[segment].segment_type == ST_MOVING, (void)0);
    ASSERT_M3(discombobulation >= 0, (void)0);
    segments[segment].moving.discombobulation = discombobulation;
}

int Segments_get_discombobulation(int segment)
{
    ASSERT_M3(segment < n_segments, 0);
    ASSERT_M3(segments[segment].segment_type == ST_MOVING, 0);
    return segments[segment].moving.discombobulation;
}

double Segments_get_collapse_progress(int segment)
{
    ASSERT_M3(segment < n_segments, 0.);
    ASSERT_M3(segments[segment].segment_type == ST_COLLAPSING, 0.);
    return segments[segment].collapsing.collapse_progress;
}

int Segments_get_hole_position(int segment)
{
    ASSERT_M3(segment < n_segments, 0);
    //double segment_position = Segments_get_position(segment);
    //double offset = segment_position - (int)floor(segment_position);
    int length = Segments_get_length(segment);
    //return length > 2 ? (int)(length * (1 - offset)) : length;
    return length;
}

//! \brief Split segment at position, create new collapsing segment, stop all segments before and including segment
//!     field: 0 1 2 3 4 5 6 7 8 9 . . . 
//!      leds:     O O o X X X O O   O = led, o = hole, X = collapse
//!       pos:     0 1 2 3 4 5 6 7
//!   segment:     ^     ^     ^
//!                |     |     position = 5
//!                |  collapse_length = 3
//!       start = 2, length = 7, shift 
//! 
//! After
//!     field: 0 1 2 3 4 5 6 7 8 9 . . . 
//!      leds:     O O . X X X O O
//!       pos:     0 1   0 1 2 0 1
//!   segment:     ^     ^     ^ new inserted (shift = 0)
//!                |     | 
//!          trimmed     collapsing
//! 
//! The hole can be in one of the three parts of the segment:
//!     - between start and collapse => shift both collapse and new segment to right, 
//!         recaulculate shift of the original segment, so that the hole stays in place
//!     - inside collapse => shift both collapse and new segment by one, trunc shift
//!         the old segment, the hole will jump (alternative would be to extend collapse
//!         length by one)
//!     - aftter collapse, in new segment => trunc shift of the old segment, calculate 
//!         shift of the new segment so that the hole stays in place
//! 
//! \param  segment new collapsing segment will appear after it
//! \param position new moving segment will start at position
//! \param led_discombobulation 
static void collapse_segment(const int segment, const int position, const int collapse_length)
{
    ASSERT_M3(!(position < collapse_length), (void)0);
    int hole_position = Segments_get_hole_position(segment);

    printf("Inserting new segment at position %i, hole %i, collapse %i at frame %i\n", position, hole_position, collapse_length, match3_game_source.cur_frame);
    Segments_print_info(segment);

    int n_inserts = 2;
    int segment_length = Segments_get_length(segment);
    int collapse_index = segment + 1;
    int new_moving_index = segment + 2;
    if (position == segment_length)     //collapse is at the end, there will be no new moving segment
    {
        n_inserts--;
        new_moving_index = 0;
    }
    if (position == collapse_length)    //collapse at the beginning, existing moving segment will be overwritten by the collapsing one
    {
        n_inserts--;
        collapse_index = segment;
        new_moving_index--;
    }
    int bullets_left = 0, bullets_collapsing = 0, bullets_right = 0;
    for (int i = 0; i < Segments_get_n_bullets(segment); ++i)
    {
        int pos = Segments_get_bullet(segment, i).segment_position;
        if (pos < position - collapse_length)
            bullets_left++;
        else if (pos < position)
            bullets_collapsing++;
        else
            bullets_right++;
    }

    n_segments += n_inserts;
    //printf("N segments increased to %i\n", n_segments);
    ASSERT_M3(n_segments <= N_MAX_SEGMENTS, (void)0); //TODO handle this situation
    for (int si = n_segments - 1; si > segment + n_inserts; --si)
    {
        segments[si] = segments[si - n_inserts];
        if(segments[si].segment_type == ST_MOVING)
            segments[si].moving.speed = 0;
    }

    //if the hole is to the left of us, we have to shift position by one
    int hole_left = (hole_position < position - collapse_length) ? 1 : 0;
    segment_t collapsing_segment = {
        .start = segments[segment].start + position - collapse_length,
        .length = collapse_length,
        .shift = floor(segments[segment].shift) + position - collapse_length + bullets_left + hole_left,
        .segment_type = ST_COLLAPSING,
        .collapsing.collapse_progress = 1.0,
        .debug = match3_game_source.cur_frame
    };
    if (collapse_index > segment) //the \p segment will be trimmed
    {
        segments[segment].length = position - collapse_length;
        segments[segment].moving.discombobulation = bullets_left;
        if (hole_left)
        {
            segments[segment].shift = floor(segments[segment].shift) + (double)(segments[segment].length - hole_position) / segments[segment].length;
        }
        else
        {
            segments[segment].shift = floor(segments[segment].shift);
        }
    }
    if (new_moving_index > 0) //new segment will be inserted to the right of the collapsing segment
    {
        double new_segment_shift = 0;
        if (hole_position == position) new_segment_shift = 1.;
        if (hole_position > position) new_segment_shift = (double)(segment_length - hole_position) / (segment_length - position);

        segment_t moving_segment = {
            .start = segments[segment].start + position,
            .length = segment_length - position,
            .shift = trunc(segments[segment].shift) + position + bullets_left + bullets_collapsing + hole_left + new_segment_shift,
            .segment_type = ST_MOVING,
            .moving.speed = 0,
            .moving.discombobulation = bullets_right,
            .debug = match3_game_source.cur_frame
        };
        segments[new_moving_index] = moving_segment;
    }
    segments[collapse_index] = collapsing_segment;
    if (new_moving_index > -1)
    {
        printf("New segment: ");
        Segments_print_info(new_moving_index);
        //double new_speed = calculate_segment_speed(new_moving_index);
        //if (new_speed < 0.) segments[new_moving_index].shift += 1.;
    }
    printf("Collapsing segment delta %f: ", segments[collapse_index].shift - segments[segment].shift - segments[segment].length);
    Segments_print_info(collapse_index);
}

//! @brief This happens when the whole segment collapses, it is then completely removed, or when two segments are merged
//! @param left_segment segment to delete
//! @param shift_length only necessary when shifting field before deleting segment, otherwise must be 0
static void delete_segment(const int left_segment, int shift_length)
{
    //ASSERT_M3(segments[left_segment].segment_type == ST_COLLAPSING, (void)0); -- this is only true when removing collapsed, not when merging
    for (int segment = left_segment; segment < n_segments - 1; ++segment)
    {
        segments[segment] = segments[segment + 1];
        segments[segment].start -= shift_length;
    }
    n_segments--;
    //printf("N segments decreasing to %i\n", n_segments);
}

static void merge_segments(const int left_segment, int right_segment)
{
    ASSERT_M3(left_segment < n_segments - 1, (void)0); //there must be at least one segment to the right
    ASSERT_M3(right_segment < n_segments, (void)0);
    ASSERT_M3(segments[left_segment].segment_type == ST_MOVING, (void)0);
    ASSERT_M3(segments[right_segment].segment_type == ST_MOVING, (void)0);
    ASSERT_M3(right_segment == Segments_get_next_moving(left_segment), (void)0);

    //the jewels in segment have to be continous, we might overwrite some collapsing segment, if that happens we will just delete it
    while (right_segment - left_segment > 1)
    {
        delete_segment(right_segment - 1, 0);
        right_segment--;
    }

    //we have to shift jewels in the field so that the dead and collapsing jewel in the intervening segment are overwritten
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
static int evaluate_field(const int segment, const int position)
{
    ASSERT_M3(segments[segment].segment_type == ST_MOVING, (void)0);
    jewel_type type = Segments_get_jewel_type(segment, position);
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
        return 0;
    }

    //we have a match, we have to split the segment and start collapse
    printf("Segment %i split at position %i, match length %i, jewel type %i\n", segment, pos_end, same_length, type);
    int effect = fmin(same_length - C_MATCH_3_LENGTH, 2);
    printf(ANSI_COLOR_CYAN "DING %i (match found)\n" ANSI_COLOR_RESET, effect);
    enum ESoundEffects effects[] = { SE_M3_JewelsDing01, SE_M3_JewelsDing02, SE_M3_JewelsDing03 };
    SoundPlayer_play(effects[effect]);
    collapse_segment(segment, pos_end, same_length);
    lastCollapseType = type;
    return 1;
}

//! @brief Inserts a new \p jewel at \p position in \p insert_segment. All jewels in the field at \p position and higher 
//! will be shifted right. All segments starts will be updated. In the end, call evaluate_field to see if there will be 
//! a collapse
//! @param position 
//! @param insert_segment 
//! @param jewel 
void Field_insert_and_evaluate(const int insert_segment, const int position, jewel_type jewel_type, int bullet_index)
{
    ASSERT_M3(insert_segment < n_segments, (void)0);
    ASSERT_M3(position < Segments_get_length(insert_segment), (void)0);
    ASSERT_M3(segments[insert_segment].segment_type == ST_MOVING, (void)0);

    printf("inserting into %i, pos %i\n", insert_segment, position);
    Segments_print_info(insert_segment);
    field_length += (field_length == C_MAX_FIELD_LENGTH - 1) ? 0 : 1; //if we are at the max field length, the last jewel will be simply overwritten
    int fi_insert = Segments_get_field_index(insert_segment, position);
    for (int fi = field_length - 1; fi > fi_insert; --fi)
    {
        field[fi] = field[fi - 1];
    }
    field[fi_insert] = make_jewel(jewel_type);
    segments[insert_segment].length++;
    for (int segment = insert_segment + 1; segment < n_segments; ++segment)
    {
        segments[segment].start += 1;
    }
    segments[insert_segment].moving.discombobulation--;
    int deleting = 0;
    for (int sbi = 0; sbi < segments[insert_segment].n_bullets; ++sbi)
    {
        if (segments[insert_segment].bullets[sbi].bullet_index == bullet_index)
        {
            deleting = 1;
            segments[insert_segment].n_bullets--;
        }
        if (deleting)
        {
            segments[insert_segment].bullets[sbi] = segments[insert_segment].bullets[sbi + 1];
        }
    }
    assert(deleting);
    //match3_announce("THUD (bullet into jewel)");
    SoundPlayer_play(SE_M3_BallImpact);
    evaluate_field(insert_segment, position);
    Segments_print_info(insert_segment);
}

static void swap_jewels(const int swap_segment, const int left_position, int* left_id, int* right_id)
{
    int left_index = Segments_get_field_index(swap_segment, left_position);
    int right_index = Segments_get_field_index(swap_segment, left_position + 1);
    *left_id = field[left_index].unique_id;
    *right_id = field[right_index].unique_id;
    jewel_t tmp = field[left_index];
    field[left_index] = field[right_index];
    field[right_index] = tmp;
}

static int swap_jewels_by_id(const int left_id, const int right_id)
{
    int left_index = -1;
    int right_index = -1;
    for (int i = 0; i < field_length; ++i)
    {
        if (field[i].unique_id == left_id) left_index = i;
        if (field[i].unique_id == right_id) right_index = i;
        if (left_index > -1 && right_index > -1) break;
    }
    if (left_index > -1 && right_index > -1)
    {
        if (Field_get_segment(left_index) != Field_get_segment(right_index))
            return 0; //the jewels are now in different segments, one of them si probably collapsing
        jewel_t tmp = field[left_index];
        field[left_index] = field[right_index];
        field[right_index] = tmp;
        return 1;
    }
    return 0; //one of the jewels was probably somehow destroyed in the meantime
}

int Field_swap_and_evaluate(const int swap_segment, const int left_position)
{
    ASSERT_M3(swap_segment < n_segments, (void)0);
    ASSERT_M3(left_position < segments[swap_segment].length - 1, (void)0);
    if (field[left_position].type == field[left_position + 1].type)
    {
        printf("Cannot pretend to swap jewels of the same type");
        return  -1;
    }

    int left_id, right_id;
    swap_jewels(swap_segment, left_position, &left_id, &right_id);
    if (evaluate_field(swap_segment, left_position + 1) || evaluate_field(swap_segment, left_position))
    {
        printf("Swap successful\n");
        return 0;
    }
    else
    {
        unswaps[n_unswaps].timeout = match3_config.unswap_timeout;
        unswaps[n_unswaps].left_jewel_id = left_id;
        unswaps[n_unswaps].right_jewel_id = right_id;
        n_unswaps++;
        return -1;
    }
}

static void set_segment_speed(int segment, double target_speed, double time_delta)
{
    ASSERT_M3(segments[segment].segment_type == ST_MOVING, (void)0);
    if (segments[segment].moving.speed == target_speed)
        return;
    double max_change = match3_config.max_accelaration * time_delta;
    //printf("Max change %f\n", max_change);
    segments[segment].moving.speed = (segments[segment].moving.speed < target_speed) ?
        fmin(target_speed, segments[segment].moving.speed + max_change) :
        fmax(target_speed, segments[segment].moving.speed - max_change);
    if (segments[segment].moving.speed == 0)
        segments[segment].shift = floor(segments[segment].shift);
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
    jewel_type left_type = Segments_get_last_jewel_type(left_segment);
    jewel_type right_type = Segments_get_jewel_type(segment, 0);
    if (left_type == right_type)
        return match3_config.retrograde_speed;
    return 0.;
}

static void unswap_update(double time_delta)
{
    for (int u = n_unswaps - 1; u >= 0; --u)
    {
        unswaps[u].timeout -= time_delta * 1000.;
        if (unswaps[u].timeout < 0)
        {
            swap_jewels_by_id(unswaps[u].left_jewel_id, unswaps[u].right_jewel_id);
            for (int un = u; un < n_unswaps - 1; ++un)
            {
                unswaps[un] = unswaps[un + 1];
            }
            n_unswaps--;
        }
    }
}


//! @brief Update speeds and positions of segments, merge segments that got close to each other
//! Rules for the speeds:
//!     -- if there is only one segment, it moves at normal speed
//!     -- otherwise, if two neighboring segments have the same jewel type at the matching ends, 
//!        the left segment moves at slow speed, and right segment at retrograde speed
//!     -- otherwise, the left segment moves at the slow speed and and the right segment stands
//! @returns 1 if collapse is in progress
int Segments_update(void)
{
    double time_delta = (double)(match3_game_source.basic_source.time_delta / 1000L) / 1e6;
    
    unswap_update(time_delta);

    int is_collapse = 0;
    //update collapsing segments
    for (int segment = n_segments - 1; segment >= 0; --segment)
    {
        if (segments[segment].segment_type != ST_COLLAPSING)
            continue;
        segments[segment].collapsing.collapse_progress -= time_delta / round_collapse_time;
        if (segments[segment].collapsing.collapse_progress < 0)
        {
            printf("Removing segment %i, length is %i\n", segment, segments[segment].length);
            delete_segment(segment, 0);
        }
        else
            is_collapse++;
    }

    //if collapse is in progress, we are done
    if (is_collapse > 0)
        return 1;

    if (n_segments == 0)
    {
        match3_game_source.level_phase = M3LP_LEVEL_WON;
        return 0;
    }

    //check speed of all segments and update as needed
    int last_segment_old_pos = floor(segments[0].shift);
    for (int segment = 0; segment < n_segments; ++segment)
    {
        if (segments[segment].segment_type != ST_MOVING)
            continue;
        double target_speed = calculate_segment_speed(segment) * speed_bias;
        set_segment_speed(segment, target_speed, time_delta);
        if (segments[segment].moving.speed == 0)
            continue;
        segments[segment].shift += segments[segment].moving.speed * time_delta;
    }
    if (last_segment_old_pos != floor(segments[0].shift))
    {
        SoundPlayer_play(SE_M3_Tick);
    }

    //finally, we can check for segments to merge and delete
    int segment = n_segments - 1;
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
        double visual_distance = floor(Segments_get_position(segment)) - floor(Segments_get_position(left_segment)) - left_length;
        double real_distance = Segments_get_position(segment) - Segments_get_position(left_segment) - left_length;
        //if the left segment is moving right, or right segment is moving left, we have to decrease the visual distance by one in each case
        //if (segments[segment].moving.speed < 0) visual_distance -= 1;
        if (segments[left_segment].moving.speed > 0) visual_distance -= 1;
        if (segments[left_segment].moving.speed > 0) real_distance -= 1;
        if (visual_distance < 2.)
        {
            //printf("Distance between %i and %i is %f (real) %f\n", left_segment, segment, visual_distance, real_distance);
        }
        ASSERT_M3_CONTINUE(!(visual_distance < 0));
        if (visual_distance < 1.)
        {
            printf("Merging segments: left %i, right %i\n", left_segment, segment);
            match3_announce("ball_impact", "Merging segments");
            int merge_position = Segments_get_length(left_segment) - 1;
            merge_segments(left_segment, segment);
            evaluate_field(left_segment, merge_position);
        }
        segment--;
    }

    if (n_segments == 0)
        match3_game_source.level_phase = M3LP_LEVEL_WON;
    return 0;
}

static jewel_t make_jewel(jewel_type type)
{
    static int unique_id = 0;
    double shift = random_01() * M_PI;
    jewel_t j = {
        .type = type,
        .unique_id = unique_id++,
        .sin_phase = sin(shift),
        .cos_phase = cos(shift)
    };
    return j;
}

static void init_jewel_colors(int n_gem_colours, int offset)
{
    const int lg = 2 * match3_config.n_half_grad + 1;
    const double w = 0.1;
    const double o = (1. - w) / 10.;
    hsl_t col;
    int angle = 360 / n_gem_colours;
    for (int a = 0; a < n_gem_colours; ++a)
    {
        col.h = (double)((a * angle + offset) % 360) / 360.;
        col.s = 1;
        for (int l = 0; l < lg; ++l)
        {
            col.l = o + w * (double)l / (double)(lg - 1);
            match3_game_source.jewel_colors[a * lg + l] = hsl2rgb(&col);
        }
        col.l = 0.5;
        match3_game_source.bullet_colors[a] = hsl2rgb(&col);
    }
}

static void make_initial_segment(double start_offset)
{
    segments[0].start = 0;
    segments[0].shift = start_offset;
    segments[0].length = field_length;
    segments[0].segment_type = ST_MOVING;
    segments[0].moving.speed = match3_config.normal_forward_speed;
    segments[0].moving.discombobulation = 0;
    segments[0].n_bullets = 0;
    n_segments = 1;
}

void Field_init_with_clue(const jewel_type field_def[], const int def_length)
{
    init_jewel_colors(6, 0);
    speed_bias = 1;
    round_collapse_time = match3_config.clue_collapse_time;
    field_length = def_length;
    for (int fi = 0; fi < field_length; ++fi)
    {
        field[fi] = make_jewel(N_GEM_COLORS - 1 - field_def[fi]);
    }
    make_initial_segment(1.);
}

void Field_init(match3_LevelDefinition_t level_definition)
{
    assert(level_definition.n_gem_colours <= N_GEM_COLORS);
    assert(level_definition.field_length <= C_MAX_FIELD_LENGTH);
    round_collapse_time = match3_config.collapse_time;
    init_jewel_colors(level_definition.n_gem_colours, 30);
    field_length = level_definition.field_length;
    speed_bias = level_definition.speed_bias;
    double same_gem_bias = level_definition.same_gem_bias;
    jewel_type last_type = (jewel_type)(random_01() * (double)level_definition.n_gem_colours);
    for (int i = field_length - 1; i >= 0; --i)
    {
        if (random_01() > same_gem_bias)
        {
            last_type = (jewel_type)(random_01() * (double)level_definition.n_gem_colours);
            same_gem_bias *= 0.9;
        }
        field[i] = make_jewel(last_type);
    }
    make_initial_segment(level_definition.start_offset);
}

jewel_type Field_get_last_match(void)
{
    return lastCollapseType;
}

void Field_destruct(void)
{
    //free(field);
}
