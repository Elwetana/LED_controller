#ifndef __M3_BULLETS_H__
#define __M3_BULLETS_H__

const int Match3_Emitor_get_length();
const int Match3_Emitor_fire();
const int Match3_Emitor_reload(int dir);
const jewel_type Match3_Emitor_get_jewel_type();

const int Match3_Bullets_get_n();
const double Match3_Bullets_get_position(int bullet_index);
const jewel_type Match3_Bullets_get_jewel_type(int bullet_index);
void Match3_Bullets_delete(int bullet_index);
void Match3_Bullets_update();

#endif /* __M3_BULLETS_H__ */