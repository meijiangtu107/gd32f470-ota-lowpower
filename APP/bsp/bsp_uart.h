/**
 * @file    bsp_uart.h
 * @brief   串口：USART0 调试口 + USART1 RS485
 * @author  meijiangtu
 * @date    2026-09-16
 * @note    接收走 DMA + 空闲中断，数据进 ringbuf；
 *          发送走轮询，RS485 那路带软件方向控制。
 */

#ifndef BSP_UART_H
#define BSP_UART_H

#include <stdint.h>
#include "gd32f4xx.h"

/* 接收缓冲大小。DMA 搬满这么多就绕回，ringbuf 实存 RX_BUF_SIZE-1 个 */
#define BSP_UART_RX_SIZE    256U

/* RS485 收发器方向切换的等待时间。
 * SETUP：拉高 DE 到收发器真正开始驱动总线
 * HOLD ：最后一个字节发完后，保持 DE 的时间（至少一个字符时间）
 * 115200 下一个字符约 8.7µs，取 10µs 留余量 */
#define RS485_DE_SETUP_US   10U
#define RS485_DE_HOLD_US    10U

/* 初始化。两路各自一份，参数不同但流程一样 */
int bsp_uart0_init(uint32_t baud);      /* 调试口 PA9/PA10 */
int bsp_uart1_init(uint32_t baud);      /* RS485  PD5/PD6，DE = PE8 */

/* 发送。usart 传 USART0 或 USART1。返回发送字节数，失败返回负值。
 * 传 USART1 时自动控制 DE 引脚方向 */
int bsp_uart_write(uint32_t usart, const uint8_t *data, uint16_t len);

/* 从接收缓冲取数据。返回实际取到的字节数，0 = 没数据 */
uint32_t bsp_uart_read(uint32_t usart, uint8_t *data, uint16_t len);

/* 格式化打印，用法同 printf，第一个参数指定往哪个串口打 */
int log_printf(uint32_t usart, const char *fmt, ...);

/* 日志总开关。默认开。
 * 协议和日志共用 USART0 时必须关掉 —— 协议帧是 ASCII 十六进制，
 * 日志文本混进去会让上位机找错帧头。关闭后 log_printf 直接返回 0，
 * 不产生任何输出（不影响返回值逻辑）。 */
void bsp_log_enable(uint8_t on);

/* 中断处理，只由 gd32f4xx_it.c 调用 */
void bsp_uart0_irq_handler(void);
void bsp_uart1_irq_handler(void);

#endif /* BSP_UART_H */
