#define _CRT_SECURE_NO_WARNINGS

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <math.h>
#include <assert.h>
#ifdef __linux__
#include "ws2811.h"
#include <unistd.h>
#include <linux/input.h>
#include <fcntl.h>
#include <errno.h>
#else
#include "fakeled.h"
#endif // __linux__

#include "colours.h"
#include "common_source.h"
#include "source_manager.h"
#include "sound_player.h"
#include "controller.h"
#include "base64.h"

#include "paint_source.h"
#include "morse_source.h"

enum EAnimMode 
{
    AM_NONE,
    AM_MOVE_NO_AA,
    AM_MOVE_AA,
    AM_SHIMMER,
    AM_MOVE_SHIMMER
};

typedef struct SAmpPhase
{
    float amp;
    float phase;
} AmpPhase_t;

static enum EAnimMode animation_mode = AM_NONE;
static double animation_speed;
static uint64_t animation_start;
static hsl_t* leds;
static int* canvas;
static AmpPhase_t* coeffs;
static hsl_t* key_frames[C_N_KEY_FRAMES];
static int frame_intervals[C_N_KEY_FRAMES]; //intervals between frames in miliseconds; frame_intervals[0] is time between frame 0 and 1
static int next_frame[C_N_KEY_FRAMES];
static int head_frame_index;
static int current_frame_index;
static uint64_t frame_start;
static const int KF_EMPTY_FRAME = -2;
static const int KF_LAST_FRAME = -1;

static const char* secret = "STASTNYADOBRYNOVYROKDIKYZEJSTETUSNAMIMARTINAVILMA";
static const char* hint = "TMOUDVACETCTYRIPOMUCKA";
static int hint_length;
static struct MorseChar hint_mc[24]; //this must be increased if hint is longer
static const double resonance_frequence = 70.0; //BPM
static const double resonance_decay = 0.9; //how quickly resonance fades when no match is found


enum EPaintCodeColours
{
    PAINT_CODE_R,
    PAINT_CODE_Y,
    PAINT_CODE_G,
    PAINT_CODE_B,
    N_PAINT_CODES
};
static const float PAINT_RYGB_HUE[] = {0.0f / 360.0f, 50.0f / 360.0f, 120.0f / 360.0f, 240.0f / 360.0f, 1.1f};

/*
* Encodes letter to RGBY code as per https://www.tmou.cz/24/page/cheatsheet
*/
void Paint_letter2rygb(char* rygb, const char c)
{
    //order is R, Y, G, B
    int order[] = { PAINT_CODE_R, PAINT_CODE_Y, PAINT_CODE_G, PAINT_CODE_B };
    int n = (int)c - 65; //n is 0 to 25
    assert(n >= 0);
    assert(n < 26);
    if (n > 15) n--; //removes Q
    if (n > 20) n--; //removes W
    int o = n / 6; // 0-5 -> 0, 6-11 -> 1, etc.
    rygb[0] = order[o];
    for (int i = o; i < 3; i++) order[i] = order[i + 1];
    o = (n % 6) / 2;  // (n % 6) -> order in second column; 0-5: 0-1 -> 0, 2-3 -> 1, 4-5 -> 2
    rygb[1] = order[o];
    for (int i = o; i < 3; i++) order[i] = order[i + 1];
    o = (n % 6) % 2; //order in the third column
    rygb[2] = order[o];
    rygb[3] = order[(o + 1) % 2];
}

static void show_secret()
{
    int n = (int)strlen(secret);
    char rygb[4];
    assert(n * 4 < paint_source.basic_source.n_leds);
    for (int letter = 0; letter < n; letter++)
    {
        Paint_letter2rygb(rygb, secret[letter]);
        for (int led = 0; led < N_PAINT_CODES; led++)
        {
            leds[letter * N_PAINT_CODES + led].h = PAINT_RYGB_HUE[(int)rygb[led]];
            leds[letter * N_PAINT_CODES + led].s = 1.0f;
            leds[letter * N_PAINT_CODES + led].l = 0.4f;
        }
    }
    animation_mode = AM_NONE;
}

