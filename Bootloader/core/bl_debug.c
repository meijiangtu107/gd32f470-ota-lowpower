/* 启动早期的串口命令窗口：用来人工构造各种启动状态。
 * 每条命令改完参数即落盘，CRC 始终保持合法。 */

#include "bl_debug.h"

#if BL_DEBUG_CMDS

#include "gd32f4xx.h"

#include "bl_uart.h"
#include "bl_tick.h"

#include "partition.h"
#include "param_store.h"

/* 等待窗口长度（ms） */
#define BL_DEBUG_WINDOW_MS   5000U

static void bl_debug_dump(void)
{
    const app_param_t *p = param_store_get();

    bl_uart_printf(BL_LOG_USART,
                   "  state: update_flag=0x%08lX  boot_slot=%lu  trials=%lu  confirm=0x%08lX\r\n",
                   (unsigned long)p->update_flag,
                   (unsigned long)p->boot_slot,
                   (unsigned long)p->boot_trials,
                   (unsigned long)p->boot_confirm);
}

static void bl_debug_help(void)
{
    /* 帮助信息必须一行一次 printf：BL_PRINT_BUF 为 128 字节，
     * 整段输出会被 vsnprintf 静默截断。 */
    bl_uart_printf(BL_LOG_USART, "\r\n--- BL debug commands (%u ms window) ---\r\n",
                   (unsigned)BL_DEBUG_WINDOW_MS);
    bl_debug_dump();
    bl_uart_printf(BL_LOG_USART, "  1 = update_flag -> FAST_UPGRADE\r\n");
    bl_uart_printf(BL_LOG_USART, "  2 = update_flag -> PENDING\r\n");
    bl_uart_printf(BL_LOG_USART, "  3 = clear boot_confirm, trials=0\r\n");
    bl_uart_printf(BL_LOG_USART, "      -> BL gives one more chance\r\n");
    bl_uart_printf(BL_LOG_USART, "  4 = clear boot_confirm, trials=%u\r\n",
                   (unsigned)PARAM_BOOT_TRIALS_MAX);
    bl_uart_printf(BL_LOG_USART, "      -> BL rolls back on next boot\r\n");
    bl_uart_printf(BL_LOG_USART, "  5 = boot_slot -> the other slot\r\n");
    bl_uart_printf(BL_LOG_USART, "  0 = boot now (skip window)\r\n");
    bl_uart_printf(BL_LOG_USART, "  ? = show this again\r\n");
    bl_uart_printf(BL_LOG_USART, "--------------------------------------\r\n");
}

/* 落盘并复位，命令生效的唯一出口。 */
static void bl_debug_commit_and_reset(void)
{
    (void)param_store_save();
    bl_uart_printf(BL_LOG_USART, "saved. resetting...\r\n");
    bl_delay_ms(50UL);

    /* 落盘后复位走真实启动路径，不在窗口里就地模拟。 */
    NVIC_SystemReset();
    for (;;) {
    }
}

void bl_debug_window(void)
{
    uint32_t start = bl_tick_ms();
    uint8_t  c;

    bl_debug_help();

    while (bl_tick_elapsed(start, BL_DEBUG_WINDOW_MS) == 0) {
        if (bl_uart_getc(BL_LOG_USART, &c) == 0) {
            continue;
        }

        switch (c) {
        case '0':
            bl_uart_printf(BL_LOG_USART, "skip window\r\n");
            return;

        case '?':
            bl_debug_help();
            break;

        case '1':
            param_set_update_flag(PARAM_UPDATE_FAST_UPGRADE);
            bl_uart_printf(BL_LOG_USART, "set update_flag = FAST_UPGRADE\r\n");
            bl_debug_commit_and_reset();
            break;

        case '2':
            param_set_update_flag(PARAM_UPDATE_PENDING);
            bl_uart_printf(BL_LOG_USART, "set update_flag = PENDING\r\n");
            bl_debug_commit_and_reset();
            break;

        case '3':
            param_boot_unconfirm();
            param_set_boot_trials(0UL);
            bl_uart_printf(BL_LOG_USART, "cleared boot_confirm, trials=0\r\n");
            bl_debug_commit_and_reset();
            break;

        case '4':
            param_boot_unconfirm();
            param_set_boot_trials(PARAM_BOOT_TRIALS_MAX);
            bl_uart_printf(BL_LOG_USART, "cleared boot_confirm, trials=%u\r\n",
                           (unsigned)PARAM_BOOT_TRIALS_MAX);
            bl_debug_commit_and_reset();
            break;

        case '5':
            param_set_boot_slot(PART_SLOT_OTHER(param_store_get()->boot_slot));
            bl_uart_printf(BL_LOG_USART, "boot_slot -> %lu\r\n",
                           (unsigned long)param_store_get()->boot_slot);
            bl_debug_commit_and_reset();
            break;

        default:
            /* 其它字符视为敲错；回车换行忽略。 */
            if ((c != '\r') && (c != '\n')) {
                bl_uart_printf(BL_LOG_USART, "unknown command '%c'\r\n", (char)c);
                bl_debug_help();
            }
            break;
        }
    }

    bl_uart_printf(BL_LOG_USART, "window timeout, booting\r\n");
}

#endif /* BL_DEBUG_CMDS */
