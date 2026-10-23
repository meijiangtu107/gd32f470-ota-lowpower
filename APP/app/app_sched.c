/* 主循环任务调度器 */

#include "app_sched.h"
#include "tasks.h"
#include "app_cmd.h"
#include "bsp_tick.h"

/* 任务表：period_ms 为执行周期，last_run 由调度器维护；高频任务排在前面 */
static app_task_t s_tasks[] = {
    { task_key_run,   10U, 0U },    /* 10ms   按键扫描（这个周期同时就是消抖） */
    { task_uart_run,  10U, 0U },    /* 10ms   协议接收 */
    { app_cmd_tick,  100U, 0U },    /* 100ms  自动上报 + 告警边沿检测 */
    { task_adc_run,  200U, 0U },    /* 200ms  更新三通道 */
    { task_oled_run, 200U, 0U },    /* 200ms  OLED 刷新 */
    { task_led_run,  500U, 0U }     /* 500ms  LED1 翻转（1s 周期） */
};

#define TASK_COUNT      (sizeof(s_tasks) / sizeof(s_tasks[0]))

void app_sched_init(void)
{
    uint32_t i;

    for (i = 0U; i < (uint32_t)TASK_COUNT; i++) {
        s_tasks[i].last_run = bsp_tick_ms();
    }
}

void app_sched_run(void)
{
    uint32_t now = bsp_tick_ms();
    uint32_t i;

    for (i = 0U; i < (uint32_t)TASK_COUNT; i++) {
        /* 用差值比较，uint32_t 回绕时仍正确 */
        if ((now - s_tasks[i].last_run) >= s_tasks[i].period_ms) {
            s_tasks[i].last_run = now;
            s_tasks[i].task();
        }
    }
}
