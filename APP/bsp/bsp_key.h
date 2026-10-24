/*
 * 按键扫描接口。消抖没有专门的延时，靠"每 10ms 才看一次"实现，
 * 因此 bsp_key_scan() 必须由任务按固定周期调用，不能乱调。
 */

#ifndef BSP_KEY_H
#define BSP_KEY_H

#include <stdint.h>

typedef enum {
    BSP_KEY_1 = 0,
    BSP_KEY_2,
    BSP_KEY_3,
    BSP_KEY_4,
    BSP_KEY_5,
    BSP_KEY_6,
    BSP_KEY_COUNT
} bsp_key_t;

typedef enum {
    BSP_KEY_EVENT_NONE = 0,
    BSP_KEY_EVENT_PRESS,        /* 刚按下 */
    BSP_KEY_EVENT_RELEASE,      /* 刚松开 */
    BSP_KEY_EVENT_LONG_PRESS    /* 按住满 1 秒，只报一次 */
} bsp_key_event_t;

void bsp_key_init(void);

/* 扫描一次。由 task_key_run() 每 10ms 调一次，别在别处调 */
void bsp_key_scan(void);

/* 取走一个事件，取走即清。同一个事件不会连着报两次 */
bsp_key_event_t bsp_key_get_event(bsp_key_t key);

#endif /* BSP_KEY_H */
