#ifndef __M3_BULLETS_H__
#define __M3_BULLETS_H__

int Match3_Emitor_get_length(void);
int Match3_Emitor_fire(void);
int Match3_Emitor_reload(int dir);
jewel_type Match3_Emitor_get_jewel_type(void);

int Match3_Bullets_get_n(void);
unsigned char Match3_Bullets_is_live(int bullet_index);
double Match3_Bullets_get_position(int bullet_index);
jewel_type Match3_Bullets_get_jewel_type(int bullet_index);
void Match3_Bullets_set_segment_info(int bullet_index, int segment_info);
int Match3_Bullets_get_segment_info(int bullet_index);
void Match3_Bullets_delete(int bullet_index);
void Match3_Bullets_update(void);

#endif /* __M3_BULLETS_H__ */
