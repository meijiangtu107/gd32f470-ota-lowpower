/* 调度器接口：任务表项定义与初始化/轮询入口 */

#ifndef APP_SCHED_H
#define APP_SCHED_H

#include <stdint.h>

typedef struct {
    void (*task)(void);
    uint32_t period_ms;
    uint32_t last_run;
} app_task_t;

/* 初始化：各任务的 last_run 置为当前 tick */
void app_sched_init(void);

/* 跑一轮：到点任务执行一次，非阻塞 */
void app_sched_run(void);

#endif /* APP_SCHED_H */
