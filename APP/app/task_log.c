/* 调试日志任务：当前为空壳，需要时恢复函数体内注释掉的 log_printf */

#include "tasks.h"
#include "bsp_tick.h"
#include "bsp_uart.h"

void task_log_run(void)
{
//  log_printf(USART1, "tick = %lu ms\r\n", (unsigned long)bsp_tick_ms());
}