/* dist will be <0, 0.5> */
static enum EPaintCodeColours get_colour_distance(hsl_t* col, float* dist)
{
    int iFrom = 0;
    //for (iFrom = 0; iFrom < N_PAINT_CODES; iFrom++)
    while (col->h >= PAINT_RYGB_HUE[iFrom]) iFrom++;
    iFrom--;
    //printf("iFrom %i, col.h %f hue from %f\n", iFrom, col->h, PAINT_RYGB_HUE[iFrom]);
    //assert(col->h > PAINT_RYGB_HUE[iFrom]);
    assert(iFrom < N_PAINT_CODES);
    assert(iFrom >= 0);
    int iTo = (iFrom + 1) % N_PAINT_CODES;
    float interval = PAINT_RYGB_HUE[iTo] - PAINT_RYGB_HUE[iFrom];
    if (interval < 0) // this blue -> red
        interval += 1;
    assert(interval >= 0);
    *dist = (col->h - PAINT_RYGB_HUE[iFrom]) / interval;
    if(*dist > 1) printf("d %f, iFrom %i, iTo %i col.h %f int %f\n", *dist, iFrom, iTo, col->h, interval);
    assert(*dist <= 1);
    assert(*dist >= 0);
    int closest = *dist < 0.5f ? iFrom : iTo;
    if (*dist > 0.5)
        *dist = 1 - *dist;
    assert(*dist >= 0);
    return (enum EPaintCodeColours)(closest);
}

static double add_resonance(hsl_t* col, int led_index, double time_seconds, double strength)
{
    int led = paint_source.basic_source.n_leds - 1 - led_index;
    unsigned int letter = led / N_PAINT_CODES;
    //printf("letter %i\n", letter);
    if (letter >= strlen(secret))
        return 0;
    //if saturation or lightness are under certain thresholds, we cannot resonante
    if (col->s < 0.1 || col->l < 0.1)
    {
        //printf("no sl h %f, s %f, l %f\n", col->h, col->s, col->l);
        return strength * resonance_decay;
    }

    int rygb_index = led % N_PAINT_CODES;
    char rygb[4];
    Paint_letter2rygb(rygb, secret[letter]);
    float dist;
    enum EPaintCodeColours led_closest = get_colour_distance(col, &dist);
    //printf("lc %i %i\n", (int)led_closest, rygb_index);
    if (led_closest != rygb[rygb_index]) //there is no match in hue -> colour is unchanged, resonance fades
    {
        return strength * resonance_decay;
    }
    //printf("d %f\n", strength);

    double amp = strength * 2 * (0.5 - dist);
    
    if (led < hint_length) 
    {
        double dit_length = 60.0 / resonance_frequence / 2.0; //resonance frequency for dits is too slow, we shall make it twice the speed
        struct MorseChar* mc = &hint_mc[led];
        double char_duration = mc->len * dit_length;
        int q = (int)(time_seconds / char_duration);
        double dit_position = (time_seconds - q * char_duration) / dit_length;
        int dit_index = (int)dit_position;
        double dit_frac = dit_position - dit_index;
        assert(dit_index < mc->len);
        int dit_index_from, dit_index_to;
        if (dit_frac >= 0.5)
        {
            dit_index_from = dit_index;
            dit_index_to = (dit_index + 1) % mc->len;
            dit_frac -= 0.5;
        }
        else
        {
            dit_index_to = dit_index;
            dit_index_from = (dit_index - 1 + mc->len) % mc->len;
            dit_frac += 0.5;
        }
        if (mc->data[dit_index_from] > 0 && mc->data[dit_index_to] > 0)
            amp *= +1;
        else if (mc->data[dit_index_from] == 0 && mc->data[dit_index_to] == 0)
            amp *= -1;
        else if (mc->data[dit_index_from] == 0 && mc->data[dit_index_to] > 0) //
            amp *= -cos(M_PI * dit_frac);
        else
            amp *= +cos(M_PI * dit_frac);
        //if (led == 0) printf("from %i, to %i, frac %f, amp %f\n", dit_index_from, dit_index_to, dit_frac, amp);
    }
    else
    {
        amp *= sin(2 * M_PI * time_seconds * resonance_frequence / 60.0); //amp = <-1, 1>
    }
    
    if (amp < 0)
        col->l *= (1 + amp); //in lowest phase, amp == -1, then col->l = 0
    else
        col->l = col->l + (1 - col->l) * amp; //in highest phase, amp == 1, then col->l = 1; for amp == 0, col->l is unchanged
    return strength;
}

