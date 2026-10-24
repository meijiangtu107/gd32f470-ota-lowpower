/* 轮询式串口驱动，无中断、无 DMA。两路：USART0 (PA9/PA10) 调试口，
 * USART1 (PD5/PD6) 挂 RS485，方向脚 DE = PE8。 */

#include <stdarg.h>
#include <stdio.h>

#include "gd32f4xx.h"
#include "bl_uart.h"

/* 缓冲大小见 bl_uart.h 的 BL_UART_PRINT_BUF */
#define BL_PRINT_BUF   BL_UART_PRINT_BUF

static uint32_t s_ota_usart = USART0;

static void bl_uart_gpio_init(void)
{
    /* USART0：PA9 / PA10 */
    rcu_periph_clock_enable(RCU_GPIOA);
    gpio_af_set(GPIOA, GPIO_AF_7, GPIO_PIN_9 | GPIO_PIN_10);
    gpio_mode_set(GPIOA, GPIO_MODE_AF, GPIO_PUPD_PULLUP, GPIO_PIN_9 | GPIO_PIN_10);
    gpio_output_options_set(GPIOA, GPIO_OTYPE_PP, GPIO_OSPEED_50MHZ, GPIO_PIN_9 | GPIO_PIN_10);

    /* USART1：PD5 / PD6 */
    rcu_periph_clock_enable(RCU_GPIOD);
    gpio_af_set(GPIOD, GPIO_AF_7, GPIO_PIN_5 | GPIO_PIN_6);
    gpio_mode_set(GPIOD, GPIO_MODE_AF, GPIO_PUPD_PULLUP, GPIO_PIN_5 | GPIO_PIN_6);
    gpio_output_options_set(GPIOD, GPIO_OTYPE_PP, GPIO_OSPEED_50MHZ, GPIO_PIN_5 | GPIO_PIN_6);

    /* RS485 方向脚 PE8，上电默认接收。 */
    rcu_periph_clock_enable(RCU_GPIOE);
    gpio_mode_set(GPIOE, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, GPIO_PIN_8);
    gpio_output_options_set(GPIOE, GPIO_OTYPE_PP, GPIO_OSPEED_50MHZ, GPIO_PIN_8);
    gpio_bit_reset(GPIOE, GPIO_PIN_8);
}

static void bl_uart_param_init(uint32_t usart)
{
    usart_deinit(usart);
    usart_baudrate_set(usart, BL_UART_BAUD);
    usart_transmit_config(usart, USART_TRANSMIT_ENABLE);
    usart_receive_config(usart, USART_RECEIVE_ENABLE);
    usart_enable(usart);
}

void bl_uart_init(void)
{
    bl_uart_gpio_init();

    rcu_periph_clock_enable(RCU_USART0);
    rcu_periph_clock_enable(RCU_USART1);

    bl_uart_param_init(USART0);
    bl_uart_param_init(USART1);

#if (BL_OTA_UART == BL_OTA_UART_RS485)
    s_ota_usart = USART1;
#else
    s_ota_usart = USART0;
#endif
}

void bl_uart_deinit(void)
{
    usart_disable(USART0);
    usart_disable(USART1);
    usart_deinit(USART0);
    usart_deinit(USART1);
}

uint32_t bl_ota_usart(void)
{
    return s_ota_usart;
}

void bl_uart_write(uint32_t usart, const uint8_t *data, uint32_t len)
{
    uint32_t i;

    if ((data == 0) || (len == 0UL)) {
        return;
    }

    /* RS485 整段拉高 DE，发送完毕再释放。 */
    if (usart == USART1) {
        gpio_bit_set(GPIOE, GPIO_PIN_8);
    }

    for (i = 0UL; i < len; i++) {
        usart_data_transmit(usart, data[i]);
        while (usart_flag_get(usart, USART_FLAG_TBE) == RESET) {
        }
    }

    if (usart == USART1) {
        /* 等 TC（移位寄存器发空）再拉低 DE，不能等 TBE。 */
        while (usart_flag_get(usart, USART_FLAG_TC) == RESET) {
        }
        gpio_bit_reset(GPIOE, GPIO_PIN_8);
    }
}

void bl_uart_putc(uint32_t usart, uint8_t c)
{
    bl_uart_write(usart, &c, 1UL);
}

int bl_uart_getc(uint32_t usart, uint8_t *c)
{
    if (usart_flag_get(usart, USART_FLAG_RBNE) == RESET) {
        return 0;
    }
    *c = (uint8_t)usart_data_receive(usart);
    return 1;
}

void bl_uart_printf(uint32_t usart, const char *fmt, ...)
{
    char    buf[BL_PRINT_BUF];
    va_list ap;
    int     len;

    va_start(ap, fmt);
    len = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    if (len <= 0) {
        return;
    }

    /* vsnprintf 返回值可能大于缓冲大小，需要夹紧。 */
    if (len > (int)sizeof(buf) - 1) {
        len = (int)sizeof(buf) - 1;
    }

    bl_uart_write(usart, (const uint8_t *)buf, (uint32_t)len);
}
