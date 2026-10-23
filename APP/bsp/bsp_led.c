/**
 * @file    bsp_led.c
 * @brief   LED 控制（PD10~PD15 = LED1~LED6）
 * @author  meijiangtu
 * @date    2026-09-16
 */

#include "bsp_led.h"
#include "bsp_pinmap.h"
#include "bsp_gpio.h"
#include "gd32f4xx.h"

void bsp_led_init(void)
{
    uint32_t i;

    bsp_gpio_config_array(BSP_PIN_LED, (uint32_t)BSP_LED_COUNT);

    /* 初始全灭。旧工程 LED_ACTIVE_HIGH = 1，即高电平点亮 */
    for (i = 0U; i < (uint32_t)BSP_LED_COUNT; i++) {
        gpio_bit_reset(BSP_PIN_LED[i].port, BSP_PIN_LED[i].pin);
    }
}

void bsp_led_write(bsp_led_t led, bool on)
{
    if ((uint32_t)led >= (uint32_t)BSP_LED_COUNT) {
        return;
    }

    gpio_bit_write(BSP_PIN_LED[led].port, BSP_PIN_LED[led].pin, on ? SET : RESET);
}

void bsp_led_toggle(bsp_led_t led)
{
    if ((uint32_t)led >= (uint32_t)BSP_LED_COUNT) {
        return;
    }

    gpio_bit_toggle(BSP_PIN_LED[led].port, BSP_PIN_LED[led].pin);
}
