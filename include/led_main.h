#ifndef __LED_MAIN_SOURCE_H__
#define __LED_MAIN_SOURCE_H__ 


//#define PRINT_FPS
// defaults for cmdline options
#define TARGET_FREQ             WS2811_TARGET_FREQ
#define GPIO_PIN                12
#define DMA                     10
#define STRIP_TYPE              WS2811_STRIP_RGB

#define LED_COUNT               100

#define FPS_SAMPLES             50
#define FRAME_TIME              20000

struct ArgOptions
{
    int clear_on_exit;
    int time_speed;
    uint64_t frame_time;
    enum SourceType source_type;
};

#endif /* __LED_MAIN_SOURCE_H__ */
