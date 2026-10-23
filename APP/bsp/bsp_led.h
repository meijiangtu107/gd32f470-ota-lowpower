/**
 * @file    bsp_led.h
 * @brief   LED 控制（PD10~PD15 = LED1~LED6）
 * @author  meijiangtu
 * @date    2026-09-16
 */

#ifndef BSP_LED_H
#define BSP_LED_H

#include <stdbool.h>

/* 对外只暴露灯的编号，"哪号灯是哪个引脚"藏在 .c 的表里。
 * 换块板子只改表，头文件不动 */
typedef enum {
    BSP_LED_1 = 0,
    BSP_LED_2,
    BSP_LED_3,
    BSP_LED_4,
    BSP_LED_5,
    BSP_LED_6,
    BSP_LED_COUNT
} bsp_led_t;

/* 初始化全部 LED 引脚，初始状态全灭 */
void bsp_led_init(void);

/* on = true 点亮，false 熄灭。传越界编号直接返回，不做任何事 */
void bsp_led_write(bsp_led_t led, bool on);

/* 翻转。传越界编号直接返回 */
void bsp_led_toggle(bsp_led_t led);

#endif /* BSP_LED_H */
