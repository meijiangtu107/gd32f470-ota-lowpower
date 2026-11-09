/* SysTick 时基适配层：调度器启动前毫秒走 DWT、启动后走内核 tick；微秒延时恒走 DWT。
 * 对外接口与迁移前一致，超时判断仍用无符号减法比较。 */

#ifndef BSP_TICK_H
#define BSP_TICK_H

#include <stdint.h>

/* 只初始化 DWT（SysTick 已归内核）。main() 中的原有调用保留 */
void bsp_tick_init(void);

/* 读取系统启动至今的毫秒数（约 49.7 天回绕一次）
 * 非阻塞。超时判断必须写成 (bsp_tick_ms() - start) >= timeout */
uint32_t bsp_tick_ms(void);

/* 阻塞延时（毫秒）。任务里让出 CPU；中断里与调度器启动前 DWT 忙等 */
void bsp_delay_ms(uint32_t ms);

/* 阻塞延时（微秒），基于 DWT 周期计数器。不要在中断里调用 */
void bsp_delay_us(uint32_t us);

/* 睡眠唤醒后恢复内核 SysTick（重装载值/清计数/开中断），不碰优先级 */
void bsp_tick_resume(void);

#endif /* BSP_TICK_H */