static void generate_led_phases(float max_amp)
{
    if(max_amp < 0.0f)
        max_amp *= -1.0f;
    if(max_amp > 1.0f)
        max_amp = 1.0f;
    for (int led = 0; led < paint_source.basic_source.n_leds; led++)
    {
        coeffs[led].amp = max_amp * random_01() * 0.5;
        coeffs[led].phase = random_01() * M_PI;
    }
}

static void switch_animation_mode(enum EAnimMode new_mode, double new_speed)
{
    if (animation_mode != new_mode)
    {
        animation_start = paint_source.basic_source.current_time;
    }

    switch (new_mode)
    {
    case AM_NONE:
    case AM_MOVE_NO_AA:
    case AM_MOVE_AA:
        break;
    case AM_SHIMMER:
    case AM_MOVE_SHIMMER:
        generate_led_phases((float)new_speed / 10.0f);
        break;
    default:
        break;
    }
    animation_mode = new_mode;
    animation_speed = new_speed;
}

static void update_leds_from_keyframes()
{
    int time_ms = ((paint_source.basic_source.current_time - frame_start) / (long)1e6);
    while (time_ms > frame_intervals[current_frame_index])
    {
        time_ms -= frame_intervals[current_frame_index];
        current_frame_index = next_frame[current_frame_index];
        if (current_frame_index == KF_LAST_FRAME)
        {
            current_frame_index = head_frame_index;
        }
        frame_start = paint_source.basic_source.current_time - time_ms * 1000l;
    }
    int next_frame_index = next_frame[current_frame_index];
    if (next_frame_index == KF_LAST_FRAME) next_frame_index = head_frame_index;
    double blend = (double)time_ms / (double)frame_intervals[current_frame_index];
    for (int led = 0; led < paint_source.basic_source.n_leds; led++)
    {
        lerp_hsl(&key_frames[current_frame_index][led], &key_frames[next_frame_index][led], blend, &leds[led]);
    }
}


static void draw_leds_to_canvas()
{
    double time_seconds = ((paint_source.basic_source.current_time - animation_start) / (long)1e3) / (double)1e6;
    double distance = animation_speed * time_seconds;
    if (current_frame_index != KF_LAST_FRAME)
    {
        update_leds_from_keyframes();
    }
    double resonance_strength = 1.0;
    for (int led = paint_source.basic_source.n_leds - 1; led >= 0; led--)
    {
        int index_before = (led + (int)floor(distance)) % paint_source.basic_source.n_leds;
        // -10 % 7 = -3
        if (index_before < 0)
            index_before += paint_source.basic_source.n_leds;
        int index_after = (index_before + 1) % paint_source.basic_source.n_leds;

        assert(index_before >= 0 && index_before < paint_source.basic_source.n_leds);
        assert(index_after >= 0 && index_after < paint_source.basic_source.n_leds);
        
        hsl_t col;
        switch (animation_mode)
        {
        case AM_NONE:
            col = leds[led];
            if(resonance_strength > 0.001)
            {
                //printf("res %f\n", resonance_strength);
                resonance_strength = add_resonance(&col, led, time_seconds, resonance_strength);
            }
            break;
        case AM_MOVE_NO_AA:
            col = leds[index_before];
            break;
        case AM_MOVE_AA:
            lerp_hsl(&leds[index_before], &leds[index_after], distance - floor(distance), &col);
            break;
        case AM_SHIMMER:
            col = leds[led];
            col.l = fmax(fmin(col.l + coeffs[led].amp * sin(coeffs[led].phase + distance), 1), 0);
            break;
        case AM_MOVE_SHIMMER:
            lerp_hsl(&leds[index_before], &leds[index_after], distance - floor(distance), &col);
            col.l = fmax(fmin(col.l + coeffs[led].amp * sin(coeffs[led].phase + distance), 1), 0);
            break;
        default:
            break;
        }
        canvas[led] = hsl2rgb(&col);
    }
}

int PaintSource_update_leds(int frame, ws2811_t* ledstrip)
{
    paint_source.cur_frame = frame;
    draw_leds_to_canvas();
    for (int led = 0; led < paint_source.basic_source.n_leds; led++)
    {
        ledstrip->channel[0].leds[led] = canvas[led];
    }
    return 1;
}

