#ifndef __PLAYER_OBJECT_H__
#define __PLAYER_OBJECT_H__

extern const int C_PLAYER_OBJ_INDEX;;

void PlayerObject_init();
int PlayerObject_get_health();
int PlayerObject_get_length();
void PlayerObject_take_hit(int i);

#endif /* __PLAYER_OBJECT_H__ */
