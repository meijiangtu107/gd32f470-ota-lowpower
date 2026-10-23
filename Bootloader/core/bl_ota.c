/* OTA 接收：在字节流中用 magic 序列对齐包头，以 hcrc 与 size 确认，
 * 收够 size 字节后做 CRC32 校验。接收期间只打印单个字符，
 * 多字符输出会阻塞约 3ms，期间到达的字节会丢。 */

#include <string.h>

#include "gd32f4xx.h"

#include "bl_ota.h"
#include "bl_uart.h"
#include "bl_tick.h"

#include "ota_pack.h"
#include "crc.h"
#include "fmc.h"
#include "param_store.h"
#include "app_jump.h"

/* ---- 超时 ---- */

/* 等待上位机开始发送的最长时间 */
#define OTA_SYNC_TIMEOUT_MS    30000U

/* 开始收之后，字节之间允许的最大空闲时间 */
#define OTA_GAP_TIMEOUT_MS     3000U

/* 同步期间的打点间隔 */
#define OTA_SYNC_DOT_MS        5000U

/* 每收多少字节打一个进度点 */
#define OTA_PROGRESS_STEP      8192U

/* ---- magic 序列 ----
 * OTA_MAGIC（0x31544F41）的小端字节序，两处必须一致。 */
static const uint8_t OTA_MAGIC_BYTES[4] = { 0x4FU, 0x54U, 0x41U, 0x31U };

typedef enum {
    OTA_ST_SYNC = 0,    /* 在字节流里找 magic */
    OTA_ST_HEADER,      /* 收满 16 字节包头 */
    OTA_ST_PAYLOAD,     /* 头校验通过，收 size 个固件字节 */
    OTA_ST_DONE,
    OTA_ST_FAIL
} ota_state_t;

/* ---- 接收状态 ---- */
static ota_state_t s_state;
static uint8_t     s_hdr[OTA_HEADER_SIZE];
static uint32_t    s_hdr_len;
static uint32_t    s_magic_led;     /* 已连续匹配了几个 magic 字节 */
static uint32_t    s_size;          /* 头里声明的固件长度 */
static uint32_t    s_got;           /* 已收到的固件字节数 */

/* ---- 固件缓冲 ----
 * 96KB，按槽满容量开辟，位于 ZI 段，不占 Flash。
 * BL 的 RAM 约 101KB，芯片共 192KB。 */
static uint8_t s_fw_buf[OTA_SIZE_MAX];

/*============================================================================
 * 小工具
 *============================================================================*/

/* 从字节流里读一个 32 位小端值。
 * 不指针强转，避免对齐要求与字节序依赖。 */
static uint32_t ota_rd_le32(const uint8_t *p)
{
    return ((uint32_t)p[0])
         | ((uint32_t)p[1] << 8)
         | ((uint32_t)p[2] << 16)
         | ((uint32_t)p[3] << 24);
}

/*============================================================================
 * 头校验：三层里的第 2、3 层
 *============================================================================*/

/* 返回 OTA_OK 表示这个头可信；否则返回负值，调用方会回退重找 */
static int ota_check_header(void)
{
    uint32_t size;
    uint32_t hcrc_stored;
    uint32_t hcrc_calc;

    /* 第 2 层：hcrc，排除碰巧撞上 magic 的假包头。 */
    hcrc_stored = ota_rd_le32(&s_hdr[OTA_HDR_OFF_HCRC]);
    hcrc_calc   = crc32_calc(s_hdr, OTA_HDR_HCRC_SPAN);
    if (hcrc_stored != hcrc_calc) {
        return OTA_ERR_BAD_HEADER;
    }

    /* 第 3 层：size 是否合理。 */
    size = ota_rd_le32(&s_hdr[OTA_HDR_OFF_SIZE]);
    if ((size < OTA_SIZE_MIN) || (size > OTA_SIZE_MAX)) {
        return OTA_ERR_TOO_BIG;
    }

    s_size = size;
    return OTA_OK;
}