//! @brief Take base64 encoded RGB values of LEDs and decode them to the array of HSL colours
//! @param payload base64 encoded RGB values
//! @param target allocated buffer of HSL colours
static void decode_led_state(char* payload, hsl_t* target)
{
    unsigned char decoded[MAX_MSG_LENGTH];
    int bytes_decoded = Base64decode(decoded, payload);
    assert(bytes_decoded == 3 * paint_source.basic_source.n_leds);
    for (int led = 0; led < paint_source.basic_source.n_leds; led++)
    {
        int rgb = decoded[3 * led] << 16 | decoded[3 * led + 1] << 8 | decoded[3 * led + 2];
        rgb2hsl(rgb, &target[paint_source.basic_source.n_leds - led - 1]);
    }
}

/* Frame linked list manipulations */

//! @brief Initialize next_frame array to KF_EMPTY_FRAME and head_frame_index to KF_LAST_FRAME
static void init_frames() 
{
    for (int i = 0; i < C_N_KEY_FRAMES; i++) 
    {
        next_frame[i] = KF_EMPTY_FRAME;
    }
    head_frame_index = KF_LAST_FRAME;
    current_frame_index = KF_LAST_FRAME;
}

static void start_key_frame_animation()
{
    current_frame_index = head_frame_index;
    frame_start = paint_source.basic_source.current_time;
}

static void stop_key_frame_animation()
{
    current_frame_index = KF_LAST_FRAME;
}

//! @brief Utitlity function to find an empty key frame
//! @return index into key_frames where next_frame == KF_EMPTY_FRAME, -1 if no such frame exists
static int get_empty_frame_index()
{
    int new_index = -1;
    // Find an empty slot in key_frames
    for (int i = 0; i < C_N_KEY_FRAMES; i++)
    {
        if (next_frame[i] == KF_EMPTY_FRAME)
        {
            new_index = i;
            break;
        }
    }
    return new_index;
}

//! @brief Utility function for getting index to key_frames and next_frame on given position in linked list
//! @param position in the linked list, 0 based
//! @return index into key_frames that's position-th in the linked list
static int get_index_of_position(int position)
{
    int current = head_frame_index;
    int cur_position = 0;
    while (next_frame[current] != KF_LAST_FRAME && cur_position++ < position) current = next_frame[current];
    if (next_frame[current] == KF_LAST_FRAME && cur_position != position) return -1;
    return current;
}

//! @brief Push a new frame to the end of the list
//! @param encoded_state base64 encoded RGB values
static void push_frame(char* encoded_state) 
{
    int new_index = get_empty_frame_index();
    if (new_index == -1) 
        return; // No space left

    // Decode the encoded state into the new frame
    decode_led_state(encoded_state, key_frames[new_index]);
    frame_intervals[new_index] = 100;

    // Find the end of the list and add the new frame
    if (head_frame_index == KF_LAST_FRAME)
    {
        head_frame_index = new_index;
    }
    else 
    {
        int current = head_frame_index;
        while (next_frame[current] != KF_LAST_FRAME) current = next_frame[current];
        next_frame[current] = new_index;
    }
    next_frame[new_index] = KF_LAST_FRAME;
}

//! @brief Remove a frame from the list by index
//! @param index Position in the linked list that is to be removed
static void remove_frame(int index) 
{
    if (index < 0 || head_frame_index == KF_LAST_FRAME) 
        return; // Invalid index or empty list

    // Special case: Removing the head of the list
    if (index == 0) 
    {
        int to_remove = head_frame_index;
        head_frame_index = next_frame[to_remove];
        next_frame[to_remove] = KF_EMPTY_FRAME; // Mark as empty
        return;
    }

    // Traverse the list to find the node just before the one to remove
    int current = get_index_of_position(index - 1);
    if (current < 0) return;

    // Remove the target node if it exists
    int to_remove = next_frame[current];
    if (to_remove != KF_LAST_FRAME) 
    {
        next_frame[current] = next_frame[to_remove];
        next_frame[to_remove] = KF_EMPTY_FRAME; // Mark as empty
    }
}

