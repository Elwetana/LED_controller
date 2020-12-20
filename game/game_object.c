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

#include "common_source.h"
#include "colours.h"
#include "game_source.h"
#include "game_object.h"
#include "moving_object.h"
#include "pulse_object.h"
#include "player_object.h"
#include "input_handler.h"
#include "stencil_handler.h"
#include "callbacks.h"


const int C_BKGRND_OBJ_INDEX = 8;
const int C_OBJECT_OBJ_INDEX = 32; //ships and asteroids
const int C_PROJCT_OBJ_INDEX = 128; //projectiles

char win_messages[GM_PLAYER_LOST][16] = {
    "Alpha", //level 1
    "Beta", //level 1 won
    "Gama", //level 2
    "Delta", //level 2 won
    "Epsilon", //level 3
    "Zeta", //level 3 won
    "Eta",      //level boss
    "Theta",      //level boss defeated
    "Iota"  //level boss won
};


typedef struct GameObject
{
    enum StencilFlags stencil_flag;
    int health;
    int deleted;
    int mark;           //!< bit array: 0: used by stencil; 1,2,3: colors RGB; 
    uint64_t time;
} game_object_t;

static struct
{
    uint64_t last_special_attack;
    int attack_ready; 
    uint64_t last_turn_around;
    uint64_t last_confetti;
    uint64_t confetti_thrown;
} boss;

static game_object_t game_objects[MAX_N_OBJECTS];
static enum GameModes current_mode = GM_LEVEL1;
static enum GameModes next_mode = GM_LEVEL1; //<! flag to be set when the mode is changed in the next update
static enum GameModes prev_mode = GM_LEVEL1;

static int get_free_index(int min, int max, char* error_message)
{
    int i = min;
    while (i < max && !game_objects[i].deleted)
    {
        i++;
    }
    if (i >= max)
    {
        printf(error_message);
        return -1;
    }
    return i;
}

int GameObject_new_projectile_index()
{
    return get_free_index(C_PROJCT_OBJ_INDEX, MAX_N_OBJECTS, "Failed to find a free projectile index\n");
}

int GameObject_new_background_index()
{
    return get_free_index(C_BKGRND_OBJ_INDEX, C_OBJECT_OBJ_INDEX, "Failed to find a free background index\n");
}


static int GameObject_spawn_enemy_projectile(int color_index, double pos, enum MovingObjectFacing direction, int delete_or_wrap)
{
    int i = GameObject_new_projectile_index();
    if (i == -1) return -1;
    int target = direction == MO_FORWARD ? game_source.basic_source.n_leds - 2 : 1;
    MovingObject_init_stopped(i, pos, MO_FORWARD, 1, ZI_Projectile);
    PulseObject_init_steady(i, color_index, 1);
    MovingObject_init_movement(i, config.enemy_speed, target, (delete_or_wrap ? GameObject_delete_object : OnArrival_wrap_around));
    GameObject_init(i, 1, SF_EnemyProjectile);
    return i;
}

/*!
 * @brief Either one of the bullet wins, or both are destroyed
 * Rules of rock-paper-scissors: B > R, R > G, G > B
 * @return  0: no collision, 1: bullet 1 wins, 2: bullet 2 wins, 3: both are destroyed
*/
int GameObject_resolve_projectile_collision(int bullet1, int bullet2)
{
    if ((game_objects[bullet1].stencil_flag) == (game_objects[bullet2].stencil_flag)) //either two player's bullets or two enemy bullets
        return 0;
    if ((game_objects[bullet1].mark & 14) == (game_objects[bullet2].mark & 14))  //they have the same colour
        return 3;
    if (game_objects[bullet1].mark & 2) //R
        return (game_objects[bullet2].mark & 4) == 4 ? 1 : 2;
    if (game_objects[bullet1].mark & 4) //G
        return (game_objects[bullet2].mark & 8) == 8 ? 1 : 2;
    if (game_objects[bullet1].mark & 8) //B
        return (game_objects[bullet2].mark & 2) == 2 ? 1 : 2;
    printf("B1: %i, B2: %i\n", game_objects[bullet1].mark, game_objects[bullet2].mark);
    assert(0);
    return -1; //this should never happen
}

