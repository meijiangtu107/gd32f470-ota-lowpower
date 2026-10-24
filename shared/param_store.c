/*
 * 参数存储：整页读-改-写 + 主备双副本。
 *
 * 【双副本到底能抗什么】
 * 主块(+0x000) 和 备份块(+0x100) 在同一个 4KB 页里，写的时候两份写同一份值
 * （镜像语义）。
 *
 * 能抗：写入过程中某字节失败、位翻转、某一份被写坏 -> 另一份还能用。
 * 不能抗：擦除那一瞬间断电 —— 一次 fmc_page_erase 把两份一起抹掉，
 *         此刻掉电就只能落默认值。
 * 想连擦除窗口一起抗住，得让主备各占一页交替写（8KB）。参数页只有 4KB，
 * 做不到，除非给参数区扩容，这里按现状取舍。
 *
 * 【本文件在 shared/ 里】APP 和 Bootloader 都要读写参数页（BL 要动
 * boot_slot / boot_trials / update_flag），所以这份逻辑必须两边共用一份，
 * 不能各写一套 —— 否则同一套 Flash 布局有两份实现，改一处忘另一处就出事。
 * 代价是这里不能调 APP 的 log_printf（那会让 shared/ 依赖 APP）。
 * 所有函数都用返回值表达结果，日志由调用方自己打。
 */

#include <string.h>

#include "gd32f4xx.h"
#include "param_store.h"
#include "partition.h"
#include "fmc.h"
#include "crc.h"

/* 4KB 整页缓存。必须是 static —— 放栈上会把栈压爆 */
static uint8_t     s_page_cache[PART_PARAM_SIZE];
static app_param_t s_param;
static uint8_t     s_dirty;

/*============================================================================
 * 内部：校验 / 默认值 / 迁移
 *============================================================================*/

/* 一份参数块是否完整可信 */
static int param_block_valid(const app_param_t *p)
{
    if (p->magic != PARAM_MAGIC) {
        return 0;
    }
    if (p->tail_magic != PARAM_TAIL_MAGIC) {
        return 0;
    }
    if (crc32_calc((const uint8_t *)p, PARAM_CRC_SPAN) != p->param_crc32) {
        return 0;
    }
    return 1;
}

static void param_set_defaults(app_param_t *p)
{
    memset(p, 0, sizeof(*p));

    p->magic        = PARAM_MAGIC;
    p->version      = PARAM_VERSION_V3;
    p->update_flag  = PARAM_UPDATE_IDLE;
    p->app1_addr    = PART_SLOT_A_BASE;
    p->app2_addr    = PART_SLOT_B_BASE;
    p->last_error   = PARAM_ERR_NONE;

    p->device_id        = 0x0001UL;
    p->baudrate         = 0x14UL;        /* 14 -> 115200（RS485 业务口） */
    p->ch0_ratio        = 1.0f;
    p->ch0_threshold    = 3000.0f;
    p->ch1_ratio        = 1.0f;
    p->ch1_threshold    = 3000.0f;
    p->alert_mode       = 0x02UL;        /* 02 -> 不主动上报 */
    p->report_interval  = 0x01UL;
    p->firmware_version = 0x02010000UL;

    p->dac_value        = 0UL;

    /* 出厂镜像本来就是好的，直接标"已确认"。
     * 若标成未确认，Bootloader 会把它当成没自证的新固件，数 3 次就回滚。 */
    p->boot_slot        = PART_SLOT_A;
    p->boot_trials      = 0UL;
    p->boot_confirm     = PARAM_BOOT_CONFIRM_MAGIC;

    p->tail_magic       = PARAM_TAIL_MAGIC;
    p->param_crc32      = crc32_calc((const uint8_t *)p, PARAM_CRC_SPAN);
}