//! @brief Insert a frame at a specific position
//! @param index Position in the linked list
//! @param encoded_state base64 encoded RGB values
static void insert_frame(int index, char* encoded_state)
{
    if (index < 0 || index >= C_N_KEY_FRAMES)
        return;

    int current = 0;
    // Traverse the linked list to reach the desired position
    if (index > 0)
    {
        current = get_index_of_position(index - 1);
        if (current < 0) //we are trying to insert outside list length
            return;
    }

    // Find an empty slot in key_frames for the new frame
    int new_index = get_empty_frame_index();
    if (new_index == -1) return; // No space left

    // Decode the encoded state into the new frame at new_index
    decode_led_state(encoded_state, key_frames[new_index]);

    // Special case: Inserting at the beginning of the list
    if (index == 0) 
    {
        next_frame[new_index] = head_frame_index;
        head_frame_index = new_index;
        return;
    }

    // Insert the new frame into the list at the desired position
    next_frame[new_index] = next_frame[current];
    next_frame[current] = new_index;
}

//! @brief Swap two frames, see also https://www.mycompiler.io/view/5YCknkvWCEr
//! @param index1 first position, 0 based
//! @param index2 second position, 0 based
static void swap_frames(int index1, int index2)
{
    if (index1 < 0 || index1 >= C_N_KEY_FRAMES) return;
    if (index2 < 0 || index2 >= C_N_KEY_FRAMES) return;
    if (index1 == index2) return;
    if (index1 > index2)
    {
        int i = index2;
        index2 = index1;
        index1 = i;
    }
    int kf_index2 = get_index_of_position(index2);
    int kf_index2_prev = get_index_of_position(index2 - 1);
    if (kf_index2 < 0 || kf_index2_prev < 0) return;
    if (index1 == 0)
    {
        int tmp = next_frame[kf_index2];
        next_frame[kf_index2_prev] = head_frame_index;
        next_frame[kf_index2] = next_frame[head_frame_index];
        next_frame[head_frame_index] = tmp;
        head_frame_index = kf_index2;
    }
    else
    {
        int kf_index1 = get_index_of_position(index1);
        int kf_index1_prev = get_index_of_position(index1 - 1);
        if (kf_index1 < 0 || kf_index1_prev < 0) return;

        next_frame[kf_index1_prev] = kf_index2;
        next_frame[kf_index2_prev] = kf_index1;
        int tmp = next_frame[kf_index2];
        next_frame[kf_index2] = next_frame[kf_index1];
        next_frame[kf_index1] = tmp;
    }
}

static void update_frame(int index, char* encoded_state)
{
    if (index < 0 || index > C_N_KEY_FRAMES) return;
    int kf_index = get_index_of_position(index);
    if (kf_index < 0) return;
    decode_led_state(encoded_state, key_frames[kf_index]);
}

static void update_timing(int index, int timing)
{
    if (index < 0 || index > C_N_KEY_FRAMES) return;
    int kf_index = get_index_of_position(index);
    if (kf_index < 0) return;
    frame_intervals[kf_index] = timing;
}