void GameObject_debug_projectile()
{
    GameObject_spawn_enemy_projectile(config.color_index_W, 2, MO_FORWARD, 1);
}

/*!
 * @brief Determine if event with rate `r` happened or not
 * see https://en.wikipedia.org/wiki/Poisson_distribution
 * We calculate the probablity of 0 events happening, i.e. k = 0. This probability is exp(-r * t).
 * We then get uniform roll from <0,1) and if it is higher than the calculated probability, the event will happen
 * @param r average number of events that happen during 1 second, i.e time rate
 * @return  1 when event happened, 0 otherwise
 */
static int roll_dice_poisson(double r)
{
    double time_seconds = game_source.basic_source.time_delta / (double)1e9;
    double prob = exp(-r * time_seconds);
    return (random_01() > prob);
}

static void update_stargate(double stargate_shrink_chance)
{
    if (roll_dice_poisson(stargate_shrink_chance))
    {
        double cur_position = MovingObject_get_position(0);
        double cur_length = MovingObject_get_length(0);
        if (cur_length - config.player_ship_size - 4 < 0)
        {
            printf("Stargate is too small\n");
            GameObjects_set_mode_player_lost(C_PLAYER_OBJ_INDEX);
            return;
        }
        MovingObject_init_stopped(0, cur_position + 1, MO_FORWARD, cur_length - 2, ZI_Background_far);
        printf("shrinking stargate\n");
    }
}

static void update_objects_level1()
{
    if (roll_dice_poisson(config.enemy_spawn_chance))
    {
        double stargate_centre = MovingObject_get_position(0) + MovingObject_get_length(0) / 2;
        int bullet = GameObject_spawn_enemy_projectile(config.color_index_G, stargate_centre, MO_FORWARD, 1);
        assert(bullet >= 0);
        game_objects[bullet].mark = 4;
    }
    PlayerObject_update();
    update_stargate(0.1);  //one shrink on average every ten seconds
}

static void update_objects_level2()
{
    if (roll_dice_poisson(1.5 * config.enemy_spawn_chance))
    {
        int level = (int)(random_01() * 3); //0, 1 or 2 with the same probability
        int color_index = (int[]){ config.color_index_R, config.color_index_G, config.color_index_B }[level];
        double stargate_centre = MovingObject_get_position(0) + MovingObject_get_length(0) / 2;
        int bullet = GameObject_spawn_enemy_projectile(color_index, stargate_centre, MO_FORWARD, 1);
        assert(bullet >= 0);
        game_objects[bullet].mark = 2 << level; //bit 0 is used by stencil; this is really not a very good system
    }
    PlayerObject_update();
    update_stargate(0.15);
}

static void update_objects_level3()
{
    if (roll_dice_poisson(3 * config.enemy_spawn_chance))
    {
        int level = (int)(random_01() * 3); //0, 1 or 2 with the same probability
        int color_index = (int[]){ config.color_index_C, config.color_index_M, config.color_index_Y }[level];
        double stargate_centre = MovingObject_get_position(0) + MovingObject_get_length(0) / 2;
        int bullet = GameObject_spawn_enemy_projectile(color_index, stargate_centre, MO_FORWARD, 1);
        assert(bullet >= 0);
        //level 0 = C = 4 + 8; 1 = M = 2 + 8; 2 = Y = 2 + 4
        game_objects[bullet].mark = 14 ^ (2 << level);
    }
    PlayerObject_update();
    update_stargate(0.20);
}

static void GameObjects_ready_special_attack(int i)
{
    assert(i == C_OBJECT_OBJ_INDEX);
    boss.attack_ready = 1;
}

static void boss_special_attack_bullet(int i)
{
    game_objects[i].stencil_flag = SF_EnemyProjectile;
}

