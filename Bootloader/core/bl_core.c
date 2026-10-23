/* Bootloader 启动核心：读参数页，判状态，跳槽或回滚。 */

#include "gd32f4xx.h"

#include "bl_core.h"
#include "bl_uart.h"
#include "bl_tick.h"
#include "bl_debug.h"
#include "bl_ota.h"

#include "partition.h"
#include "param_store.h"
#include "app_jump.h"
#include "ota_pack.h"

/* 报错循环间隔（ms） */
#define BL_ERR_REPEAT_MS   2000U

static void bl_stop_with_error(const char *why)
{
    for (;;) {
        bl_uart_printf(BL_LOG_USART, "BL ERROR: %s\r\n", why);
        bl_delay_ms(BL_ERR_REPEAT_MS);
    }
}

/* 回滚到另一个槽，不返回。前提是另一槽存在有效固件。 */
static void bl_rollback(const char *why)
{
    uint32_t cur   = param_store_get()->boot_slot;
    uint32_t other = PART_SLOT_OTHER(cur);

    bl_uart_printf(BL_LOG_USART, "ROLLBACK (%s): slot %lu -> slot %lu\r\n",
                   why, (unsigned long)cur, (unsigned long)other);

    if (app_vector_valid(PART_SLOT_BASE(other)) != 1) {
        bl_stop_with_error("rollback target has no valid firmware");
    }

    /* 三个字段改完只落盘一次，必须显式 save()。 */
    param_set_boot_slot(other);

    /* 旧固件有效，标记为已确认，避免下次启动再回滚。 */
    param_boot_set_confirmed();

    if (param_store_save() != PARAM_STORE_OK) {
        /* 落盘失败则停住报错，避免槽指针只留在 RAM。 */
        bl_stop_with_error("rollback cannot be persisted");
    }

    bl_uart_printf(BL_LOG_USART, "rollback saved\r\n");
    bl_delay_ms(50UL);

    /* 复位让启动状态机从第 1 步重走。 */
    NVIC_SystemReset();
    for (;;) {
    }
}

void bl_core_run(void)
{
    int      pr;
    uint32_t flag;
    uint32_t slot;
    uint32_t base;
    uint32_t trials;

    bl_uart_printf(BL_LOG_USART, "\r\n=== ximenzi Bootloader v0.2 ===\r\n");

    /* ---- 第 1 步：读参数（主/备双副本择优，坏哪份修哪份）---- */
    pr = param_store_init();
    bl_uart_printf(BL_LOG_USART, "param -> %d (0=ok 1=repaired 2=default 3=migrated)\r\n", pr);

    flag = param_store_get()->update_flag;

#if BL_DEBUG_CMDS
    /* 调试命令窗口：收到命令即落盘并复位，超时或按 '0' 继续。
     * 参数页有 CRC 保护，直接改字段会被判为主块损坏并回滚到备份。
     * 若已收到升级请求则跳过窗口；窗口可能改标志，跑完重读一次。 */
    if (flag != PARAM_UPDATE_FAST_UPGRADE) {
        bl_debug_window();
        flag = param_store_get()->update_flag;
    }
#endif

    /* ---- 第 2 步：FAST_UPGRADE —— App 请求进升级态 ---- */
    if (flag == PARAM_UPDATE_FAST_UPGRADE) {
        int r;

        /* 先清标志再干活，否则升级中复位会永久停在升级态。 */
        param_set_update_flag(PARAM_UPDATE_IDLE);
        (void)param_store_save();

        bl_uart_printf(BL_LOG_USART, "upgrade requested, entering OTA mode\r\n");

        /* 等待上位机发包，阻塞，内部有超时。
         * 只收进 RAM，全程不碰 Flash，目标槽未改动。 */
        r = bl_ota_receive();

        if (r == OTA_OK) {
            bl_uart_printf(BL_LOG_USART,
                           "[ota] package ready: %lu bytes verified in RAM\r\n",
                           (unsigned long)bl_ota_size());

            /* 装入非活动槽；成功则切槽并复位（不返回），
             * 失败返回负数，照常启动旧固件。 */
            r = bl_ota_install();
            bl_uart_printf(BL_LOG_USART,
                           "[ota] install failed (err %d) -> normal boot\r\n", r);
        } else {
            bl_uart_printf(BL_LOG_USART,
                           "[ota] receive failed (err %d) -> normal boot\r\n", r);
        }
    }

    /* ---- 第 3 步：PENDING —— Download 区有完整镜像待安装 ---- */
    if (flag == PARAM_UPDATE_PENDING) {
        bl_uart_printf(BL_LOG_USART, "PENDING: image staged in download area\r\n");
        /* 校验 Download -> 装非活动槽 -> 切 boot_slot -> 复位。
         * 掉电后 PENDING 仍在，下次启动重来（幂等）。 */
        param_set_update_flag(PARAM_UPDATE_FAILED);
        (void)param_store_save();
        bl_uart_printf(BL_LOG_USART, "install not implemented yet -> marked FAILED\r\n");
    }

    slot = param_store_get()->boot_slot;
    base = PART_SLOT_BASE(slot);

    /* ---- 第 4 步：回滚判定 —— 新固件是否已自证 ----
     * 判据为 boot_confirm 是否签字，而非是否检测到错误。 */
    if (param_store_get()->boot_confirm != PARAM_BOOT_CONFIRM_MAGIC) {
        trials = param_store_get()->boot_trials;

        bl_uart_printf(BL_LOG_USART, "slot %lu not confirmed (trials=%lu/%u)\r\n",
                       (unsigned long)slot,
                       (unsigned long)trials,
                       (unsigned)PARAM_BOOT_TRIALS_MAX);

        if (trials >= PARAM_BOOT_TRIALS_MAX) {
            /* 给够机会仍未签字，说明该槽固件无法启动或运行即崩。 */
            bl_rollback("new firmware never confirmed");
            /* 不返回 */
        }

        /* 未到上限：再给一次机会并记录尝试次数。 */
        param_set_boot_trials(trials + 1UL);
        (void)param_store_save();
    }

    /* ---- 第 5 步：活动槽里是否有可运行的固件 ---- */
    if (app_vector_valid(base) != 1) {
        bl_uart_printf(BL_LOG_USART, "slot %lu has no valid firmware\r\n", (unsigned long)slot);
        /* 另一槽有效则切换过去。 */
        bl_rollback("active slot invalid");
        /* 不返回 */
    }

    /* ---- 第 6 步：交接。先关串口，APP 会自行重新配置 ---- */
    bl_uart_printf(BL_LOG_USART, "jumping to 0x%08lX\r\n", (unsigned long)base);
    bl_delay_ms(50UL);          /* 让串口把最后一行发完再关 */
    bl_uart_deinit();

    (void)app_jump(base);

    /* app_jump 仅在向量表非法时返回；走到这里说明异常，停住报错。 */
    bl_stop_with_error("app_jump returned");
}
