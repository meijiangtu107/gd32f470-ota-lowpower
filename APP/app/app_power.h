/* 低功耗应用层接口：RS485 波特率换算与唤醒后的外设重建回调 */

#ifndef APP_POWER_H
#define APP_POWER_H

#include <stdint.h>

/* USART0 调试/协议口固定波特率 */
#define APP_UART0_BAUD  115200U

/* 参数页里的波特率代码转成实际波特率（RS485 业务口用） */
uint32_t app_power_rs485_baud(void);

/* 深睡唤醒后重建外设，由 main.c 注册给 bsp_pmu 作为回调 */
void app_power_restore_after_wake(void);

#endif