static void boss_special_attack()
{
    int attack = GameObject_new_projectile_index();
    if (attack == -1)
        return;
    GameObject_init(attack, 1, SF_Background);
    GameObject_mark(attack, 14);
    MovingObject_init_stopped(attack, 0, MO_FORWARD, game_source.basic_source.n_leds - 1, ZI_Projectile);
    PulseObject_init(attack, 1, PM_FADE, 8, 150, 0, 0, 1, boss_special_attack_bullet);
    PulseObject_set_color_all(attack, config.color_index_K, config.color_index_W, config.color_index_K, game_source.basic_source.n_leds - 1);
    boss.attack_ready = 0;
    printf("special attack %i\n", attack);
}


static void update_objects_level_boss()
{
    double pos = MovingObject_get_position(C_OBJECT_OBJ_INDEX);
    enum MovingObjectFacing f = MovingObject_get_facing(C_OBJECT_OBJ_INDEX);
    int health = GameObject_get_health(C_OBJECT_OBJ_INDEX);
    assert(health > 0);

    int low_health_rage = 1 + config.boss_health / health;
    if (low_health_rage > 5) low_health_rage = 5;

    if(roll_dice_poisson(low_health_rage * config.enemy_spawn_chance))
    {
        int level = (int)(random_01() * 3); //0, 1 or 2 with the same probability
        int color_index = (int[]){ config.color_index_R, config.color_index_G, config.color_index_B }[level];
        double boss_end_pos = pos + (f == MO_FORWARD) * MovingObject_get_length(C_OBJECT_OBJ_INDEX);
        int bullet = GameObject_spawn_enemy_projectile(color_index, boss_end_pos, f, (pos < config.wraparound_fire_pos));
        if (bullet != -1)
        {
            game_objects[bullet].mark = 2 << level;
        }
    }
    if (MovingObject_get_speed(C_OBJECT_OBJ_INDEX) == 0 && roll_dice_poisson(2))
    {
        double target = (pos < config.wraparound_fire_pos) ? pos + 10 : pos + (random_01() - 0.5) * 6;
        MovingObject_init_movement(C_OBJECT_OBJ_INDEX, config.player_ship_speed, (int)target, MovingObject_stop);
    }

    const uint64_t turn_around_cooldown = 5 * 1e9;
    if (pos > config.wraparound_fire_pos && pos < game_source.basic_source.n_leds - config.wraparound_fire_pos &&
        game_source.basic_source.current_time - boss.last_turn_around > turn_around_cooldown) //there is a chance that boss will turn around
    {
        //printf("Considering boss turn around. Current facing %i ", f);
        if (roll_dice_poisson(0.5))
        {
            MovingObject_set_facing(C_OBJECT_OBJ_INDEX, -1 * f);
            boss.last_turn_around = game_source.basic_source.current_time;
            f = MovingObject_get_facing(C_OBJECT_OBJ_INDEX);
            //printf("- succeeded, now f=%i\n", f);
        }
        else
        {
            //printf("- not\n");
        }
    }

    const uint64_t special_attack_cooldown = 10 * 1e9;
    if (health < config.boss_health / 4 && game_source.basic_source.current_time - boss.last_special_attack > special_attack_cooldown) //there is a chance for boss special attack
    {
        if (roll_dice_poisson(1))
        {
            boss.last_special_attack = game_source.basic_source.current_time;
            PulseObject_init(C_OBJECT_OBJ_INDEX, 1, PM_ONCE, 1, 500, 0, 2 * M_PI / (health / 2), 1, GameObjects_ready_special_attack);
            PulseObject_set_color_all(C_OBJECT_OBJ_INDEX, config.color_index_player, config.color_index_K, config.color_index_player, health / 2);
        }
    }
    if (boss.attack_ready)
    {
        boss_special_attack();
    }
    PlayerObject_update();
}

