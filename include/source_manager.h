#ifndef __SOURCE_MANAGER_H__
#define __SOURCE_MANAGER_H__

#include "common_source.h"

enum SourceType string_to_SourceType(char*);
void SourceConfig_add_color(char* source_name, SourceColors* source_colors);
void SourceConfig_destruct();
void SourceColors_destruct(SourceColors* source_colors);

void SourceManager_init(enum SourceType source_type, int led_count, int time_speed);
void set_source(enum SourceType source_type);
int (*SourceManager_update_leds)(int, ws2811_t*);
void (*SourceManager_destruct_source)();
void (*SourceManager_process_message)(const char*);
void SourceManager_set_time(uint64_t time_ns, uint64_t delta_ns);
void check_message();


#endif /* __SOURCE_MANAGER_H__ */

