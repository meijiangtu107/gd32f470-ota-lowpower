/**
 * @file    param_store.h
 * @brief   参数存储：整页读-改-写 + 主备双副本
 * @author  meijiangtu
 * @date    2026-10-10
 *
 * 这一层管"参数怎么组织、怎么写进参数页"，
 * 底层擦写交给 shared/fmc.c，结构体定义在 shared/param_layout.h。
 *
 * 【为什么不能直接擦 256 字节】
 * Flash 擦除的最小单位是页（4KB）。想改主参数块，按页擦会把同一页里的
 * 备份块、告警记录区、BL 日志区一起抹掉 —— 这正是旧工程
 * param_manager_save() 的 bug（擦 4KB 只写回 256B）。
 * 正确做法只有一条：整页读进 RAM → 在 RAM 里改 → 整页擦 → 整页写回。
 */

#ifndef PARAM_STORE_H
#define PARAM_STORE_H

#include <stdint.h>
#include "param_layout.h"

/* param_store_init() 的返回值：告诉调用者这次上电的参数是从哪来的 */
#define PARAM_STORE_OK         0
#define PARAM_STORE_REPAIRED   1    /* 主块坏 -> 用备份恢复，并已回写修复 */
#define PARAM_STORE_DEFAULTED  2    /* 两份都坏 -> 重建默认，并已回写 */
#define PARAM_STORE_MIGRATED   3    /* 读到 v2 老参数 -> 已迁移到 v3 */
#define PARAM_STORE_ERR      (-1)

/* ---- 生命周期 ---- */

/* 上电调一次：读参数 -> 校验 -> 坏了就修 -> 必要时迁移版本。
 * 返回上面那几个 PARAM_STORE_xxx，调用者可以据此打日志。 */
int param_store_init(void);

/* 把 RAM 里的参数写回 Flash（整页读-改-写）。0 成功，-1 失败 */
int param_store_save(void);

/* 恢复出厂默认值并立即写回 */
int param_store_factory_reset(void);

/* 只读访问当前参数（RAM 副本）。要改请用下面的 setter */
const app_param_t *param_store_get(void);

/* 是否有改过但还没保存 */
int param_store_is_dirty(void);

/* ---- 业务参数 ----
 * setter 只改 RAM 副本并置脏标记，不会立刻写 Flash ——
 * 因为一次改好几个参数只该擦写一次。改完统一调 param_store_save()。 */

uint32_t param_get_device_id(void);
void     param_set_device_id(uint32_t id);

uint32_t param_get_baudrate(void);
void     param_set_baudrate(uint32_t baudrate);

/* channel: 0 = CH0, 1 = CH1 */
float    param_get_ratio(uint8_t channel);
void     param_set_ratio(uint8_t channel, float ratio);
float    param_get_threshold(uint8_t channel);
void     param_set_threshold(uint8_t channel, float threshold);

uint32_t param_get_alert_mode(void);
void     param_set_alert_mode(uint32_t mode);

uint32_t param_get_report_interval(void);
void     param_set_report_interval(uint32_t interval);

uint32_t param_get_dac_value(void);
void     param_set_dac_value(uint32_t value);

/* ---- 告警记录（参数页 +0x200，最多 10 条）---- */

/* 追加一条。满了返回 -1。
 * 注意：只往空槽写，不擦除 —— 所以写之前该槽必须是 0xFF。 */
int      param_alert_add(const alert_record_t *rec);

int      param_alert_get(uint32_t index, alert_record_t *out);

/* 已有几条。空槽是 0xFF 不是 0，所以判据用"整槽是否全 FF"，
 * 遇到第一个空槽就停 —— 和 param_alert_add() 的判据一致。 */
uint32_t param_alert_count(void);

/* 清空全部告警。告警区和参数块同页，不能单独擦，
 * 所以这里走整页读-改-写：只把 +0x200 那段填回 0xFF，其余原样保留。 */
int      param_alert_clear(void);

/* ---- A/B 槽位（Step 10 的 Bootloader 用）---- */

uint32_t param_get_boot_slot(void);
void     param_set_boot_slot(uint32_t slot);

/* App 自检通过后调这个：boot_confirm = 已确认，boot_trials = 0。
 * 不调的话，Bootloader 会认为新固件没活过来，累计 3 次后回滚。
 * 这是一次性动作，内部会立刻落盘。 */
void     param_boot_confirm(void);

/* ---- 启动状态机要动的字段 ----
 *
 * 下面这些 setter 只改 RAM 副本 + 置脏标记，**不落盘**。
 * 因为状态机要求"先落盘再动作"（掉电不能丢状态），而一次状态切换
 * 可能要改好几个字段 —— 边改边落盘既慢又可能只落一半。所以设计成：
 * 调用方改完所有字段，再调一次 param_store_save()，让整个切换成为
 * 一个原子动作。
 *
 * ⚠️ 这条约定必须遵守。改完字段忘记 save()，改动就只活在 RAM 里，
 *    复位后消失 —— 而且因为没报错，现象会非常迷惑（"拨了槽又弹回来"）。
 *
 * 读这些字段直接用 param_store_get()->xxx，不再另开 getter。 */

void param_set_update_flag(uint32_t flag);
void param_set_boot_trials(uint32_t trials);

/* 切到新装的槽之后调：把那槽标成"还没自证"。
 * 下次 Bootloader 启动看到这个标记，就开始数 boot_trials。 */
void param_boot_unconfirm(void);

/* 标成"已确认"（不落盘，也不动 trials）。
 * 和 param_boot_confirm() 的区别只在落不落盘 —— 后者是给 App 用的
 * 一次性动作，这个是给"一次改多个字段后统一 save"的调用方用的。 */
void param_boot_set_confirmed(void);

/* ---- 自测 ----
 * 把 Step 7 的验收标准做成可执行的一条命令：
 * 往告警区和 BL 日志区各埋一个靶子 -> 存参数（整页擦写）-> 看靶子还在不在。
 * 返回 PARAM_STORE_OK 表示通过；失败返回负数，绝对值指出死在哪一步。 */
int param_store_selftest(void);

#endif /* PARAM_STORE_H */
