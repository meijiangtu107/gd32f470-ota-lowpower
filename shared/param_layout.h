/*
 * 参数页布局（唯一来源，APP 与 Bootloader 共用）。
 *
 * 参数页 4KB 的内部划分：
 *   +0x000  主参数块     256B   <- 平时读这块
 *   +0x100  备份参数块   256B   <- 主块坏了读这块
 *   +0x200  APP 告警记录区
 *   +0x300  BL 环形日志区（8 条 x 32B）
 *
 * 为什么主备双副本：参数页是设备唯一的持久状态，它坏了一切都乱。
 * 主块 CRC 不过 -> 用备份；两块都坏 -> 重建默认并记日志。
 *
 * 关键不变式（A/B 回滚的底气就在这里）：
 *   写参数时"主块 = 新状态，备份块 = 上一份状态"，
 *   所以无论写到哪一步掉电，恢复出来要么是完整的新、要么是完整的旧，
 *   绝不会出现中间态。
 *
 * 结构体布局是刻意钉死的：新加字段只能填进 reserved 数组里，
 * 不许插在中间、不许删字段。否则 Flash 偏移全变，
 * 现场设备里已经存好的参数会全部认不出来。
 */

#ifndef SHARED_PARAM_LAYOUT_H
#define SHARED_PARAM_LAYOUT_H

#include <stdint.h>
#include <stddef.h>
#include "partition.h"

/* ---- 参数页内部偏移 ---- */
#define PARAM_MAIN_OFFSET     0x000UL
#define PARAM_BACKUP_OFFSET   0x100UL
#define PARAM_ALERT_OFFSET    0x200UL
#define PARAM_BLLOG_OFFSET    0x300UL

#define PARAM_BLOCK_SIZE      0x100UL               /* 主/备参数块各 256B */

#define PARAM_MAIN_ADDR       (PART_PARAM_BASE + PARAM_MAIN_OFFSET)
#define PARAM_BACKUP_ADDR     (PART_PARAM_BASE + PARAM_BACKUP_OFFSET)
#define PARAM_ALERT_ADDR      (PART_PARAM_BASE + PARAM_ALERT_OFFSET)
#define PARAM_BLLOG_ADDR      (PART_PARAM_BASE + PARAM_BLLOG_OFFSET)

/* ---- BL 环形日志 ---- */
#define PARAM_BLLOG_ENTRY_SIZE   32UL
#define PARAM_BLLOG_ENTRY_COUNT  8UL

/* ---- 魔术字 ---- */
#define PARAM_MAGIC              0x5AA5C33CUL
#define PARAM_TAIL_MAGIC         0xA5A5C3C3UL

/* ---- 版本 ----
 * V2 是老设备的版本，只在迁移判断里出现一次；新设备一律写 V3。 */
#define PARAM_VERSION_V2         0x00020001UL
#define PARAM_VERSION_V3         0x00030001UL

/* ---- 更新标志 ---- */
#define PARAM_UPDATE_IDLE          0x00000000UL
#define PARAM_UPDATE_FAST_UPGRADE  0x55AA55AAUL    /* APP 请求升级，BL 见到就进升级态 */
#define PARAM_UPDATE_PENDING       0xAA55AA55UL    /* Download 里有完整镜像，待安装 */
#define PARAM_UPDATE_FAILED        0xDEAD0001UL    /* 升级失败 */

/* ---- 启动确认（A/B 回滚的开关）----
 * 新固件跑起来后自己写 CONFIRM；不写就一直累计 trials，
 * 到 PARAM_BOOT_TRIALS_MAX 次仍未确认 -> BL 拨回旧槽。
 * 判据不是"检测到错误"，而是"新固件没证明自己活着"。 */
#define PARAM_BOOT_CONFIRM_MAGIC 0x434F4E46UL       /* "CONF" */
#define PARAM_BOOT_CONFIRM_NONE  0x00000000UL
#define PARAM_BOOT_TRIALS_MAX    3U

/* ---- 错误码 ---- */
#define PARAM_ERR_NONE           0UL
#define PARAM_ERR_MAGIC          1UL
#define PARAM_ERR_CRC            2UL
#define PARAM_ERR_TAIL           3UL
#define PARAM_ERR_VERSION        4UL

/* ---- 参数结构体 ----
 * 共 64 个 uint32 = 256B，正好一个参数块。
 * param_crc32 落在偏移 248，CRC 覆盖范围就是它之前的 248 字节。 */
typedef struct {
    /* 共享头：APP 与 Bootloader 必须完全一致 */
    uint32_t magic;              /* 0x5AA5C33C */
    uint32_t version;            /* PARAM_VERSION_V3 */
    uint32_t update_flag;        /* IDLE / FAST_UPGRADE / PENDING / FAILED */
    uint32_t app_size;           /* 待安装镜像长度 */
    uint32_t app_crc32;          /* 待安装镜像整包 CRC32 */
    uint32_t app1_addr;          /* = PART_SLOT_A_BASE */
    uint32_t app2_addr;          /* = PART_SLOT_B_BASE */
    uint32_t update_counter;     /* 升级成功计数 */
    uint32_t fail_counter;       /* 升级失败/回滚计数 */
    uint32_t last_error;         /* 最后错误码 */
    uint32_t log_write_index;    /* BL 日志写指针 */

    /* 业务参数 */
    uint32_t device_id;
    uint32_t baudrate;
    float    ch0_ratio;
    float    ch0_threshold;
    float    ch1_ratio;
    float    ch1_threshold;
    uint32_t alert_mode;
    uint32_t report_interval;
    uint32_t firmware_version;

    /* --- v3 新增：占原 reserved[0..3] 的位置，偏移一个字都没动 --- */
    uint32_t dac_value;          /* DAC 持久值（原 reserved[0]） */
    uint32_t boot_slot;          /* 0=Slot A, 1=Slot B —— 启动谁 */
    uint32_t boot_trials;        /* 当前槽未确认启动次数 */
    uint32_t boot_confirm;       /* = PARAM_BOOT_CONFIRM_MAGIC 表示已确认 */

    uint32_t reserved[38];       /* 原 42 个，用掉 4 个 */

    /* 校验尾 */
    uint32_t param_crc32;        /* CRC32，覆盖本字段之前的所有字节 */
    uint32_t tail_magic;         /* 0xA5A5C3C3 */
} app_param_t;

/* ---- 告警记录 ----
 * 存在参数页 +0x200，最多 10 条。 */
typedef struct {
    uint32_t timestamp;
    uint8_t  channel;
    uint8_t  reserved[3];        /* 补齐到 4 字节，避免非对齐访问 */
    float    threshold;
    float    actual_value;
} alert_record_t;

#define ALERT_RECORD_MAX        10UL
#define ALERT_RECORD_START_ADDR PARAM_ALERT_ADDR
#define ALERT_RECORD_TOTAL_SIZE (ALERT_RECORD_MAX * sizeof(alert_record_t))

/* ---- CRC 覆盖范围 ----
 * 用 offsetof 算，不写死 248 —— 结构体改了范围自动跟着变。 */
#define PARAM_CRC_SPAN          ((uint32_t)offsetof(app_param_t, param_crc32))

/* ---- 编译期自检 ---- */
typedef char param_assert_size[(sizeof(app_param_t) == PARAM_BLOCK_SIZE) ? 1 : -1];
typedef char param_assert_crc_span[(PARAM_CRC_SPAN == 248UL) ? 1 : -1];

#endif /* SHARED_PARAM_LAYOUT_H */
