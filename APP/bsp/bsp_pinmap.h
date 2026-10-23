/**
 * @file    bsp_pinmap.h
 * @brief   全工程引脚定义唯一来源
 * @author  meijiangtu
 * @date    2026-09-16
 * @note    表定义在 bsp_gpio.c。改引脚只改那一处，别处不动。
 */

#ifndef BSP_PINMAP_H
#define BSP_PINMAP_H

#include <stdint.h>
#include "bsp_led.h"
#include "bsp_key.h"

/* 一个引脚的全部配置。bsp_gpio_config() 拿它一次配好 */
typedef struct {
    uint32_t port;      /* GPIOA / GPIOB / GPIOE ... */
    uint32_t rcu;       /* RCU_GPIOA ... */
    uint16_t pin;       /* GPIO_PIN_10，可多选或运算 */
    uint8_t  af;        /* GPIO_AF_x，0 = 不用复用功能 */
    uint32_t mode;      /* GPIO_MODE_OUTPUT / INPUT / AF */
    uint32_t pupd;      /* GPIO_PUPD_NONE / PULLUP / PULLDOWN */
    uint32_t otype;     /* GPIO_OTYPE_PP / OD */
    uint32_t ospeed;    /* GPIO_OSPEED_50MHZ ... */
} bsp_pin_t;

/* LED：PD10~PD15，高电平点亮 */
extern const bsp_pin_t BSP_PIN_LED[BSP_LED_COUNT];

/* 按键：PE15 / PE6 / PE11 / PE4 / PE7 / PB0，上拉输入，按下为低 */
extern const bsp_pin_t BSP_PIN_KEY[BSP_KEY_COUNT];

/* 低功耗唤醒键 PA0（EXTI0），上拉输入，按下为低。
 * 不属于 6 个业务按键，深睡时其它脚都切模拟态，它必须保持输入 */
extern const bsp_pin_t BSP_PIN_WAKEUP_KEY;

/* 调试串口 USART0：PA9 = TX / PA10 = RX，AF7 */
extern const bsp_pin_t BSP_PIN_UART0_TX;
extern const bsp_pin_t BSP_PIN_UART0_RX;

/* RS485 串口 USART1：PD5 = TX / PD6 = RX，AF7 */
extern const bsp_pin_t BSP_PIN_UART1_TX;
extern const bsp_pin_t BSP_PIN_UART1_RX;

/* RS485 收发方向控制脚。高电平 = 发送，低电平 = 接收 */
extern const bsp_pin_t BSP_PIN_RS485_DE;

/* SPI0（GD25Qxx Flash）：PB3 = SCK / PB4 = MISO / PB5 = MOSI，AF5 */
extern const bsp_pin_t BSP_PIN_SPI0_SCK;
extern const bsp_pin_t BSP_PIN_SPI0_MISO;
extern const bsp_pin_t BSP_PIN_SPI0_MOSI;

/* SPI0 的片选，软件控制，普通输出 */
extern const bsp_pin_t BSP_PIN_SPI0_CS;

/* SPI3（GD30AD3344）：PE12 = SCK / PE13 = MISO / PE14 = MOSI，AF5 */
extern const bsp_pin_t BSP_PIN_SPI3_SCK;
extern const bsp_pin_t BSP_PIN_SPI3_MISO;
extern const bsp_pin_t BSP_PIN_SPI3_MOSI;

/* SPI3 的片选，软件控制，普通输出 */
extern const bsp_pin_t BSP_PIN_SPI3_CS;
/* I2C0（OLED）：PB8 = SCL / PB9 = SDA，AF4。
 * 必须是开漏输出 —— I2C 是线与总线，配成推挽会导致总线争用 */
extern const bsp_pin_t BSP_PIN_I2C0_SCL;
extern const bsp_pin_t BSP_PIN_I2C0_SDA;

/* ADC0 模拟输入：PC0 = CH10 / PC1 = CH11。
 * 模拟模式不用复用功能（af = 0），otype/ospeed 也不起作用。
 * PC1 是 DAC 回读点：PA4（DAC0_OUT0）用跳线接过来，用来验证 DAC 输出。
 * 旧工程注释也写 PC1，但实际采的是 CH12/PC2 —— 从没真正读回过 DAC */
extern const bsp_pin_t BSP_PIN_ADC_CH0;
extern const bsp_pin_t BSP_PIN_ADC_CH1;

/* DAC0_OUT0 = PA4，模拟输出。跳线接到 PC1 做回读（见上面 PC1 的说明）。
 * 同样是模拟模式，af = 0，otype/ospeed 不起作用 */
extern const bsp_pin_t BSP_PIN_DAC_OUT;

#endif /* BSP_PINMAP_H */
