/* BL 的中断向量：全程只使能 SysTick 一个中断（1ms 时基），串口收发均为轮询。 */

#include "gd32f4xx.h"
#include "bl_tick.h"

/* HardFault 现场，可在 Keil 的 Watch 窗口观察 CFSR 与 HFSR。 */
volatile uint32_t g_fault_cfsr = 0;
volatile uint32_t g_fault_hfsr = 0;

void HardFault_Handler(void)
{
    g_fault_cfsr = SCB->CFSR;
    g_fault_hfsr = SCB->HFSR;

    while (1) {
    }
}

void SysTick_Handler(void)
{
    bl_tick_isr();
}