void GameObject_debug_boss_special()
{
    int health = GameObject_get_health(C_OBJECT_OBJ_INDEX);
    boss.last_special_attack = game_source.basic_source.current_time;
    PulseObject_init(C_OBJECT_OBJ_INDEX, 1, PM_ONCE, 1, 500, 0, 2 * M_PI / (health / 2), 1, GameObjects_ready_special_attack);
    PulseObject_set_color_all(C_OBJECT_OBJ_INDEX, config.color_index_player, config.color_index_K, config.color_index_player, health / 2);
}

void update_boss_defeat()
{
    const uint64_t confetti_cooldown = 500 * 1e6;
    if (game_source.basic_source.current_time - boss.last_confetti < confetti_cooldown)
    {
        return;
    }
    int ii[2];
    ii[0] = GameObject_new_projectile_index();
    if(ii[0] == -1)
    {
        printf("Not enough confetti\n");
        return;
    }
    GameObject_init(ii[0], 1, SF_Background);
    ii[1] = GameObject_new_projectile_index();
    if (ii[1] == -1)
    {
        printf("Not enough confetti\n");
        return;
    }
    GameObject_init(ii[1], 1, SF_Background);
    int conf_len = 3;
    for(int conf = 0; conf < 2; ++conf)
    {
        int i = ii[conf];
        MovingObject_init_stopped(i, 100, 2 * conf - 1, conf_len, ZI_Background_near);
        MovingObject_init_movement(i, 6 * config.player_ship_speed, conf * (199 - conf_len), GameObject_delete_object);
        PulseObject_init_steady(i, config.color_index_K, conf_len);
        PulseObject_set_color(i, 0, 0, config.color_index_R, 1);
        PulseObject_set_color(i, 0, 0, config.color_index_boss_head, 2);
    }
    boss.confetti_thrown++;
    boss.last_confetti = game_source.basic_source.current_time;
    if(boss.confetti_thrown > 40)
    {
        next_mode = GM_LEVEL_BOSS_WON;
    }
}

static void GameObject_update_objects()
{
    if (next_mode != current_mode)
    {
        printf("Changing mode from %i to %i\n", current_mode, next_mode);
        prev_mode = current_mode;
        current_mode = next_mode;
        GameObjects_init();
    }
    switch (current_mode)
    {
    case GM_LEVEL1:
        update_objects_level1();
        break;
    case GM_LEVEL2:
        update_objects_level2();
        break;
    case GM_LEVEL3:
        update_objects_level3();
        break;
    case GM_LEVEL_BOSS:
        update_objects_level_boss();
        break;
    case GM_LEVEL1_WON:
    case GM_LEVEL2_WON:
    case GM_LEVEL3_WON:
    case GM_LEVEL_BOSS_WON:
        break;
    case GM_LEVEL_BOSS_DEFEATED:
        update_boss_defeat();
        break;
    case GM_PLAYER_LOST:
        break;
    }
}

void GameObject_delete_object(int gi)
{
    game_objects[gi].deleted = 1;
}

int GameObject_is_deleted(int gi)
{
    return game_objects[gi].deleted;
}

void GameObject_init(int gi, int health, int stencil_flag)
{
    game_objects[gi].deleted = 0;
    game_objects[gi].health = health;
    game_objects[gi].stencil_flag = stencil_flag;
    game_objects[gi].mark = 0;
    game_objects[gi].time = game_source.basic_source.current_time;
}

int GameObject_take_hit(int gi)
{
    return --game_objects[gi].health;
}

int GameObject_heal(int gi)
{
    return ++game_objects[gi].health;
}

int GameObject_get_health(int gi)
{
    return game_objects[gi].health;
}

void GameObject_mark(int gi, int mark)
{
    game_objects[gi].mark |= mark;
}

void GameObject_clear_mark(int gi, int mark)
{
    game_objects[gi].mark &= (0xFFFFFF - mark);
}

int GameObject_get_mark(int gi)
{
    return game_objects[gi].mark;
}

uint64_t GameObject_get_time(int gi)
{
    return game_objects[gi].time;
}

enum StencilFlags GameObject_get_stencil_flag(int gi)
{
    return game_objects[gi].stencil_flag;
}

