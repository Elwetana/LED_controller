#ifndef __M3_PLAYERS_H__
#define __M3_PLAYERS_H__

enum EM3_BUTTONS
{
    M3B_A,
    M3B_B,
    M3B_X,
    M3B_Y,
    M3B_DUP,
    M3B_DRIGHT,
    M3B_DDOWN,
    M3B_DLEFT,
    M3B_N_BUTTONS
};

const int Match3_Player_get_position(int player_index);
const int Match3_Player_is_moving(int player_index);

void Match3_Player_move(int player, signed char direction);
void Match3_Player_press_button(int player, enum EM3_BUTTONS button);
void Match3_Players_init();

#endif /* __M3_PLAYERS_H__ */