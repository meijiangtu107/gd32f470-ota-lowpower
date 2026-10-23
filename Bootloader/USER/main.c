/* Bootloader 主程序：初始化时基与串口后进入启动核心。 */

#include "gd32f4xx.h"
#include "bl_tick.h"
#include "bl_uart.h"
#include "bl_core.h"

int main(void)
{
    bl_tick_init();
    bl_uart_init();

    /* 不返回：要么跳进某个槽，要么停在错误循环 */
    bl_core_run();

    while (1) {
    }
}
