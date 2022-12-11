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
#include "m3_game.h"
#include "m3_bullets.h"

//! Jewels fired by the players
typedef struct TBullet {
    jewel_type jewel_type;
    double position; //!< relative to field
    double speed;    //!< could be positive or negative, in leds/second
    int segment_info;
    unsigned char marked_for_delete;
} bullet_t;
//! array of bullets
bullet_t bullets[N_MAX_BULLETS];
//! number of bullets
n_bullets = 0;

//! Emitor fires the bullets
//! there is only one emitor, so we don't need typedef for this struct
struct {
    jewel_type jewel_type;
    double last_fire;
    const int length;
} emitor = {
    .last_fire = 0,
    .jewel_type = 0,
    .length = 3
};


/******************* Emitor *****************************/

const int Match3_Emitor_get_length()
{
    return emitor.length;
}

const int Match3_Emitor_fire()
{
    double t = miliseconds_from_start();
    if (t - emitor.last_fire < match3_config.emitor_cooldown)
        return 1;
    emitor.last_fire = t;

    ASSERT_M3(emitor.jewel_type < N_GEM_COLORS, (void)0);
    bullets[n_bullets].jewel_type = emitor.jewel_type;
    bullets[n_bullets].speed = -match3_config.bullet_speed; // +random_01();
    bullets[n_bullets].position = match3_game_source.basic_source.n_leds - 1 - emitor.length;
    bullets[n_bullets].segment_info = 0;
    //bullets[n_bullets].last_frame = 0;
    n_bullets++;
    return 0;
}

const int Match3_Emitor_reload(int dir)
{
    emitor.jewel_type = (emitor.jewel_type + dir) % N_GEM_COLORS;
    return 1;
}

const jewel_type Match3_Emitor_get_jewel_type()
{
    return emitor.jewel_type;
}

/******************** Bullets *****************************/

const double Match3_Bullets_get_position(int bullet_index)
{
    ASSERT_M3(bullet_index < n_bullets, 0);
    ASSERT_M3(!bullets[bullet_index].marked_for_delete, 0);
    return bullets[bullet_index].position;
}

const jewel_type Match3_Bullets_get_jewel_type(int bullet_index)
{
    ASSERT_M3(bullet_index < n_bullets, 0);
    ASSERT_M3(!bullets[bullet_index].marked_for_delete, 0);
    return bullets[bullet_index].jewel_type;
}

unsigned char Match3_Bullets_is_live(int bullet_index)
{
    ASSERT_M3(bullet_index < n_bullets, 0);
    return !bullets[bullet_index].marked_for_delete;
}

const int Match3_Bullets_get_n()
{
    return n_bullets;
}

static void delete_bullet(int bullet_index)
{
    for (int b = bullet_index; b < n_bullets - 1; ++b)
    {
        bullets[b] = bullets[b + 1];
    }
    n_bullets--;
}

void Match3_Bullets_delete(int bullet_index)
{
    //printf("Before: "); for (int b = 0; b < n_bullets; ++b) printf("%i ", bullets[b].jewel.type); printf("\n");
    //printf("deleting bullet %i of type %i, n_bullets %i\n", bullet, bullets[bullet].jewel.type, n_bullets);
    ASSERT_M3(bullet_index < n_bullets, (void)0);
    bullets[bullet_index].marked_for_delete = 1;
    //printf("After: "); for (int b = 0; b < n_bullets; ++b) printf("%i ", bullets[b].jewel.type); printf("\n");
}

void Match3_Bullets_set_segment_info(int bullet_index, int segment_info)
{
    ASSERT_M3(bullet_index < n_bullets, (void)0);
    bullets[bullet_index].segment_info = segment_info;
}

const int Match3_Bullets_get_segment_info(int bullet_index)
{
    ASSERT_M3(bullet_index < n_bullets, (void)0);
    return bullets[bullet_index].segment_info;
}

void Match3_Bullets_update()
{
    double time_delta = (double)(match3_game_source.basic_source.time_delta / 1000L) / 1e6;
    int remove_bullet[N_MAX_BULLETS] = { 0 };
    for (int bullet = 0; bullet < n_bullets; ++bullet)
    {
        if (bullets[bullet].marked_for_delete)
        {
            remove_bullet[bullet] = 1;
            continue;
        }
        bullets[bullet].position += bullets[bullet].speed * time_delta;
        if (bullets[bullet].position > match3_game_source.basic_source.n_leds - 1 || (int)bullets[bullet].position < 0)
        {
            remove_bullet[bullet] = 1;
        }
        if ((int)bullets[bullet].position < 0 && bullets[bullet].segment_info > 0)
        {
            int segment, pos;
            Match3_get_segment_and_position(bullets[bullet].segment_info, &segment, &pos);
            Segments_add_shift(segment, 1);
        }
    }
    //remove bullets that are over limit
    for (int bullet = n_bullets - 1; bullet >= 0; --bullet)
    {
        if (!remove_bullet[bullet])
            continue;
        delete_bullet(bullet);
        //TODO -- update segment shift if inside segment, otherwise discombobulation is lost
    }
}
