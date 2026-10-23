/* 协议接收任务：USART0 字节流 -> 帧 -> 命令分发。
 * 协议与调试共用 USART0（PA9/PA10，115200），日志已在协议初始化后关闭。 */

#include "tasks.h"
#include "bsp_uart.h"
#include "app_protocol.h"
#include "app_cmd.h"

/* 单次最多取 64 字节，栈只有 1KB */
#define UART_RX_CHUNK   64U

void task_uart_run(void)
{
    uint8_t  buf[UART_RX_CHUNK];
    uint32_t n;
    uint32_t i;

    n = bsp_uart_read(USART0, buf, UART_RX_CHUNK);
    if (n == 0U) {
        return;
    }

    for (i = 0U; i < n; i++) {
        if (app_protocol_feed(buf[i]) != 0) {
            app_cmd_dispatch(app_protocol_get_frame());
        }
    }
}