/*============================================================================
 * 状态机：每来一个字节喂一次
 *============================================================================*/

static void ota_feed(uint8_t b);

/* 头校验失败时把 hdr[1..15] 重新喂回状态机，窗口右移一格重找。 */
static void ota_resync_from_header(void)
{
    uint8_t  tmp[OTA_HEADER_SIZE];
    uint32_t i;

    memcpy(tmp, s_hdr, OTA_HEADER_SIZE);    /* 先拷贝，避免自覆盖 */

    s_state     = OTA_ST_SYNC;
    s_magic_led = 0UL;
    s_hdr_len   = 0UL;

    /* 只喂 15 字节，最多推进到"头收 15 字节"，不会再次触发头校验。 */
    for (i = 1UL; i < OTA_HEADER_SIZE; i++) {
        ota_feed(tmp[i]);
    }
}

static void ota_feed(uint8_t b)
{
    switch (s_state) {

    /* ---- 第 1 层：在字节流里找 magic ---- */
    case OTA_ST_SYNC:
        if (b == OTA_MAGIC_BYTES[s_magic_led]) {
            s_magic_led++;
            if (s_magic_led == 4UL) {
                /* 4 字节全部匹配，放回头缓冲。 */
                memcpy(s_hdr, OTA_MAGIC_BYTES, 4UL);
                s_hdr_len   = 4UL;
                s_magic_led = 0UL;
                s_state     = OTA_ST_HEADER;
            }
        } else {
            /* "OTA1" 无自重叠，只需判断该字节是否为 'O'。 */
            s_magic_led = (b == OTA_MAGIC_BYTES[0]) ? 1UL : 0UL;
        }
        break;

    /* ---- 收满 16 字节包头后校验 ---- */
    case OTA_ST_HEADER:
        s_hdr[s_hdr_len++] = b;
        if (s_hdr_len == OTA_HEADER_SIZE) {
            if (ota_check_header() == OTA_OK) {
                s_state = OTA_ST_PAYLOAD;
                s_got   = 0UL;
            } else {
                ota_resync_from_header();       /* 假包头，右移一格重找 */
            }
        }
        break;

    /* ---- 收固件：只计数，不做判断 ---- */
    case OTA_ST_PAYLOAD:
        s_fw_buf[s_got++] = b;
        if (s_got >= s_size) {
            s_state = OTA_ST_DONE;
        }
        break;

    default:
        break;
    }
}

/*============================================================================
 * 主流程
 *============================================================================*/

