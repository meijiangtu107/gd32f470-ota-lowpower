/* 低功耗应用层动作：唤醒回调按依赖顺序重建外设，
 * 并提供参数页波特率代码到 USART1 实际波特率的换算。 */

#include "app_power.h"

#include "bsp_uart.h"
#include "bsp_led.h"
#include "bsp_key.h"
#include "bsp_spi.h"
#include "bsp_i2c.h"
#include "bsp_adc.h"
#include "bsp_timer.h"
#include "bsp_dac.h"
#include "drv_gd30ad3344.h"
#include "param_store.h"
#include "tasks.h"

uint32_t app_power_rs485_baud(void)
{
    switch (param_get_baudrate()) {
    case 0x11U: return 4800U;
    case 0x12U: return 9600U;
    case 0x14U: return 115200U;
    case 0x13U:
    default:    return 19200U;
    }
}

/* 依赖顺序不能颠倒：SPI3 先于 AD3344，ADC 先于 TIMER1 */
void app_power_restore_after_wake(void)
{
    uint32_t dac_value;

    bsp_uart0_init(APP_UART0_BAUD);
    bsp_uart1_init(app_power_rs485_baud());
    bsp_led_init();
    bsp_key_init();

    bsp_spi0_init();
    bsp_spi3_init();
    drv_gd30_init();

    bsp_i2c0_init();
    /* OLED 硬件未确认，暂不恢复 */
    /* task_oled_init(); */

    (void)bsp_adc_init();
    (void)bsp_timer1_init();

    (void)bsp_dac_init();
    dac_value = param_get_dac_value();
    if (dac_value > 0x0FFFU) {
        dac_value = 0U;
    }
    (void)bsp_dac_set((uint16_t)dac_value);
}