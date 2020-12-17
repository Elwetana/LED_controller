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


const int C_BKGRND_OBJ_INDEX = 0;
const int C_OBJECT_OBJ_INDEX = 32; //ships and asteroids
const int C_PROJCT_OBJ_INDEX = 128; //projectiles

char win_messages[GM_PLAYER_LOST][16] = {
    "     ",
    "~   ~",
    "  ~  ",
    "~~~~~",
    "     ",
    " ~~  "
    "",
    "Boss defeated"
};


typedef struct GameObject
{
    enum StencilFlags stencil_flag;
    int health;
    int deleted;
    int mark;           //!< bit array: 0: used by stencil; 1,2,3: colors RGB; 
    uint64_t time;
} game_object_t;

static game_object_t game_objects[MAX_N_OBJECTS];
static enum GameModes current_mode = GM_LEVEL_BOSS;


int GameObject_new_projectile_index()
{
    int i = C_PROJCT_OBJ_INDEX;
    while (i < MAX_N_OBJECTS && !game_objects[i].deleted)
    {
        i++;
    }
    if (i >= MAX_N_OBJECTS)
    {
        printf("Failed to create projectile\n");
        return -1;
    }
    return i;
}


static int GameObject_spawn_enemy_projectile(int color_index)
{
    int i = GameObject_new_projectile_index();
    if (i == -1) return -1;
    MovingObject_init_stopped(i, 2, MO_FORWARD, 1, 2);
    PulseObject_init_steady(i, color_index, 1);
    MovingObject_init_movement(i, config.enemy_speed, game_source.basic_source.n_leds - 3, GameObject_delete_object);
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
    GameObject_spawn_enemy_projectile(config.color_index_W);
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
        MovingObject_init_stopped(0, cur_position + 1, MO_FORWARD, cur_length - 2, 9);
        printf("shrinking stargate\n");
    }
}

static void update_objects_level1()
{
    if (roll_dice_poisson(config.enemy_spawn_chance))
    {
        int bullet = GameObject_spawn_enemy_projectile(config.color_index_G);
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
        int bullet = GameObject_spawn_enemy_projectile(color_index);
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
        int bullet = GameObject_spawn_enemy_projectile(color_index);
        assert(bullet >= 0);
        //level 0 = C = 4 + 8; 1 = M = 2 + 8; 2 = Y = 2 + 4
        game_objects[bullet].mark = 14 ^ (2 << level);
    }
    PlayerObject_update();
    update_stargate(0.20);
}

static void update_objects_level_boss()
{
    //TODO -- move boss, change tactics, change weapons and so on...
    if(roll_dice_poisson(3 * config.enemy_spawn_chance))
    {
        int level = (int)(random_01() * 3); //0, 1 or 2 with the same probability
        int color_index = (int[]){ config.color_index_R, config.color_index_G, config.color_index_B }[level];
        int bullet = GameObject_spawn_enemy_projectile(color_index);
        assert(bullet >= 0);
        game_objects[bullet].mark = 2 << level; //bit 0 is used by stencil; this is really not a very good system
    }
    PlayerObject_update();
}

static void GameObject_update_objects()
{
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
    case GM_PLAYER_LOST:
        break;
    }
}

void GameObject_delete_object(int gi)
{
    game_objects[gi].deleted = 1;
}

void GameObject_init(int gi, int health, int stencil_flag)
{
    game_objects[gi].deleted = 0;
    game_objects[gi].health = health;
    game_objects[gi].stencil_flag = stencil_flag;
    game_objects[gi].mark = 0;
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

//******** GAME STATE FUNCTIONS ********

void OnArrival_stargate_decoration(int i)
{
    int sg_start = MovingObject_get_position(0);
    int sg_width = MovingObject_get_length(0);
    int dec_length = MovingObject_get_length(i);
    if (i == 1 || i == 2)
    {
        MovingObject_init_stopped(i, sg_start, MO_FORWARD, dec_length, 8);
        MovingObject_init_movement(i, config.decoration_speed, sg_start + sg_width / 2 - dec_length, OnArrival_stargate_decoration);
    }
    else
    {
        MovingObject_init_stopped(i, sg_start + sg_width, MO_BACKWARD, dec_length, 8);
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
    MovingObject_init_stopped(0, stargate_start, MO_FORWARD, stargate_width, 9);
    PulseObject_init_steady(0, config.color_index_stargate, stargate_width);

    //decorations
    int dec_length = 2;
    //1 -> 0, 2 -> 1, 3 -> 3, 4 -> 4
    for (int dec = 1; dec < 5; ++dec)
    {
        int dir = (dec < 3) ? +1 : -1;
        GameObject_init(dec, 1, SF_Background);
        MovingObject_init_stopped(dec, stargate_start + (dec - dir - (dir < 0)) * stargate_width / 4,
            dir, dec_length, 8);
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
    GameObject_init(C_OBJECT_OBJ_INDEX, 5, SF_Enemy);
    MovingObject_init_stopped(C_OBJECT_OBJ_INDEX, 10, MO_FORWARD, 6, 2);
    PulseObject_init_steady(C_OBJECT_OBJ_INDEX, config.color_index_player, 6);
}

static void game_over_init()
{
    int l = game_source.basic_source.n_leds - 10;
    GameObject_init(0, 0, SF_Background);
    MovingObject_init_stopped(0, 5, MO_FORWARD, l, 0);
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

static void show_victory_message(char* message)
{
    int msg_len = strlen(message);
    assert(9 * msg_len < MAX_OBJECT_LENGTH);

    GameObject_init(0, 1, SF_Background);
    game_objects[0].time = game_source.basic_source.current_time;
    MovingObject_init_stopped(0, 1, MO_FORWARD, 9 * msg_len + 1, 0);
    MovingObject_init_movement(0, 0.1, game_source.basic_source.n_leds - 9 * msg_len - 2, OnArrival_victory_message);
    MovingObject_set_render_mode(0, 2);
    PulseObject_init(0, 1, PM_REPEAT, 0, 1000, 0, 0, 0.5, NULL);
    PulseObject_set_color(0, config.color_index_R, config.color_index_R, config.color_index_R, 0);
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
        current_mode++;
        printf("Player won level %i\n", current_mode);
        GameObjects_init(); //TODO here and in all functions that change current_mode -- it would be better to set a flag that mode was changed and call GameObjects_init() in the game object update 
    }
}

void GameObject_boss_hit(int i)
{
    assert(i == C_OBJECT_OBJ_INDEX);
    game_objects[C_OBJECT_OBJ_INDEX].health--;
    if (!game_objects[C_OBJECT_OBJ_INDEX].health)
    {
        printf("Boss defeated\n");
        current_mode++;
        GameObjects_init();
    }
}


void GameObject_debug_win()
{
    current_mode++;
    printf("Player cheated to win level %i\n", current_mode);
    GameObjects_init();
}

void GameObjects_set_mode_player_lost(int i)
{
    assert(i == C_PLAYER_OBJ_INDEX);
    current_mode = GM_PLAYER_LOST;
    GameObjects_init();
    printf("player lost\n");
}


void GameObjects_next_level()
{
    //there is a timeout after winning previous level during which we can proceed
    const uint64_t timeout = 2 * 1e9;
    if (game_source.basic_source.current_time - game_objects[0].time < timeout) return;
    printf("advancing to next level\n");
    current_mode++;
    GameObjects_init();
}

/*! The sequence of actions during one loop:
*   - process inputs - this may include timers?
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