//******** GAME STATE FUNCTIONS ********

void OnArrival_stargate_decoration(int i)
{
    int sg_start = MovingObject_get_position(0);
    int sg_width = MovingObject_get_length(0);
    int dec_length = MovingObject_get_length(i);
    if (i == 1 || i == 2)
    {
        MovingObject_init_stopped(i, sg_start, MO_FORWARD, dec_length, ZI_Background_mid);
        MovingObject_init_movement(i, config.decoration_speed, sg_start + sg_width / 2 - dec_length, OnArrival_stargate_decoration);
    }
    else
    {
        MovingObject_init_stopped(i, sg_start + sg_width, MO_BACKWARD, dec_length, ZI_Background_mid);
        MovingObject_init_movement(i, config.decoration_speed, sg_start + sg_width / 2, OnArrival_stargate_decoration);
    }
}

static void stargate_init()
/* Stargate is one long dark blue object that is used for collision detection with player 
* on it we have number of lighter objects running toward the center. Over time, stargate 
* grows narrower */
{
    int stargate_width = 40;
    int stargate_start = 5;
    GameObject_init(0, 1, SF_Enemy);
    MovingObject_init_stopped(0, stargate_start, MO_FORWARD, stargate_width, ZI_Background_far);
    PulseObject_init_steady(0, config.color_index_stargate, stargate_width);

    //decorations
    int dec_length = 2;
    //1 -> 0, 2 -> 1, 3 -> 3, 4 -> 4
    for (int dec = 1; dec < 5; ++dec)
    {
        int dir = (dec < 3) ? +1 : -1;
        GameObject_init(dec, 1, SF_Background);
        MovingObject_init_stopped(dec, stargate_start + (dec - dir - (dir < 0)) * stargate_width / 4,
            dir, dec_length, ZI_Background_mid);
        MovingObject_init_movement(dec, config.decoration_speed, stargate_start + stargate_width / 2 - (dir > 0) * dec_length, OnArrival_stargate_decoration);
        PulseObject_init_steady(dec, 0, dec_length);
        for (int led = 0; led < dec_length; ++led)
        {
            PulseObject_set_color(dec, config.color_index_stargate + led + 2, config.color_index_stargate + led + 2, config.color_index_stargate + led, led);
            //PulseObject_set_color(dec, config.color_index_G, config.color_index_G, config.color_index_G, led);
        }
    }
}

static void boss_init()
{
    //init boss
    GameObject_init(C_OBJECT_OBJ_INDEX, config.boss_health, SF_Enemy);
    MovingObject_init_stopped(C_OBJECT_OBJ_INDEX, 10, MO_FORWARD, config.boss_health / 2 + 1, ZI_Ship);
    PulseObject_init_steady(C_OBJECT_OBJ_INDEX, config.color_index_player, config.boss_health / 2 + 1);
    PulseObject_set_color(C_OBJECT_OBJ_INDEX, config.color_index_boss_head, config.color_index_boss_head, config.color_index_boss_head, config.boss_health / 2);

    //init decorations that mark area where shooting backwards is possible
    int decor_len = 3;
    GameObject_init(0, 1, SF_Background);
    MovingObject_init_stopped(0, config.wraparound_fire_pos, MO_FORWARD, decor_len, ZI_Background_far);
    PulseObject_init_steady(0, config.color_index_stargate, decor_len);

    GameObject_init(1, 1, SF_Background);
    MovingObject_init_stopped(0 + 1, game_source.basic_source.n_leds - config.wraparound_fire_pos - decor_len, MO_BACKWARD, decor_len, ZI_Background_far);
    PulseObject_init_steady(0 + 1, config.color_index_stargate, decor_len);
}

static void game_over_init()
{
    int l = game_source.basic_source.n_leds - 10;
    GameObject_init(0, 0, SF_Background);
    MovingObject_init_stopped(0, 5, MO_FORWARD, l, ZI_always_visible);
    PulseObject_init(0, 1, PM_REPEAT, 2, 5000, 0, 3 * M_PI / l, 10., NULL);
    PulseObject_set_color_all(0, config.color_index_game_over, config.color_index_K, 0, l);
}

