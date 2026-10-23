/* Bootloader 时基接口：SysTick 毫秒计数。 */

#ifndef BL_TICK_H
#define BL_TICK_H

#include <stdint.h>

void     bl_tick_init(void);

uint32_t bl_tick_ms(void);

/* 由 SysTick_Handler 调用 */
void     bl_tick_isr(void);

/* 从 start 起是否已过 timeout_ms，非阻塞。 */
int      bl_tick_elapsed(uint32_t start, uint32_t timeout_ms);

/* 阻塞延时，仅用于短延时。 */
void     bl_delay_ms(uint32_t ms);

#endif /* BL_TICK_H */