/* v2 -> v3 迁移。返回 1 表示做了迁移，0 表示不需要。
 *
 * 为什么能直接读 v2：v3 的 4 个新字段是填进原 reserved 的，
 * 结构体大小和 CRC 范围都没变，所以 v2 的块在 v3 代码里照样能过 CRC 校验，
 * 只有 version 这一个字段不同 —— 正好用来判断"这是老参数"。 */
static int param_migrate_v2_to_v3(app_param_t *p)
{
    if (p->version != PARAM_VERSION_V2) {
        return 0;
    }

    p->version      = PARAM_VERSION_V3;
    p->app1_addr    = PART_SLOT_A_BASE;
    p->app2_addr    = PART_SLOT_B_BASE;

    /* 老设备本来就一直在 A 槽稳定运行，标成已确认，免得被误判回滚 */
    p->boot_slot    = PART_SLOT_A;
    p->boot_confirm = PARAM_BOOT_CONFIRM_MAGIC;
    p->boot_trials  = 0UL;

    /* v2 的 PENDING 对应的暂存区与现在的 Download 区语义不同，不能沿用 */
    if (p->update_flag == PARAM_UPDATE_PENDING) {
        p->update_flag = PARAM_UPDATE_FAILED;
    }

    return 1;
}

/*============================================================================
 * 生命周期
 *============================================================================*/

int param_store_init(void)
{
    app_param_t main_blk;
    app_param_t backup_blk;
    int         main_ok;
    int         backup_ok;

    memcpy(&main_blk,   (const void *)(uintptr_t)PARAM_MAIN_ADDR,   sizeof(main_blk));
    memcpy(&backup_blk, (const void *)(uintptr_t)PARAM_BACKUP_ADDR, sizeof(backup_blk));

    main_ok   = param_block_valid(&main_blk);
    backup_ok = param_block_valid(&backup_blk);

    if (main_ok) {
        s_param = main_blk;
    } else if (backup_ok) {
        /* 主块坏了，用备份顶上，并立刻回写把主块修好 */
        s_param = backup_blk;
        s_dirty = 1;
        (void)param_store_save();
        return PARAM_STORE_REPAIRED;
    } else {
        /* 两份都不可信（比如全新的芯片，整页还是 0xFF）→ 落默认值 */
        param_set_defaults(&s_param);
        s_dirty = 1;
        (void)param_store_save();
        return PARAM_STORE_DEFAULTED;
    }

    if (param_migrate_v2_to_v3(&s_param) != 0) {
        s_dirty = 1;
        (void)param_store_save();
        return PARAM_STORE_MIGRATED;
    }

    s_dirty = 0;
    return PARAM_STORE_OK;
}

int param_store_save(void)
{
    /* 常量字段和 CRC 每次重算，调用者改了什么都不会破坏一致性 */
    s_param.magic       = PARAM_MAGIC;
    s_param.version     = PARAM_VERSION_V3;
    s_param.tail_magic  = PARAM_TAIL_MAGIC;
    s_param.param_crc32 = crc32_calc((const uint8_t *)&s_param, PARAM_CRC_SPAN);

    /* 1. 整页读进 RAM。
     *    告警记录区和 BL 日志区就是这样被"顺便带上来"的，
     *    下一步整页擦掉它们之后还得靠这份缓存还原。 */
    if (fmc_read(PART_PARAM_BASE, s_page_cache, PART_PARAM_SIZE) != FMC_OK) {
        return PARAM_STORE_ERR;
    }

    /* 2. 只改目标字段：主块 + 备份块，同一份内容写两遍 */
    memcpy(&s_page_cache[PARAM_MAIN_OFFSET],   &s_param, sizeof(s_param));
    memcpy(&s_page_cache[PARAM_BACKUP_OFFSET], &s_param, sizeof(s_param));

    /* 3. 整页擦。整页 4KB 连同告警区、日志区一起被抹，
     *    但它们的备份就在 s_page_cache 里 */
    if (fmc_erase_pages(PART_PARAM_BASE, PART_PARAM_SIZE) != FMC_OK) {
        return PARAM_STORE_ERR;
    }

    /* 4. 整页写回 */
    if (fmc_program(PART_PARAM_BASE, s_page_cache, PART_PARAM_SIZE) != FMC_OK) {
        return PARAM_STORE_ERR;
    }

    s_dirty = 0;
    return PARAM_STORE_OK;
}

