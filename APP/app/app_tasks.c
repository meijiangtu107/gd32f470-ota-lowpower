/* RTOS 迁移：内核钩子与业务任务。
 * 五个任务替代原 app_sched 的轮询表；周期一律用 vTaskDelayUntil，避免工作耗时累积成漂移。 */

#include "FreeRTOS.h"
#include "task.h"
#include "tasks.h"
#include "app_cmd.h"

/*============================================================================
 * 内核钩子
 *============================================================================*/

void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
    (void)xTask;
    (void)pcTaskName;
    taskDISABLE_INTERRUPTS();
    for (;;) {
    }
}

void vApplicationMallocFailedHook(void)
{
    taskDISABLE_INTERRUPTS();
    for (;;) {
    }
}

/*============================================================================
 * 业务任务
 *============================================================================*/

/* 协议：5ms 收串口，每 20 拍（100ms）跑一次周期巡检。
 * 两条路径同住一个任务，app_cmd.c 的状态天然串行，不需要锁。 */
#define CMD_PERIOD_MS   5U
#define CMD_TICK_DIV    20U

static void t_cmd(void *arg)
{
    TickType_t last = xTaskGetTickCount();
    uint32_t   div  = 0U;

    (void)arg;
    for (;;) {
        task_uart_run();

        if (++div >= CMD_TICK_DIV) {
            div = 0U;
            app_cmd_tick();
        }

        vTaskDelayUntil(&last, pdMS_TO_TICKS(CMD_PERIOD_MS));
    }
}

static void t_adc(void *arg)
{
    TickType_t last = xTaskGetTickCount();

    (void)arg;
    for (;;) {
        task_adc_run();
        vTaskDelayUntil(&last, pdMS_TO_TICKS(200U));
    }
}

static void t_key(void *arg)
{
    TickType_t last = xTaskGetTickCount();

    (void)arg;
    for (;;) {
        task_key_run();
        vTaskDelayUntil(&last, pdMS_TO_TICKS(10U));
    }
}

static void t_oled(void *arg)
{
    TickType_t last = xTaskGetTickCount();

    (void)arg;
    for (;;) {
        task_oled_run();
        vTaskDelayUntil(&last, pdMS_TO_TICKS(200U));
    }
}

static void t_led(void *arg)
{
    TickType_t last = xTaskGetTickCount();

    (void)arg;
    for (;;) {
        task_led_run();
        vTaskDelayUntil(&last, pdMS_TO_TICKS(500U));
    }
}

/*============================================================================
 * 创建
 *============================================================================*/

/* 栈单位是字（Cortex-M 下 1 字 = 4 字节）；优先级数值越大越高 */
static void task_create_checked(TaskFunction_t fn, const char *name,
                                uint16_t stack_words, UBaseType_t prio)
{
    if (xTaskCreate(fn, name, stack_words, NULL, prio, NULL) != pdPASS) {
        configASSERT(0);    /* 内核堆不够：任务没建起来，停住比带病运行好 */
    }
}

void app_tasks_create(void)
{
    task_create_checked(t_cmd,  "cmd",  640U, 3U);
    task_create_checked(t_adc,  "adc",  256U, 2U);
    task_create_checked(t_key,  "key",  192U, 2U);
    task_create_checked(t_oled, "oled", 384U, 1U);
    task_create_checked(t_led,  "led",   96U, 1U);
}