int bl_ota_receive(void)
{
    uint32_t rx_usart  = bl_ota_usart();
    uint32_t start_ms  = bl_tick_ms();
    uint32_t last_ms   = start_ms;
    uint32_t dot_ms    = start_ms;
    uint32_t next_dot  = OTA_PROGRESS_STEP;
    uint8_t  b;

    s_state     = OTA_ST_SYNC;
    s_magic_led = 0UL;
    s_hdr_len   = 0UL;
    s_size      = 0UL;
    s_got       = 0UL;

    bl_uart_printf(BL_LOG_USART,
                   "[ota] waiting for package (magic='OTA1', timeout %us)...\r\n",
                   (unsigned)(OTA_SYNC_TIMEOUT_MS / 1000U));

    for (;;) {
        /* ---- 超时判断 ---- */
        if (s_state == OTA_ST_SYNC) {
            if (bl_tick_elapsed(start_ms, OTA_SYNC_TIMEOUT_MS) != 0) {
                bl_uart_printf(BL_LOG_USART, "\r\n[ota] no package, giving up\r\n");
                return OTA_ERR_TIMEOUT;
            }
            /* 同步阶段打点安全。 */
            if (bl_tick_elapsed(dot_ms, OTA_SYNC_DOT_MS) != 0) {
                dot_ms = bl_tick_ms();
                bl_uart_putc(BL_LOG_USART, '.');
            }
        } else {
            /* 已经开始收，间隔不能过长。 */
            if (bl_tick_elapsed(last_ms, OTA_GAP_TIMEOUT_MS) != 0) {
                bl_uart_printf(BL_LOG_USART,
                               "\r\n[ota] gap timeout (state=%d, got=%lu/%lu)\r\n",
                               (int)s_state,
                               (unsigned long)s_got,
                               (unsigned long)s_size);
                return OTA_ERR_TIMEOUT;
            }
        }

        /* ---- 取字节 ---- */
        if (bl_uart_getc(rx_usart, &b) == 0) {
            continue;
        }
        last_ms = bl_tick_ms();

        ota_feed(b);

        /* ---- 进度点 ----
         * 只打印单个字符，多字符会阻塞约 3ms 导致丢字节。 */
        if ((s_state == OTA_ST_PAYLOAD) && (s_got >= next_dot)) {
            next_dot += OTA_PROGRESS_STEP;
            bl_uart_putc(BL_LOG_USART, '.');
        }

        if (s_state == OTA_ST_DONE) {
            break;
        }
    }

    bl_uart_printf(BL_LOG_USART, "\r\n[ota] received %lu bytes, checking crc...\r\n",
                   (unsigned long)s_got);

    /* ---- 整包校验 ----
     * 失败则整包作废；此时目标槽未做任何改动。 */
    {
        uint32_t crc_stored = ota_rd_le32(&s_hdr[OTA_HDR_OFF_CRC32]);
        uint32_t crc_calc   = crc32_calc(s_fw_buf, s_size);

        if (crc_stored != crc_calc) {
            bl_uart_printf(BL_LOG_USART,
                           "[ota] CRC MISMATCH: stored=0x%08lX calc=0x%08lX\r\n",
                           (unsigned long)crc_stored,
                           (unsigned long)crc_calc);
            return OTA_ERR_BAD_CRC;
        }

        bl_uart_printf(BL_LOG_USART, "[ota] CRC OK (0x%08lX), %lu bytes\r\n",
                       (unsigned long)crc_calc, (unsigned long)s_size);
    }

    return OTA_OK;
}

const uint8_t *bl_ota_buf(void)
{
    return s_fw_buf;
}

uint32_t bl_ota_size(void)
{
    return s_size;
}

/*============================================================================
 * 安装：把 RAM 里的镜像搬进非活动槽
 *
 * 始终不碰正在运行的槽，写完才切换 boot_slot。
 *============================================================================*/