void OnArrival_victory_message(int i)
{
    assert(i == 0);
    int pos = MovingObject_get_position(0);
    int len = MovingObject_get_length(0);
    if (pos > 2)
    {
        MovingObject_init_movement(0, 0.1, 1, OnArrival_victory_message);
    }
    else
    {
        MovingObject_init_movement(0, 0.1, game_source.basic_source.n_leds - len - 1, OnArrival_victory_message);
    }
}

void OnArrival_wrap_around(int i)
{
    int len = MovingObject_get_length(i);
    double pos = MovingObject_get_position(i);
    double new_pos = -1;
    if (pos < 2)
    {
        new_pos = game_source.basic_source.n_leds - len - 2;
    }
    if (pos > game_source.basic_source.n_leds - len - 2)
    {
        new_pos = 1;
    }
    assert(new_pos != -1);
    MovingObject_set_position(i, new_pos);
}

static void show_victory_message(char* message)
{
    int msg_len = strlen(message);
    assert(9 * msg_len < MAX_OBJECT_LENGTH);

    GameObject_init(0, 1, SF_Background);
    MovingObject_init_stopped(0, 1, MO_FORWARD, 9 * msg_len + 1, ZI_always_visible);
    MovingObject_init_movement(0, 0.1, game_source.basic_source.n_leds - 9 * msg_len - 2, OnArrival_victory_message);
    MovingObject_set_render_mode(0, 2);
    PulseObject_init(0, 1, PM_REPEAT, 0, 1000, 0, 0, 0.5, NULL);
    PulseObject_set_color(0, config.color_index_R, config.color_index_R, config.color_index_R, ZI_always_visible);
    for (int i = 0; i < msg_len; i++)
    {
        char c = message[i];
        for (int bit = 0; bit < 8; ++bit)
        {
            int color1 = (c & (1 << (7 - bit))) ? config.color_index_player : config.color_index_K;
            PulseObject_set_color(0, config.color_index_K, color1, color1, 1 + 9 * i + bit);
        }
        PulseObject_set_color(0, config.color_index_R, config.color_index_R, config.color_index_R, 1 + 9 * i + 8);
    }
}

static void GameObjects_init_objects()
{
    switch (current_mode)
    {
    case GM_LEVEL1:
    case GM_LEVEL2:
    case GM_LEVEL3:
        //spawn stargate
        stargate_init();
        break;
    case GM_LEVEL_BOSS:
        boss_init();
        break;
    case GM_LEVEL1_WON:
    case GM_LEVEL2_WON:
    case GM_LEVEL3_WON:
    case GM_LEVEL_BOSS_WON:
        //spawn victory message
        show_victory_message(win_messages[current_mode]);
        break;
    case GM_LEVEL_BOSS_DEFEATED:
        boss.confetti_thrown = 0;
        boss.last_confetti = 0;
        break;
    case GM_PLAYER_LOST:
        //spawn game over
        game_over_init();
        break;
    }
}

void GameObjects_init()
{
    for (int i = 0; i < MAX_N_OBJECTS; ++i)
        game_objects[i].deleted = 1;

    PlayerObject_init(current_mode);
    InputHandler_init(current_mode);
    Stencil_init(current_mode);
    GameObjects_init_objects();
}

enum GameModes GameObjects_get_current_mode()
{
    return current_mode;
}

void GameObjects_player_reached_gate()
{
    int sg_start = MovingObject_get_position(0);
    int sg_length = MovingObject_get_length(0);
    int player_start = MovingObject_get_position(C_PLAYER_OBJ_INDEX);
    int player_length = MovingObject_get_length(C_PLAYER_OBJ_INDEX);
    if (sg_start + sg_length > player_start + player_length)
    {
        next_mode = current_mode + 1;
        printf("Player won level %i\n", current_mode);
    }
}

