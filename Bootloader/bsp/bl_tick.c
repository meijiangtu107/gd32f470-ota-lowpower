/* Bootloader 时基：SysTick 毫秒计数。 */

#include "gd32f4xx.h"
#include "bl_tick.h"

static volatile uint32_t s_tick_ms;

void bl_tick_init(void)
{
    /* 1 ms 中断一次；配置失败说明 SystemCoreClock 不对，直接停住。 */
    if (SysTick_Config(SystemCoreClock / 1000U) != 0U) {
        while (1) {
        }
    }
    NVIC_SetPriority(SysTick_IRQn, 0x00U);
}

void bl_tick_isr(void)
{
    s_tick_ms++;
}

uint32_t bl_tick_ms(void)
{
    return s_tick_ms;
}

int bl_tick_elapsed(uint32_t start, uint32_t timeout_ms)
{
    /* 无符号减法，计数回绕也正确。 */
    return ((s_tick_ms - start) >= timeout_ms) ? 1 : 0;
}

void bl_delay_ms(uint32_t ms)
{
    uint32_t start = s_tick_ms;

    while ((s_tick_ms - start) < ms) {
    }
}
