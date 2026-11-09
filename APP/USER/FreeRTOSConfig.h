#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

/* RTOS 迁移：内核配置（第一步）——三行向量重映射宏保持注释，第三步才打开 */
/* 提供 SystemCoreClock 和 __NVIC_PRIO_BITS（= 4） */
#include "gd32f4xx.h"

/* ---- 基本参数 ---- */
#define configUSE_PREEMPTION                    1
#define configUSE_TIME_SLICING                  1
#define configUSE_IDLE_HOOK                     0
#define configUSE_TICK_HOOK                     0
#define configCPU_CLOCK_HZ                      (SystemCoreClock)
#define configTICK_RATE_HZ                      ((TickType_t)1000)
#define configMAX_PRIORITIES                    5
#define configMINIMAL_STACK_SIZE                128     /* 字，不是字节 */
#define configTOTAL_HEAP_SIZE                   ((size_t)(32 * 1024))
#define configMAX_TASK_NAME_LEN                 16
#define configUSE_16_BIT_TICKS                  0

/* ---- 行为开关 ---- */
#define configUSE_MUTEXES                       1
#define configUSE_COUNTING_SEMAPHORES           0
#define configUSE_RECURSIVE_MUTEXES             0
#define configUSE_TIMERS                        0
#define configUSE_CO_ROUTINES                   0
#define configUSE_TICKLESS_IDLE                 0
#define configUSE_PORT_OPTIMISED_TASK_SELECTION 0

/* ---- 中断与优先级：本文件最重要的三行 ---- */
#define configPRIO_BITS                         __NVIC_PRIO_BITS
#define configKERNEL_INTERRUPT_PRIORITY         (15 << (8 - configPRIO_BITS))
#define configMAX_SYSCALL_INTERRUPT_PRIORITY    (5  << (8 - configPRIO_BITS))

/* ---- 保险丝：调试期全开 ---- */
#define configCHECK_FOR_STACK_OVERFLOW          2
#define configUSE_MALLOC_FAILED_HOOK            1
#define configASSERT(x) \
    do { if ((x) == 0) { __disable_irq(); for (;;) {} } } while (0)

/* ---- 钩子函数 ----
 * 不要在 FreeRTOSConfig.h 里声明：TaskHandle_t 此时尚未定义，
 * 与 task.h 里的声明类型不一致会报错。
 * 直接在 app_tasks.c 里定义，签名用 task.h 的 TaskHandle_t。 */

/* ---- 需要的 API ---- */
#define INCLUDE_vTaskPrioritySet                1
#define INCLUDE_uxTaskPriorityGet               1
#define INCLUDE_vTaskDelete                     1
#define INCLUDE_vTaskSuspend                    1
#define INCLUDE_vTaskDelay                      1
#define INCLUDE_vTaskDelayUntil                 1
#define INCLUDE_xTaskGetTickCount               1
#define INCLUDE_xTaskGetSchedulerState          1
#define INCLUDE_uxTaskGetStackHighWaterMark     1
#define INCLUDE_xTaskGetCurrentTaskHandle       1

/* ---- 向量重映射：SVC/PendSV/SysTick 交给内核（与删除 SysTick_Handler 同步生效） ---- */
#define vPortSVCHandler                         SVC_Handler
#define xPortPendSVHandler                      PendSV_Handler
#define xPortSysTickHandler                     SysTick_Handler

#endif /* FREERTOS_CONFIG_H */