int param_store_factory_reset(void)
{
    /* 只重置参数本身，告警记录和 BL 日志保留 ——
     * 它们是"发生过的事实"，不该被恢复出厂设置抹掉 */
    param_set_defaults(&s_param);
    s_dirty = 1;
    return param_store_save();
}

const app_param_t *param_store_get(void)
{
    return &s_param;
}

int param_store_is_dirty(void)
{
    return (s_dirty != 0U) ? 1 : 0;
}

/*============================================================================
 * 业务参数存取
 *============================================================================*/

uint32_t param_get_device_id(void)
{
    return s_param.device_id;
}

void param_set_device_id(uint32_t id)
{
    if (id != 0UL) {
        s_param.device_id = id;
        s_dirty = 1;
    }
}

uint32_t param_get_baudrate(void)
{
    return s_param.baudrate;
}

void param_set_baudrate(uint32_t baudrate)
{
    s_param.baudrate = baudrate;
    s_dirty = 1;
}

float param_get_ratio(uint8_t channel)
{
    if (channel == 0U) {
        return s_param.ch0_ratio;
    }
    if (channel == 1U) {
        return s_param.ch1_ratio;
    }
    return 1.0f;
}

void param_set_ratio(uint8_t channel, float ratio)
{
    if (channel == 0U) {
        s_param.ch0_ratio = ratio;
        s_dirty = 1;
    } else if (channel == 1U) {
        s_param.ch1_ratio = ratio;
        s_dirty = 1;
    }
}

float param_get_threshold(uint8_t channel)
{
    if (channel == 0U) {
        return s_param.ch0_threshold;
    }
    if (channel == 1U) {
        return s_param.ch1_threshold;
    }
    return 0.0f;
}

void param_set_threshold(uint8_t channel, float threshold)
{
    if (channel == 0U) {
        s_param.ch0_threshold = threshold;
        s_dirty = 1;
    } else if (channel == 1U) {
        s_param.ch1_threshold = threshold;
        s_dirty = 1;
    }
}

uint32_t param_get_alert_mode(void)
{
    return s_param.alert_mode;
}

void param_set_alert_mode(uint32_t mode)
{
    s_param.alert_mode = mode;
    s_dirty = 1;
}

uint32_t param_get_report_interval(void)
{
    return s_param.report_interval;
}

void param_set_report_interval(uint32_t interval)
{
    s_param.report_interval = interval;
    s_dirty = 1;
}

uint32_t param_get_dac_value(void)
{
    return s_param.dac_value;
}

void param_set_dac_value(uint32_t value)
{
    s_param.dac_value = value;
    s_dirty = 1;
}

/*============================================================================
 * 告警记录
 *============================================================================*/

uint32_t param_alert_count(void)
{
    alert_record_t rec;
    uint32_t       addr;
    uint32_t       i;

    for (i = 0UL; i < ALERT_RECORD_MAX; i++) {
        addr = ALERT_RECORD_START_ADDR + i * (uint32_t)sizeof(rec);

        /* 空槽是全 0xFF，不是 0。
         * 只判 timestamp == 0 会把擦干净的槽（timestamp = 0xFFFFFFFF）
         * 也当成有效记录，于是一页空白被数成 10 条满记录。
         * 判据必须和 param_alert_add() 里的 fmc_is_erased() 一致。 */
        if (fmc_is_erased(addr, (uint32_t)sizeof(rec)) == 1) {
            break;
        }

        memcpy(&rec, (const void *)(uintptr_t)addr, sizeof(rec));
        if (rec.timestamp == 0UL) {
            break;
        }
    }
    return i;
}

