/* 串口轮询驱动（无中断、无 DMA），115200-8-N-1。
 * OTA 走哪条口由 BL_OTA_UART 决定，调试口固定 USART0，两路都会初始化。 */

#ifndef BL_UART_H
#define BL_UART_H

#include <stdint.h>
#include "gd32f4xx.h"

/* 调试打印口，固定为 USART0，不随 OTA 端口变化。 */
#define BL_LOG_USART   USART0

/* ---- OTA 走哪条串口 ----
 * 该宏只决定 OTA 收发用哪条口，其余不动。 */
#define BL_OTA_UART_DEBUG   0   /* USART0 / PA9 PA10，板载 CH340 */
#define BL_OTA_UART_RS485   1   /* USART1 / PD5 PD6，DE = PE8 */

#ifndef BL_OTA_UART
#define BL_OTA_UART   BL_OTA_UART_DEBUG
#endif

#define BL_UART_BAUD   115200U

/* bl_uart_printf 的栈缓冲大小。
 * 格式化结果超过该长度会被静默截断；BL 栈只有 1KB，不要调大。 */
#define BL_UART_PRINT_BUF   128U

void     bl_uart_init(void);

/* 跳转前调用，关闭串口，避免与 APP 的外设配置冲突。 */
void     bl_uart_deinit(void);

void     bl_uart_putc(uint32_t usart, uint8_t c);
void     bl_uart_write(uint32_t usart, const uint8_t *data, uint32_t len);

/* 非阻塞读一个字节。返回 1 = 读到了，0 = 现在没有 */
int      bl_uart_getc(uint32_t usart, uint8_t *c);

void     bl_uart_printf(uint32_t usart, const char *fmt, ...);

/* OTA 端口（由 BL_OTA_UART 决定），收发固件都用它 */
uint32_t bl_ota_usart(void);

#endif /* BL_UART_H */
