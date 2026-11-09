#ifndef BSP_IRQ_PRIO_H
#define BSP_IRQ_PRIO_H

/* 全工程 NVIC 优先级唯一出处。
 * 数值越小越紧急，与任务优先级相反。
 * 0~4  ：绝对不调用 FreeRTOS API 的高实时中断
 * 5~15 ：允许调用 FromISR 系列函数
 * 分组固定 PRE4_SUB0：4 位全给抢占，这是 FreeRTOS 唯一支持的分组。 */

#define BSP_IRQ_PRIO_WAKEUP     1U   /* EXTI0、RTC_WKUP：只清标志 */
#define BSP_IRQ_PRIO_USART0     5U   /* USART0 协议口 */
#define BSP_IRQ_PRIO_USART1     6U   /* USART1 RS485 业务口 */
#define BSP_IRQ_PRIO_DMA_ADC    6U   /* DMA1_CH4 ADC 快照 */

void bsp_irq_prio_init(void);

#endif /* BSP_IRQ_PRIO_H */
