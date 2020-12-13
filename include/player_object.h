#ifndef __PLAYER_OBJECT_H__
#define __PLAYER_OBJECT_H__

extern const int C_PLAYER_OBJ_INDEX;;

void PlayerObject_init(enum GameModes current_mode);
int PlayerObject_get_health();
void PlayerObject_take_hit(int i);

#endif /* __PLAYER_OBJECT_H__ */
