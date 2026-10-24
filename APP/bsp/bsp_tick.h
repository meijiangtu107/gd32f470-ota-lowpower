/* SysTick 时基：毫秒计数 + 微秒延时（DWT 实现）。
 * 毫秒计数约 49.7 天回绕一次，超时判断必须用无符号减法比较。 */

#ifndef BSP_TICK_H
#define BSP_TICK_H

#include <stdint.h>

/* 初始化 SysTick，1ms 中断。依赖 SystemInit() 已配好 SystemCoreClock */
void bsp_tick_init(void);

/* 读取系统启动至今的毫秒数（约 49.7 天回绕一次）
 * 非阻塞。超时判断必须写成 (bsp_tick_ms() - start) >= timeout */
uint32_t bsp_tick_ms(void);

/* 阻塞延时（毫秒）。不要在中断里调用 */
void bsp_delay_ms(uint32_t ms);

/* 阻塞延时（微秒），基于 DWT 周期计数器。不要在中断里调用 */
void bsp_delay_us(uint32_t us);

/* SysTick 中断处理，只由 SysTick_Handler 调用 */
void bsp_tick_isr(void);

#endif /* BSP_TICK_H */
