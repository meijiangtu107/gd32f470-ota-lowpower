/* SysTick 时基：1ms 中断累加毫秒计数，微秒延时走 DWT 周期计数器。
 * 中断里只做 s_tick_ms++，其余一概不干。 */

#include "bsp_tick.h"
#include <stdbool.h>
#include "gd32f4xx.h"

/* 系统启动至今的毫秒数。
 * volatile 必须加：它被 SysTick 中断修改、被主循环读取。
 * 不加的话编译器会认为没人改过它，把它缓存进寄存器，
 * 主循环将永远读到同一个旧值。 */
static volatile uint32_t s_tick_ms = 0U;

/* DWT 是否已使能，避免 bsp_delay_us 每次重复配置 */
static bool s_dwt_ready = false;

/*============================================================================
 * 中断侧：只做累加，不做任何阻塞操作
 *============================================================================*/

void bsp_tick_isr(void)
{
    s_tick_ms++;
}

/*============================================================================
 * 对外接口
 *============================================================================*/

void bsp_tick_init(void)
{
    /* SysTick_Config 是 CMSIS 提供的，内部完成：
     *   LOAD = ticks - 1        重装载值
     *   VAL  = 0                清当前计数
     *   CTRL = 内核时钟 | 开中断 | 启动
     * 返回非 0 表示 ticks-1 超出 24 位 LOAD 寄存器范围。
     * 240MHz / 1000 = 240000，未超范围，正常返回 0。 */
    if (SysTick_Config(SystemCoreClock / 1000U) != 0U) {
        /* 配置失败说明 SystemCoreClock 异常，停在这里便于调试 */
        while (1) {
        }
    }

    NVIC_SetPriority(SysTick_IRQn, 0x00U);
}

uint32_t bsp_tick_ms(void)
{
    return s_tick_ms;
}

void bsp_delay_ms(uint32_t ms)
{
    uint32_t start = s_tick_ms;

    /* 必须用减法：(s_tick_ms - start) 在 uint32_t 回绕时依然正确；
     * 若写成 (s_tick_ms < start + ms)，start + ms 溢出时会死循环。 */
    while ((s_tick_ms - start) < ms) {
    }
}

void bsp_delay_us(uint32_t us)
{
    uint32_t start;
    uint32_t ticks;

    if (!s_dwt_ready) {
        /* 使能 DWT 周期计数器：TRCENA 打开跟踪调试单元，CYCCNTENA 启动计数 */
        CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
        DWT->CYCCNT = 0U;
        DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
        s_dwt_ready = true;
    }

    start = DWT->CYCCNT;
    ticks = us * (SystemCoreClock / 1000000U);   /* 240MHz 时 1us = 240 周期 */

    while ((DWT->CYCCNT - start) < ticks) {
    }
}