//! @brief Process messages from HTTP server
//! All messages have to have format <command>?<parameter>. Available commands are:
//! 
//!     set?<base64 encoded RGB values>
//!     anim?<mode>=<speed>
//!     add?<base64 encoded RGB values>
//!     del?<int position in linked list>
//!     update?<pos>&<base 64 encoded>
//!     time?<pos>&<int timing>
//!     swap?<pos1>&<pos2>
//! @param msg 
void PaintSource_process_message(const char* msg)
{
    char* sep = strchr(msg, '?');
    if (sep == NULL)
    {
        printf("PaintSource: message does not contain target %s\n", msg);
        return;
    }
    if ((sep - msg) >= MAX_CMD_LENGTH)
    {
        printf("PaintSource: target is too long or poorly formatted: %s\n", msg);
        return;
    }
    if ((strlen(sep + 1) >= MAX_MSG_LENGTH))
    {
        printf("PaintSource: message too long or poorly formatted: %s\n", msg);
        return;
    }
    char target[MAX_CMD_LENGTH];
    char payload[MAX_MSG_LENGTH];
    strncpy(target, msg, sep - msg);
    strncpy(payload, sep + 1, MAX_MSG_LENGTH);
    target[sep - msg] = 0x0;
    payload[MAX_MSG_LENGTH - 1] = 0x0;
    if (!strncasecmp(target, "set", 3))
    {
        stop_key_frame_animation();
        decode_led_state(payload, leds);
        return;
    }
    if (!strncasecmp(target, "anim", 4))
    {
        int new_mode;
        double new_speed;
        int n = sscanf(payload, "%d=%lf", &new_mode, &new_speed);
        if (n != 2)
        {
            printf("Trying to set animation, but format is invalid %s\n", payload);
            return;
        }
        if (new_mode > (int)AM_MOVE_SHIMMER || new_mode < 0)
        {
            printf("Invalid number for new mode: %s\n", payload);
            return;
        }
        switch_animation_mode((enum EAnimMode)new_mode, new_speed);
        return;
    }
    if (!strncasecmp(target, "add", 3))
    {
        push_frame(payload);
        start_key_frame_animation();
        return;
    }
    if (!strncasecmp(target, "del", 3))
    {
        int index;
        int n = sscanf(payload, "%i", &index);
        if (n != 1)
        {
            printf("Invalid index in delete keyframe message\n");
            return;
        }
        remove_frame(index);
        start_key_frame_animation();
        return;
    }
    if (!strncasecmp(target, "update", 6))
    {
        char encoded[MAX_MSG_LENGTH];
        int index;
        int n = sscanf(payload, "%i&%s", &index, encoded);
        if (n != 2)
        {
            printf("Keyframe update message invalid format\n");
            return;
        }
        update_frame(index, encoded);
        start_key_frame_animation();
        return;
    }
    if (!strncasecmp(target, "time", 4))
    {
        int index;
        int timing;
        int n = sscanf(payload, "%i&%i", &index, &timing);
        if (n != 2)
        {
            printf("Keyframe timing message invalid format\n");
            return;
        }
        update_timing(index, timing);
        start_key_frame_animation();
        return;
    }
    if (!strncasecmp(target, "swap", 4))
    {
        int index1;
        int index2;
        int n = sscanf(payload, "%i&%i", &index1, &index2);
        if (n != 2)
        {
            printf("Keyframe swap message invalid format\n");
            return;
        }
        swap_frames(index1, index2);
        start_key_frame_animation();
        return;
    }
    if (!strncasecmp(target, "secret", 6))
    {
        stop_key_frame_animation();
        show_secret();
        return;
    }
    printf("PaintSource: Unknown command: %s, parameter was: %s\n", target, payload);
}

static void hint_mc_init()
{
    hint_length = strlen(hint);
    char buff[5];
    for (int i = 0; i < hint_length; ++i)
    {
        MorseSource_get_code(buff, hint[i]);
        MorseSource_make_morse_char(&hint_mc[i], buff, 1);
    }
}

void PaintSource_init(int n_leds, int time_speed, uint64_t current_time)
{
    BasicSource_init(&paint_source.basic_source, n_leds, time_speed, source_config.colors[PAINT_SOURCE], current_time);
    leds = malloc(n_leds * sizeof(hsl_t));
    for (int led = 0; led < n_leds; led++)
    {
        rgb2hsl(0, &leds[led]);
    }
    for (int frame = 0; frame < C_N_KEY_FRAMES; frame++)
    {
        key_frames[frame] = malloc(n_leds * sizeof(hsl_t));
        for (int led = 0; led < n_leds; led++)
        {
            rgb2hsl(0, &key_frames[frame][led]);
        }
    }
    canvas = malloc(n_leds * sizeof(int));
    coeffs = malloc(n_leds * sizeof(AmpPhase_t));
    paint_source.start_time = current_time;
    animation_start = current_time;
    hint_mc_init();
    init_frames();

    //switch_animation_mode(AM_MOVE_SHIMMER, 1.1);
    //show_secret();
}

void PaintSource_destruct(void)
{
    free(leds);
    for (int frame = 0; frame < C_N_KEY_FRAMES; frame++) free(key_frames[frame]);
    free(canvas);
    free(coeffs);
}

void PaintSource_construct(void)
{
    BasicSource_construct(&paint_source.basic_source);
    paint_source.basic_source.update = PaintSource_update_leds;
    paint_source.basic_source.init = PaintSource_init;
    paint_source.basic_source.destruct = PaintSource_destruct;
    paint_source.basic_source.process_message = PaintSource_process_message;
}

paint_PaintSource_t paint_source = {
    .basic_source.construct = PaintSource_construct
};


