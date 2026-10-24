/*
 * 低功耗接口：Deep-sleep 进出 + 唤醒恢复。
 *
 * 选 Deep-sleep 而不是 Standby：
 *   Deep-sleep  醒来看起来像"函数返回"，SRAM 和寄存器都还在
 *   Standby     醒来等于复位，从 main() 重新开始
 * 本项目的需求是"睡 10 秒继续干活"，只能选前者。
 *
 * 【唤醒后外设怎么恢复】
 * bsp 层不能调 drivers/app，所以这里只负责电源、时钟、VTOR、
 * 以及 bsp_tick 的重建；其余外设由上层通过回调注入。
 * main.c 里注册 app_restore_after_wake() 即可。
 */

#ifndef BSP_PMU_H
#define BSP_PMU_H

#include <stdint.h>

/* 唤醒后回调。由 main.c 注册，负责重 init 所有外设 */
typedef void (*bsp_pmu_restore_fn_t)(void);

void bsp_pmu_set_restore(bsp_pmu_restore_fn_t fn);

/* 进入 Deep-sleep，由 RTC 唤醒定时器在 seconds 秒后唤醒。
 * 返回 0 = 睡过并已恢复；-1 = RTC 未就绪，没有进入睡眠。
 *
 * RTC 没起来时唤醒定时器不会触发，睡下去就醒不来 —— 所以这里
 * 先检查 bsp_rtc_is_ready()，未就绪直接拒绝，调用方回错误帧即可。 */
int bsp_pmu_deepsleep_rtc(uint16_t seconds);

/* 进入 Deep-sleep，由 PA0 唤醒键（EXTI0）唤醒。
 * 返回 0 = 睡过并已恢复；-1 = 未进入（保留给将来扩展）。
 * 若按下动作发生在进入睡眠之前，边沿已经过去，设备会一直睡，
 * 所以这个功能适合"按住不放再进睡眠"或"先睡再按"的用法。 */
int bsp_pmu_deepsleep_key(void);

/* 中断转发，只由 gd32f4xx_it.c 调 */
void bsp_pmu_rtc_isr(void);
void bsp_pmu_key_isr(void);

#endif /* BSP_PMU_H */