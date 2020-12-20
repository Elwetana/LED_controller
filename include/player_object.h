#ifndef __PLAYER_OBJECT_H__
#define __PLAYER_OBJECT_H__

extern const int C_PLAYER_OBJ_INDEX;;

void PlayerObject_init(enum GameModes current_mode);
void PlayerObject_update();
int PlayerObject_get_health();
int PlayerObject_is_hit(int bullet);
void PlayerObject_take_hit(int i);
void PlayerObject_hide_above();
void PlayerObject_hide_below();
void PlayerObject_cloak();
void PlayerObject_move_left();
void PlayerObject_move_right();
void PlayerObject_fire_bullet_red();
void PlayerObject_fire_bullet_green();
void PlayerObject_fire_bullet_blue();


#endif /* __PLAYER_OBJECT_H__ */
