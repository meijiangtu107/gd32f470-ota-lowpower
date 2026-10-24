/* 中断向量表的服务函数全部集中在此，函数体只做一次转发；
 * 覆盖 SysTick(1ms)、USART0/USART1、DMA1_CH4(ADC 序列搬完)、
 * RTC 唤醒定时器(EXTI22) 和 PA0 唤醒键(EXTI0)。 */

#include "gd32f4xx.h"
#include "bsp_tick.h"
#include "bsp_uart.h"
#include "bsp_adc.h"
#include "bsp_pmu.h"

/* HardFault 现场，在 Keil Watch 窗口查看；HFSR.FORCED 置位表示可配置 fault 升级，对应原因见 CFSR */
volatile uint32_t g_fault_cfsr = 0;
volatile uint32_t g_fault_hfsr = 0;

/* HardFault：记录 CFSR/HFSR 后停机 */
void HardFault_Handler(void)
{
    g_fault_cfsr = SCB->CFSR;
    g_fault_hfsr = SCB->HFSR;

    while (1) {
    }
}

/* SysTick 中断，1ms 一次，转发到 bsp_tick_isr() */
void SysTick_Handler(void)
{
    bsp_tick_isr();
}

/* USART0 中断（调试口）：只开 IDLE 中断，数据由 DMA 搬运 */
void USART0_IRQHandler(void)
{
    bsp_uart0_irq_handler();
}

/* USART1 中断（RS485） */
void USART1_IRQHandler(void)
{
    bsp_uart1_irq_handler();
}

/* DMA1_CH4 中断：ADC 由 TIMER1 TRGO 触发，DMA 搬满 BSP_ADC_CH_COUNT 个值后置 FTF，这里做冻结副本。 */
void DMA1_Channel4_IRQHandler(void)
{
    bsp_adc_dma_isr();
}

/* RTC 唤醒定时器中断（EXTI 22）：只清标志，外设恢复在 WFI 返回后由 bsp_pmu 统一执行 */
void RTC_WKUP_IRQHandler(void)
{
    bsp_pmu_rtc_isr();
}

/* PA0 唤醒键中断（EXTI0） */
void EXTI0_IRQHandler(void)
{
    bsp_pmu_key_isr();
}
