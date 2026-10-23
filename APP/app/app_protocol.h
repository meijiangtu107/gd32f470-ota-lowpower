/* 帧格式：A5B6 | dev_id(2) | type(1) | cmd(2) | len(1) | ver(1)=0x02 | payload | CRC16(2) | B6A5
 * 二进制总长 13+len，整帧按 ASCII 十六进制（1 字节 -> 2 个大写字符）传输。
 * 多字节字段及 CRC 一律大端（高字节在前），收发两个方向相同。 */

#ifndef APP_PROTOCOL_H
#define APP_PROTOCOL_H

#include <stdint.h>

/* ---- 帧结构 ---- */
#define AP_HEADER_START         0xA5B6U
#define AP_TAIL_END             0xB6A5U
#define AP_PROTOCOL_VERSION     0x02U

/* ---- 帧类型 ---- */
#define AP_TYPE_COMMAND         0x01U   /* 命令下发（上位机 -> 设备） */
#define AP_TYPE_RESPONSE        0x02U   /* 应答（设备 -> 上位机） */
#define AP_TYPE_HEARTBEAT       0x05U   /* 心跳 */
#define AP_TYPE_ERROR           0xFFU   /* 错误应答 */

/* ---- 广播 ---- */
#define AP_BROADCAST_ID         0xFFFFU

/* ---- 特殊命令字 ---- */
#define AP_CMD_HEARTBEAT_DEVICE 0x8888U
#define AP_CMD_BROADCAST_FIND   0xFFFFU
#define AP_CMD_ERROR_RESPONSE   0xEEEEU

/* ---- 系统管理类 ---- */
#define AP_CMD_SYS_REBOOT           0x0101U
#define AP_CMD_SYS_QUERY_VERSION    0x0104U
#define AP_CMD_SYS_SET_TIME         0x0105U
#define AP_CMD_SYS_QUERY_TIME       0x0106U
#define AP_CMD_SYS_SET_ID           0x01A1U
#define AP_CMD_SYS_SET_BAUDRATE     0x01A2U
#define AP_CMD_SYS_QUERY_ID         0x0111U
#define AP_CMD_SYS_QUERY_BAUDRATE   0x0112U

/* ---- 数据类 ---- */
#define AP_CMD_DATA_QUERY_CH0       0x0201U
#define AP_CMD_DATA_QUERY_CH1       0x0202U
#define AP_CMD_DATA_QUERY_CH2       0x0221U
#define AP_CMD_DATA_SET_RATIO_CH0   0x0241U
#define AP_CMD_DATA_SET_RATIO_CH1   0x0242U
#define AP_CMD_DATA_SET_INTERVAL    0x0261U

/* ---- 控制类 ---- */
#define AP_CMD_CTRL_SET_DAC         0x0301U
#define AP_CMD_CTRL_START_REPORT    0x0302U
#define AP_CMD_CTRL_STOP_REPORT     0x0303U
#define AP_CMD_CTRL_SLEEP           0x03AAU

/* ---- 参数配置类 ---- */
#define AP_CMD_PARAM_READ_ALL       0x0400U
#define AP_CMD_PARAM_READ_CH0       0x0401U
#define AP_CMD_PARAM_READ_CH1       0x0402U
#define AP_CMD_PARAM_READ_CH2       0x0403U
#define AP_CMD_PARAM_WRITE_CH0      0x0411U
#define AP_CMD_PARAM_WRITE_CH1      0x0412U
#define AP_CMD_PARAM_WRITE_CH2      0x0413U

/* ---- 告警类 ---- */
#define AP_CMD_ALERT_SET_MODE       0x0601U
#define AP_CMD_ALERT_QUERY          0x0602U
#define AP_CMD_ALERT_CLEAR          0x0603U

/* ---- 上报间隔 / 告警模式 ---- */
#define AP_REPORT_INTERVAL_1S       0x01U
#define AP_REPORT_INTERVAL_3S       0x02U
#define AP_REPORT_INTERVAL_5S       0x03U
#define AP_ALERT_MODE_ACTIVE        0x01U
#define AP_ALERT_MODE_PASSIVE       0x02U

/* 二进制帧最大长度：13 + 255 */
#define AP_PAYLOAD_MAX          255U
#define AP_BIN_MAX              (13U + AP_PAYLOAD_MAX)

/* 解析出来的一帧 */
typedef struct {
    uint16_t device_id;
    uint8_t  frame_type;
    uint16_t command;
    uint8_t  length;
    uint8_t  version;
    uint8_t  data[AP_PAYLOAD_MAX];
} app_frame_t;

/* 初始化：从参数页取设备 ID，清空接收状态 */
void app_protocol_init(void);

/* 喂一个 ASCII 字符，返回 1 表示拼出完整帧，内容用 app_protocol_get_frame() 取 */
int  app_protocol_feed(uint8_t ch);

const app_frame_t *app_protocol_get_frame(void);

uint16_t app_protocol_get_device_id(void);
void     app_protocol_set_device_id(uint16_t id);

/* 发送，均走 USART0 */
void app_protocol_send_response(uint16_t command, const uint8_t *data, uint8_t len);
void app_protocol_send_error(void);
void app_protocol_send_heartbeat(void);

/* 发送裸文本（告警记录），不套帧 */
void app_protocol_send_text(const char *text);

#endif /* APP_PROTOCOL_H */