int bl_ota_install(void)
{
    uint32_t cur_slot;
    uint32_t dst_slot;
    uint32_t dst_base;
    uint32_t cur_base;
    uint32_t size;
    uint32_t erase_size;
    uint32_t crc_stored;
    uint32_t crc_calc;

    if (s_state != OTA_ST_DONE) {
        return OTA_ERR_BAD_CRC;         /* 没收到完整包，不该走到这 */
    }

    size = s_size;

    cur_slot = param_store_get()->boot_slot;
    dst_slot = PART_SLOT_OTHER(cur_slot);
    dst_base = PART_SLOT_BASE(dst_slot);
    cur_base = PART_SLOT_BASE(cur_slot);

    bl_uart_printf(BL_LOG_USART,
                   "[ota] installing %lu bytes: slot %lu (0x%08lX) -> slot %lu (0x%08lX)\r\n",
                   (unsigned long)size,
                   (unsigned long)cur_slot, (unsigned long)cur_base,
                   (unsigned long)dst_slot, (unsigned long)dst_base);

    /* ---- 1. 镜像是否为目标槽构建 ----
     * 同时拦截非固件数据和装错槽的情况。 */
    if (app_image_valid(s_fw_buf, dst_base) != 1) {
        if (app_image_valid(s_fw_buf, cur_base) == 1) {
            bl_uart_printf(BL_LOG_USART,
                           "[ota] REFUSED: image is built for slot %lu, which is the\r\n"
                           "      slot CURRENTLY RUNNING. Installing it would overwrite\r\n"
                           "      ourselves. Send the image built for slot %lu.\r\n",
                           (unsigned long)cur_slot, (unsigned long)dst_slot);
        } else {
            bl_uart_printf(BL_LOG_USART,
                           "[ota] REFUSED: image is not a valid firmware for slot %lu\r\n",
                           (unsigned long)dst_slot);
        }
        return OTA_ERR_BAD_HEADER;
    }

    /* ---- 2. 擦目标槽 ----
     * 按镜像大小擦页，不必擦满 96KB；尾部残留旧字节无害，
     * 向量表校验只看开头，CRC 只覆盖 size 字节。 */
    erase_size = (size + PART_PAGE_SIZE - 1UL) & ~(PART_PAGE_SIZE - 1UL);

    bl_uart_printf(BL_LOG_USART,
                   "[ota] erasing 0x%08lX..0x%08lX (%lu pages, ~%lu ms)...\r\n",
                   (unsigned long)dst_base,
                   (unsigned long)(dst_base + erase_size - 1UL),
                   (unsigned long)(erase_size / PART_PAGE_SIZE),
                   (unsigned long)((erase_size / PART_PAGE_SIZE) * 50UL));

    if (fmc_erase_pages(dst_base, erase_size) != FMC_OK) {
        bl_uart_printf(BL_LOG_USART, "[ota] erase FAILED\r\n");
        return OTA_ERR_BAD_HEADER;
    }

    /* ---- 3. 写进槽 ---- */
    bl_uart_printf(BL_LOG_USART, "[ota] writing %lu bytes...\r\n", (unsigned long)size);

    if (fmc_program(dst_base, s_fw_buf, size) != FMC_OK) {
        bl_uart_printf(BL_LOG_USART, "[ota] write FAILED\r\n");
        return OTA_ERR_BAD_HEADER;
    }

    /* ---- 4. 对槽内复算 CRC ----
     * 收包时的校验只证明传输正确，此处复算证明写入 Flash 正确。 */
    crc_stored = ota_rd_le32(&s_hdr[OTA_HDR_OFF_CRC32]);
    crc_calc   = crc32_calc((const uint8_t *)(uintptr_t)dst_base, size);

    if (crc_calc != crc_stored) {
        bl_uart_printf(BL_LOG_USART,
                       "[ota] VERIFY FAILED: slot crc=0x%08lX, expect 0x%08lX\r\n",
                       (unsigned long)crc_calc, (unsigned long)crc_stored);
        return OTA_ERR_BAD_CRC;
    }

    bl_uart_printf(BL_LOG_USART, "[ota] verify OK (0x%08lX)\r\n",
                   (unsigned long)crc_calc);

    /* ---- 5. 切槽 ----
     * 三个字段改完只落盘一次。 */
    param_set_boot_slot(dst_slot);

    /* 新槽未自证；下次启动开始计 boot_trials，达到上限则回滚。 */
    param_boot_unconfirm();
    param_set_boot_trials(0UL);
    param_set_update_flag(PARAM_UPDATE_IDLE);

    if (param_store_save() != PARAM_STORE_OK) {
        /* 落盘失败则中止，避免槽指针只留在 RAM。 */
        bl_uart_printf(BL_LOG_USART, "[ota] cannot persist boot_slot, aborting\r\n");
        return OTA_ERR_BAD_HEADER;
    }

    bl_uart_printf(BL_LOG_USART,
                   "[ota] installed. boot_slot %lu -> %lu, resetting...\r\n",
                   (unsigned long)cur_slot, (unsigned long)dst_slot);
    bl_delay_ms(50UL);

    NVIC_SystemReset();

    /* 到不了这里 */
    return OTA_OK;
}
