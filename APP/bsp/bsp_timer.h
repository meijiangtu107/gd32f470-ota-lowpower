/**
 * @file    bsp_timer.h
 * @brief   TIMER1 —— 给 ADC 提供固定频率的触发脉冲
 * @author  meijiangtu
 * @date    2026-09-27
 * @note    这个定时器不当时基用（时基在 bsp_tick，走 SysTick）。
 *          它只干一件事：每隔固定时间产生一次更新事件，通过 TRGO
 *          踹 ADC 一脚，让 ADC 按硬件节奏采样。
 *
 *          为什么不用软件触发？软件触发的采样时刻由主循环决定，
 *          串口解析、刷屏这些任务会挤占时间，间隔会抖。
 *          定时器触发是硬件对齐的，间隔严格。
 */

#ifndef BSP_TIMER_H
#define BSP_TIMER_H

#include <stdint.h>

/* ADC 触发频率（Hz）。只改这一行，预分频和周期自动重算 */
#define BSP_TIMER1_TRIG_HZ      1000U

/* 配置 TIMER1 并启动。返回 0 成功，负值表示时钟算不出来 */
int      bsp_timer1_init(void);

/* 读回实际配置出来的触发频率，调试用。
 * 和 BSP_TIMER1_TRIG_HZ 对不上，就说明 APB1 时钟的推算错了 */
uint32_t bsp_timer1_get_trig_hz(void);

#endif /* BSP_TIMER_H */