int param_alert_add(const alert_record_t *rec)
{
    uint32_t idx;
    uint32_t addr;

    if (rec == 0) {
        return PARAM_STORE_ERR;
    }

    idx = param_alert_count();
    if (idx >= ALERT_RECORD_MAX) {
        return PARAM_STORE_ERR;
    }

    addr = ALERT_RECORD_START_ADDR + idx * (uint32_t)sizeof(alert_record_t);

    /* Flash 只能把 1 写成 0。如果这个槽里还留着旧数据，
     * 直接写会变成"新旧按位与"，结果静默损坏、硬件不报错。
     * 所以先确认它是干净的 0xFF。 */
    if (fmc_is_erased(addr, (uint32_t)sizeof(alert_record_t)) != 1) {
        return PARAM_STORE_ERR;
    }

    if (fmc_program(addr, (const uint8_t *)rec, (uint32_t)sizeof(alert_record_t)) != FMC_OK) {
        return PARAM_STORE_ERR;
    }

    return PARAM_STORE_OK;
}

int param_alert_get(uint32_t index, alert_record_t *out)
{
    if (out == 0) {
        return PARAM_STORE_ERR;
    }
    if (index >= param_alert_count()) {
        return PARAM_STORE_ERR;
    }

    memcpy(out,
           (const void *)(uintptr_t)(ALERT_RECORD_START_ADDR + index * (uint32_t)sizeof(*out)),
           sizeof(*out));
    return PARAM_STORE_OK;
}

int param_alert_clear(void)
{
    /* 告警区和参数块在同一个 4KB 页里，没有任何办法单独擦它。
     * 对告警区单独做"擦一段、写回一段"是参数存储最典型的错误：
     * 页对齐后实际擦掉的是整个参数页，只把告警段写回，
     * 主/备参数块就跟着一起没了。
     * 所以必须整页读-改-写：只把 +0x200 那段填回 0xFF。 */
    if (fmc_read(PART_PARAM_BASE, s_page_cache, PART_PARAM_SIZE) != FMC_OK) {
        return PARAM_STORE_ERR;
    }

    memset(&s_page_cache[PARAM_ALERT_OFFSET], 0xFF, ALERT_RECORD_TOTAL_SIZE);

    if (fmc_erase_pages(PART_PARAM_BASE, PART_PARAM_SIZE) != FMC_OK) {
        return PARAM_STORE_ERR;
    }
    if (fmc_program(PART_PARAM_BASE, s_page_cache, PART_PARAM_SIZE) != FMC_OK) {
        return PARAM_STORE_ERR;
    }

    return PARAM_STORE_OK;
}

/*============================================================================
 * A/B 槽位
 *============================================================================*/

uint32_t param_get_boot_slot(void)
{
    return s_param.boot_slot;
}

void param_set_boot_slot(uint32_t slot)
{
    s_param.boot_slot = (slot == PART_SLOT_B) ? PART_SLOT_B : PART_SLOT_A;
    s_dirty = 1;
}

/* 只改字段，不落盘。给"一次改多个字段后统一 save"的调用方用 */
void param_boot_set_confirmed(void)
{
    s_param.boot_confirm = PARAM_BOOT_CONFIRM_MAGIC;
    s_param.boot_trials  = 0UL;
    s_dirty = 1;
}

void param_boot_confirm(void)
{
    /* 这是"新固件还活着"的签字。App 每次启动调一次，所以必须自己落盘 ——
     * 万一签字还没写就掉电，Bootloader 会再给一次机会，不影响正确性。 */
    param_boot_set_confirmed();
    (void)param_store_save();
}

void param_set_update_flag(uint32_t flag)
{
    s_param.update_flag = flag;
    s_dirty = 1;
}

void param_set_boot_trials(uint32_t trials)
{
    s_param.boot_trials = trials;
    s_dirty = 1;
}

