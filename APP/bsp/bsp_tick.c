/* SysTick 时基适配层：
 * 调度器启动前用 DWT 计时，启动后直接用内核 tick。
 * 对外接口与迁移前一致，调用方零改动。 */

#include "bsp_tick.h"
#include <stdbool.h>
#include "gd32f4xx.h"
#include "FreeRTOS.h"
#include "task.h"

static bool s_dwt_ready = false;

static void dwt_init(void)
{
    if (s_dwt_ready) {
        return;
    }
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0U;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    s_dwt_ready = true;
}

/* 调度器启动前唯一的毫秒表。32 位 @240MHz 约 17.9 秒回绕，
 * 初始化阶段用足够。 */
static uint32_t dwt_now_ms(void)
{
    return DWT->CYCCNT / (SystemCoreClock / 1000U);
}

void bsp_tick_init(void)
{
    /* SysTick 已归内核，这里只初始化 DWT。
     * main() 中原有的调用保留，不要删。 */
    dwt_init();
}

uint32_t bsp_tick_ms(void)
{
    /* 只有调度器正常运行时 tick 才在走。
     * vTaskSuspendAll 期间 xTaskIncrementTick 只累加 pended ticks，
     * xTaskGetTickCount 返回冻结值——用它做超时判断会死循环。 */
    if (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING) {
        return (uint32_t)xTaskGetTickCount();
    }
    return dwt_now_ms();
}

void bsp_delay_ms(uint32_t ms)
{
    uint32_t start;

    if (ms == 0U) {
        return;
    }

    /* 只有调度器正常运行时才能 vTaskDelay。
     * 中断里（IPSR != 0）、启动前（NOT_STARTED）、低功耗唤醒期间
     * （vTaskSuspendAll 后是 SUSPENDED）都必须忙等——冻结状态下
     * vTaskDelay 永远不会返回。 */
    if ((__get_IPSR() != 0U) ||
        (xTaskGetSchedulerState() != taskSCHEDULER_RUNNING)) {
        dwt_init();
        start = DWT->CYCCNT;
        while ((DWT->CYCCNT - start) < (ms * (SystemCoreClock / 1000U))) {
        }
        return;
    }

    /* 任务里：让出 CPU */
    vTaskDelay(pdMS_TO_TICKS(ms));
}

void bsp_delay_us(uint32_t us)
{
    uint32_t start;
    uint32_t ticks;

    dwt_init();

    start = DWT->CYCCNT;
    ticks = us * (SystemCoreClock / 1000000U);
    while ((DWT->CYCCNT - start) < ticks) {
    }
}

void bsp_tick_resume(void)
{
    /* 唤醒专用：只重设装载值、清计数、开中断，
     * 不碰内核设置过的优先级。 */
    SysTick->LOAD = (SystemCoreClock / 1000U) - 1U;
    SysTick->VAL  = 0U;
    SysTick->CTRL |= SysTick_CTRL_TICKINT_Msk | SysTick_CTRL_ENABLE_Msk;
}