static void GameObjects_shrink_boss(int i)
{
    assert(i == C_OBJECT_OBJ_INDEX);
    int health = GameObject_get_health(C_OBJECT_OBJ_INDEX);
    MovingObject_init_stopped(C_OBJECT_OBJ_INDEX, 10, MO_FORWARD, health / 2 + 1, ZI_Ship);
    PulseObject_init_steady(C_OBJECT_OBJ_INDEX, config.color_index_player, health / 2 + 1);
    PulseObject_set_color(C_OBJECT_OBJ_INDEX, config.color_index_boss_head, config.color_index_boss_head, config.color_index_boss_head, health / 2);
}

void GameObjects_boss_hit(int i)
{
    assert(i == C_OBJECT_OBJ_INDEX);
    game_objects[C_OBJECT_OBJ_INDEX].health--;
    if (!game_objects[C_OBJECT_OBJ_INDEX].health)
    {
        next_mode = current_mode + 1;
        printf("Boss defeated\n");
    }
    int len = MovingObject_get_length(C_OBJECT_OBJ_INDEX);
    PulseObject_init(C_OBJECT_OBJ_INDEX, 1, PM_ONCE, 2, 300, 0, 0, 1, GameObjects_shrink_boss);
    PulseObject_set_color_all(C_OBJECT_OBJ_INDEX, config.color_index_player, config.color_index_R, config.color_index_player, len);
}

void GameObject_debug_win()
{
    next_mode = current_mode + 1;
    printf("Player cheated to win level %i\n", current_mode);
}

void GameObjects_set_mode_player_lost(int i)
{
    assert(i == C_PLAYER_OBJ_INDEX);
    next_mode = GM_PLAYER_LOST;
    printf("player lost\n");
}

void GameObjects_next_level()
{
    //there is a timeout after winning previous level during which we can proceed
    const uint64_t timeout = 2 * 1e9;
    if (game_source.basic_source.current_time - game_objects[0].time < timeout) return;
    if (current_mode != GM_PLAYER_LOST)
    {
        next_mode = current_mode + 1;
        printf("advancing to next level\n");
    }
}

void GameObjects_set_level_by_message(char* message)
{
    for (int i = 0; i < (int)GM_PLAYER_LOST; ++i)
    {
        if (_stricmp(message, win_messages[i]) == 0)
        {
            printf("setting mode %i\n", i);
            next_mode = i;
            break;
        }
    }
}

void GameObjects_restart_lost_level()
{
    next_mode = prev_mode;
}

/*! The sequence of actions during one loop:
*   - process inputs - this may include timers?
*   - update objects, spawn new ones
*   - calculate movement -- from this moment no new object can be spawned
*   - process collisions using stencil buffer -- this may also trigger events
*   - process colors for blinking objects
*   - move and render all objects
*
* \param frame      current frame - not used
* \param ledstrip   pointer to rendering device
* \returns          1 when render is required, i.e. always
*/
int GameObjects_update_leds(int frame, ws2811_t* ledstrip)
{
#ifdef GAME_DEBUG
    //printf("Frame: %i\n", frame);
    (void)frame;
#else
    (void)frame;
#endif // GAME_DEBUG

    //unit_tests();
    Canvas_clear(ledstrip->channel[0].leds);
    InputHandler_process_input();
    GameObject_update_objects();
    //calculate movement, check collisions, adjust movement, start effects
    for (int gi = 0; gi < MAX_N_OBJECTS; ++gi)
    {
        if (game_objects[gi].deleted)
        {
            continue;
        }
        MovingObject_calculate_move_results(gi);
        Stencil_stencil_test(gi, game_objects[gi].stencil_flag);
    }
    //apply movement results, render scene
    for (int gi = 0; gi < MAX_N_OBJECTS; ++gi)
    {
        if (game_objects[gi].deleted)
        {
            continue;
        }
        PulseObject_update(gi);
        MovingObject_render(gi, ledstrip->channel[0].leds);
        MovingObject_update(gi);
    }
    return 1;
}