void param_boot_unconfirm(void)
{
    /* 只清"已确认"这一位，不重置 trials —— trials 由调用方按需设置。
     * 拆开是为了让"切槽"这个动作能一次把几个字段改成想要的样子，
     * 然后只落盘一次。 */
    s_param.boot_confirm = PARAM_BOOT_CONFIRM_NONE;
    s_dirty = 1;
}

/*============================================================================
 * 自测：参数页整页读-改-写的完整性验证，一条命令跑完
 *============================================================================*/

int param_store_selftest(void)
{
    alert_record_t rec;
    alert_record_t back;
    uint8_t        marker[4];
    uint8_t        rd[4];
    uint32_t       saved_id;
    uint32_t       i;

    /* --- 准备：清空告警区，保证靶位是干净 0xFF --- */
    if (param_alert_clear() != PARAM_STORE_OK) {
        return -1;
    }
    if (param_alert_count() != 0UL) {
        return -2;
    }

    /* --- 靶子 1：往告警区（+0x200）写一条记录 --- */
    memset(&rec, 0, sizeof(rec));
    rec.timestamp    = 0x12345678UL;
    rec.channel      = 1U;
    rec.threshold    = 3000.0f;
    rec.actual_value = 3500.0f;
    if (param_alert_add(&rec) != PARAM_STORE_OK) {
        return -3;
    }

    /* --- 靶子 2：BL 日志区（+0x300）。日志区归 Bootloader 管，没有对外 API，
     *     这里直接走整页读-改-写。
     *     靶子取"当前内容的反码"——保证和原值一定不同，这样反复跑这个自测
     *     也不会把上次留下的残留当成本次结果。写之前整页已经擦过，
     *     不用担心"只能 1->0"的限制。 --- */
    memcpy(marker, (const void *)(uintptr_t)PARAM_BLLOG_ADDR, sizeof(marker));
    for (i = 0UL; i < sizeof(marker); i++) {
        marker[i] = (uint8_t)(~marker[i]);
    }

    if (fmc_read(PART_PARAM_BASE, s_page_cache, PART_PARAM_SIZE) != FMC_OK) {
        return -4;
    }
    memcpy(&s_page_cache[PARAM_BLLOG_OFFSET], marker, sizeof(marker));
    if (fmc_erase_pages(PART_PARAM_BASE, PART_PARAM_SIZE) != FMC_OK) {
        return -5;
    }
    if (fmc_program(PART_PARAM_BASE, s_page_cache, PART_PARAM_SIZE) != FMC_OK) {
        return -6;
    }

    /* 立刻回读，确认靶子真的写进去了。
     * 不先验这一步的话，万一写入失败，后面会误判成"存参数把它破坏了" */
    memcpy(rd, (const void *)(uintptr_t)PARAM_BLLOG_ADDR, sizeof(rd));
    if (memcmp(rd, marker, sizeof(marker)) != 0) {
        return -7;
    }

    /* --- 动一下参数，逼出一次完整的整页读-改-写 --- */
    saved_id = param_get_device_id();
    param_set_device_id(saved_id ^ 0x0001UL);
    if (param_store_save() != PARAM_STORE_OK) {
        return -8;
    }

    /* --- 验收 1：告警记录还在不在 --- */
    if (param_alert_count() != 1UL) {
        return -9;
    }
    if (param_alert_get(0UL, &back) != PARAM_STORE_OK) {
        return -10;
    }
    if (back.timestamp != 0x12345678UL) {
        return -11;
    }
    if (back.channel != 1U) {
        return -12;
    }

    /* --- 验收 2：BL 日志区的靶子还在不在 --- */
    memcpy(rd, (const void *)(uintptr_t)PARAM_BLLOG_ADDR, sizeof(rd));
    if (memcmp(rd, marker, sizeof(marker)) != 0) {
        return -13;
    }

    /* --- 收尾：device_id 改回去 --- */
    param_set_device_id(saved_id);
    if (param_store_save() != PARAM_STORE_OK) {
        return -14;
    }

    return PARAM_STORE_OK;
}
