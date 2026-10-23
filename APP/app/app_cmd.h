/* 命令分发、自动上报、告警记录 */

#ifndef APP_CMD_H
#define APP_CMD_H

#include <stdint.h>

#include "app_protocol.h"

/* 上电初始化：从参数页恢复上报间隔等状态 */
void app_cmd_init(void);

/* 分发一帧命令，内部完成设备 ID、帧类型过滤及自动上报期间的应答限制 */
void app_cmd_dispatch(const app_frame_t *frame);

/* 调度器 100ms 调一次：自动上报计时 + 告警边沿检测（非阻塞） */
void app_cmd_tick(void);

int app_cmd_is_auto_report_enabled(void);

/* 设备时间（Unix 秒），RTC 不可用时退回上电秒计数 */
uint32_t app_cmd_get_utc_seconds(void);

#endif /* APP_CMD_H */
