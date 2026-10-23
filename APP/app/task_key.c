/* 按键事件处理任务 */

#include "tasks.h"
#include "bsp_key.h"
#include "bsp_uart.h"

/* 10ms 调一次：先扫描再逐个取事件，取走即清，按住只报一次 */
void task_key_run(void)
{
    uint32_t i;

    bsp_key_scan();

    for (i = 0U; i < (uint32_t)BSP_KEY_COUNT; i++) 
    {
        switch (bsp_key_get_event((bsp_key_t)i)) {
        case BSP_KEY_EVENT_PRESS:
            log_printf(USART0, "KEY%lu PRESS\r\n", (unsigned long)(i + 1U));
            break;

        case BSP_KEY_EVENT_RELEASE:
            log_printf(USART0, "KEY%lu RELEASE\r\n", (unsigned long)(i + 1U));
            break;

        case BSP_KEY_EVENT_LONG_PRESS:
            log_printf(USART0, "KEY%lu LONG_PRESS\r\n", (unsigned long)(i + 1U));
            break;

        default:
            break;
        }
    }
}
