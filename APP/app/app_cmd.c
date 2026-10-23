/* 命令分发、自动上报、告警记录 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "gd32f4xx.h"       /* NVIC_SystemReset() */

#include "app_cmd.h"
#include "app_adc.h"
#include "app_protocol.h"
#include "param_store.h"
#include "bsp_dac.h"
#include "bsp_tick.h"
#include "bsp_rtc.h"
#include "bsp_pmu.h"
#include "drv_oled.h"
#include "drv_gd30ad3344.h"
#include "tasks.h"

/* 固件版本 2.0.1.0（赛题要求） */
#define FW_VER_MAJOR    0x02U
#define FW_VER_MINOR    0x01U
#define FW_VER_PATCH    0x00U
#define FW_VER_BUILD    0x00U

#define ALERT_RECORD_RAM_MAX    10U
#define ALERT_LINE_MAX_LEN      96U

#define BAUDRATE_MAP_4800       0x11U
#define BAUDRATE_MAP_9600       0x12U
#define BAUDRATE_MAP_19200      0x13U
#define BAUDRATE_MAP_115200     0x14U

/* 状态 */

static uint32_t s_device_time;              /* 软件计时的 Unix 秒基准 */
static uint32_t s_device_time_base_ms;      /* 记下基准时的 tick */
static uint8_t  s_time_sw;                  /* 1 = RTC 不可用，退回软件计时 */

static uint8_t  s_auto_report;
static uint16_t s_report_interval_ms;
static uint32_t s_last_report_ms;

/* 告警记录只放 RAM，上电清空，掉电丢失 */
static alert_record_t s_alerts[ALERT_RECORD_RAM_MAX];
static uint8_t s_alert_count;
static uint8_t s_alert_write_index;
static uint8_t s_alert_over[APP_ADC_CH_COUNT];

/* 小工具 */

static uint16_t cmd_read_be16(const uint8_t *d)
{
    return (uint16_t)(((uint16_t)d[0] << 8) | d[1]);
}

static uint32_t cmd_read_be32(const uint8_t *d)
{
    return ((uint32_t)d[0] << 24) | ((uint32_t)d[1] << 16) |
           ((uint32_t)d[2] << 8)  | (uint32_t)d[3];
}

static void cmd_write_be16(uint8_t *d, uint16_t v)
{
    d[0] = (uint8_t)(v >> 8);
    d[1] = (uint8_t)(v & 0xFFU);
}

static void cmd_write_be32(uint8_t *d, uint32_t v)
{
    d[0] = (uint8_t)(v >> 24);
    d[1] = (uint8_t)(v >> 16);
    d[2] = (uint8_t)(v >> 8);
    d[3] = (uint8_t)(v & 0xFFU);
}

static void cmd_write_float_be(uint8_t *d, float v)
{
    union {
        float    f;
        uint32_t u;
    } conv;

    conv.f = v;
    cmd_write_be32(d, conv.u);
}

static float cmd_read_float_be(const uint8_t *d)
{
    union {
        float    f;
        uint32_t u;
    } conv;

    conv.u = cmd_read_be32(d);
    return conv.f;
}

/* 参数页里存的是间隔代码（1/2/3），转成毫秒 */
static uint16_t cmd_interval_code_to_ms(uint32_t code)
{
    switch (code) {
    case AP_REPORT_INTERVAL_3S:
        return 3000U;
    case AP_REPORT_INTERVAL_5S:
        return 5000U;
    case AP_REPORT_INTERVAL_1S:
    default:
        return 1000U;
    }
}

/* 初始化 / 时间 */

void app_cmd_init(void)
{
    s_device_time         = 0U;
    s_device_time_base_ms = bsp_tick_ms();
    s_time_sw             = (bsp_rtc_is_ready() == 1) ? 0U : 1U;

    s_auto_report        = 0U;
    s_report_interval_ms = cmd_interval_code_to_ms(param_get_report_interval());
    s_last_report_ms     = bsp_tick_ms();

    memset(s_alerts, 0, sizeof(s_alerts));
    memset(s_alert_over, 0, sizeof(s_alert_over));
    s_alert_count       = 0U;
    s_alert_write_index = 0U;
}

uint32_t app_cmd_get_utc_seconds(void)
{
    uint32_t sec;

    /* 优先用 RTC（跨睡眠、跨断电）；RTC 不可用时退回上电秒计数 */
    if ((s_time_sw == 0U) && (bsp_rtc_get_unix(&sec) == 0)) {
        return sec;
    }

    return s_device_time +
           ((uint32_t)bsp_tick_ms() - s_device_time_base_ms) / 1000U;
}

int app_cmd_is_auto_report_enabled(void)
{
    return (s_auto_report != 0U) ? 1 : 0;
}

/* 告警 */

/* 只显示时分秒，日期固定 2026-01-01，上位机按此格式解析 */
static int cmd_format_alert_line(const alert_record_t *rec,
                                 char *out, size_t out_len)
{
    uint32_t seconds;
    uint32_t minutes;
    uint32_t hours;

    if ((rec == NULL) || (out == NULL) || (out_len == 0U)) {
        return 0;
    }

    seconds = rec->timestamp % 60U;
    minutes = (rec->timestamp / 60U) % 60U;
    hours   = (rec->timestamp / 3600U) % 24U;

    return snprintf(out, out_len,
                    "2026-01-01 %02lu:%02lu:%02lu | CH%lu | %.2f | %.2f\r\n",
                    (unsigned long)hours,
                    (unsigned long)minutes,
                    (unsigned long)seconds,
                    (unsigned long)rec->channel,
                    (double)rec->threshold,
                    (double)rec->actual_value);
}

static void cmd_alert_add(uint8_t channel, float threshold, float value)
{
    alert_record_t *rec = &s_alerts[s_alert_write_index];
    char line[ALERT_LINE_MAX_LEN];

    rec->timestamp    = app_cmd_get_utc_seconds();
    rec->channel      = channel;
    memset(rec->reserved, 0, sizeof(rec->reserved));
    rec->threshold    = threshold;
    rec->actual_value = value;

    s_alert_write_index = (uint8_t)((s_alert_write_index + 1U) % ALERT_RECORD_RAM_MAX);
    if (s_alert_count < ALERT_RECORD_RAM_MAX) {
        s_alert_count++;
    }

    /* 主动模式：立即推一行文本（非协议帧） */
    if (param_get_alert_mode() == AP_ALERT_MODE_ACTIVE) {
        (void)cmd_format_alert_line(rec, line, sizeof(line));
        app_protocol_send_text(line);
    }
}

/* 边沿触发：只在"未超 -> 超"的那一刻记一次 */
static void cmd_alert_check(uint8_t channel)
{
    int over;

    if (channel >= APP_ADC_CH_COUNT) {
        return;
    }

    over = app_adc_over_threshold(channel);

    if ((over != 0) && (s_alert_over[channel] == 0U)) {
        cmd_alert_add(channel,
                      app_adc_get_threshold(channel),
                      app_adc_get_value(channel));
    }

    s_alert_over[channel] = (uint8_t)over;
}

/* 上报数据（12 字节） */

/* 时间戳用 memcpy 按本机小端写入，CH0/CH1 用大端浮点，上位机按此格式接收 */
static void cmd_build_report_data(uint8_t out[12])
{
    uint32_t ts = app_cmd_get_utc_seconds();

    memcpy(&out[0], &ts, 4U);
    cmd_write_float_be(&out[4], app_adc_get_value(APP_ADC_CH0));
    cmd_write_float_be(&out[8], app_adc_get_value(APP_ADC_CH1));
}

/* 各命令处理 */

static void cmd_sys_reboot(void)
{
    uint8_t ok = 0xFFU;

    app_protocol_send_response(AP_CMD_SYS_REBOOT, &ok, 1U);
    bsp_delay_ms(100U);
    NVIC_SystemReset();
}

static void cmd_sys_query_version(void)
{
    uint8_t ver[4] = { FW_VER_MAJOR, FW_VER_MINOR, FW_VER_PATCH, FW_VER_BUILD };

    app_protocol_send_response(AP_CMD_SYS_QUERY_VERSION, ver, 4U);
}

static void cmd_sys_set_time(const app_frame_t *f)
{
    uint8_t  ok = 0xFFU;
    uint32_t ts;

    if (f->length != 4U) {
        app_protocol_send_error();
        return;
    }

    ts = cmd_read_be32(f->data);

    /* 优先写 RTC；失败则退回软件计时，本次上电内 0x0106 可读回同值 */
    if (bsp_rtc_set_unix(ts) == 0) {
        s_time_sw = 0U;
    } else {
        s_time_sw             = 1U;
        s_device_time         = ts;
        s_device_time_base_ms = bsp_tick_ms();
    }

    app_protocol_send_response(AP_CMD_SYS_SET_TIME, &ok, 1U);
}

static void cmd_sys_query_time(void)
{
    uint8_t out[4];

    cmd_write_be32(out, app_cmd_get_utc_seconds());
    app_protocol_send_response(AP_CMD_SYS_QUERY_TIME, out, 4U);
}

static void cmd_sys_set_id(const app_frame_t *f)
{
    uint16_t id;
    uint8_t  ok = 0xFFU;

    if (f->length != 2U) {
        app_protocol_send_error();
        return;
    }

    id = cmd_read_be16(f->data);
    if ((id < 0x0001U) || (id > 0xFFFEU)) {
        app_protocol_send_error();
        return;
    }

    param_set_device_id(id);
    (void)param_store_save();
    app_protocol_set_device_id(id);
    app_protocol_send_response(AP_CMD_SYS_SET_ID, &ok, 1U);
}

static void cmd_sys_set_baudrate(const app_frame_t *f)
{
    uint8_t code;
    uint8_t ok = 0xFFU;

    if (f->length != 1U) {
        app_protocol_send_error();
        return;
    }

    code = f->data[0];
    if ((code != BAUDRATE_MAP_4800) && (code != BAUDRATE_MAP_9600) &&
        (code != BAUDRATE_MAP_19200) && (code != BAUDRATE_MAP_115200)) {
        app_protocol_send_error();
        return;
    }

    param_set_baudrate(code);
    (void)param_store_save();
    app_protocol_send_response(AP_CMD_SYS_SET_BAUDRATE, &ok, 1U);

    /* 先应答再复位，避免半截帧。协议口为 USART0 固定 115200，上位机仍以此收发；
     * 该参数只影响 USART1 RS485 业务口 */
    bsp_delay_ms(100U);
    NVIC_SystemReset();
}

static void cmd_sys_query_id(void)
{
    uint8_t out[2];

    cmd_write_be16(out, app_protocol_get_device_id());
    app_protocol_send_response(AP_CMD_SYS_QUERY_ID, out, 2U);
}

static void cmd_sys_query_baudrate(void)
{
    uint8_t code = (uint8_t)param_get_baudrate();

    app_protocol_send_response(AP_CMD_SYS_QUERY_BAUDRATE, &code, 1U);
}

static void cmd_query_channel(uint8_t channel, uint16_t command)
{
    uint8_t out[4];

    cmd_write_float_be(out, app_adc_get_value(channel));
    app_protocol_send_response(command, out, 4U);
}

static void cmd_set_ratio(const app_frame_t *f, uint8_t channel, uint16_t command)
{
    float ratio;
    uint8_t ok = 0xFFU;

    if (f->length != 4U) {
        app_protocol_send_error();
        return;
    }

    ratio = cmd_read_float_be(f->data);
    if ((ratio <= 0.0f) || (ratio > 1000.0f)) {
        app_protocol_send_error();
        return;
    }

    app_adc_set_ratio(channel, ratio);
    param_set_ratio(channel, ratio);
    (void)param_store_save();
    app_protocol_send_response(command, &ok, 1U);
}

static void cmd_set_interval(const app_frame_t *f)
{
    uint8_t code;
    uint8_t ok = 0xFFU;

    if (f->length != 1U) {
        app_protocol_send_error();
        return;
    }

    code = f->data[0];
    if ((code != AP_REPORT_INTERVAL_1S) &&
        (code != AP_REPORT_INTERVAL_3S) &&
        (code != AP_REPORT_INTERVAL_5S)) {
        app_protocol_send_error();
        return;
    }

    param_set_report_interval((uint32_t)code);
    (void)param_store_save();

    /* 存参数并立即切换上报周期 */
    s_report_interval_ms = cmd_interval_code_to_ms((uint32_t)code);

    app_protocol_send_response(AP_CMD_DATA_SET_INTERVAL, &ok, 1U);
}

static void cmd_ctrl_set_dac(const app_frame_t *f)
{
    uint16_t value;
    uint8_t  ok = 0xFFU;

    if (f->length != 2U) {
        app_protocol_send_error();
        return;
    }

    value = cmd_read_be16(f->data);
    if (bsp_dac_set(value) != 0) {
        app_protocol_send_error();
        return;
    }

    /* 落盘，保证重启后 CH1 回读链仍成立 */
    param_set_dac_value((uint32_t)value);
    (void)param_store_save();
    app_protocol_send_response(AP_CMD_CTRL_SET_DAC, &ok, 1U);
}

static void cmd_ctrl_start_report(void)
{
    uint8_t out[12];

    s_auto_report    = 1U;
    s_last_report_ms = bsp_tick_ms();

    task_led_set_work(1U);

    /* 首帧立即回，内容与周期上报格式相同 */
    cmd_build_report_data(out);
    app_protocol_send_response(AP_CMD_CTRL_START_REPORT, out, 12U);
}

static void cmd_ctrl_stop_report(void)
{
    uint8_t ok = 0xFFU;

    s_auto_report = 0U;
    task_led_set_work(0U);
    app_protocol_send_response(AP_CMD_CTRL_STOP_REPORT, &ok, 1U);
}

static void cmd_ctrl_sleep(void)
{
    uint8_t ok = 0xFFU;

    s_auto_report = 0U;
    task_led_set_work(0U);

    /* 先应答并等发送完成，之后关串口，避免半截帧 */
    app_protocol_send_response(AP_CMD_CTRL_SLEEP, &ok, 1U);
    bsp_delay_ms(20U);

    /* 睡前关闭外部器件：OLED 电荷泵 100~300µA，AD3344 连续转换 150µA；
     * 需在关 I2C/SPI 之前完成，从机不在线时函数内部有超时保护 */
    (void)drv_oled_display_off();
    drv_gd30_powerdown();

    /* PA0 按键唤醒，睡到按键按下；返回时外设由 main.c 回调恢复。
     * 改用 RTC 定时唤醒可换成 bsp_pmu_deepsleep_rtc(10U) */
    (void)bsp_pmu_deepsleep_key();

    app_protocol_send_text("instrument wakeup\r\n");
}

static void cmd_param_read_all(void)
{
    uint8_t out[8];

    cmd_write_float_be(&out[0], app_adc_get_threshold(APP_ADC_CH0));
    cmd_write_float_be(&out[4], app_adc_get_threshold(APP_ADC_CH1));
    app_protocol_send_response(AP_CMD_PARAM_READ_ALL, out, 8U);
}

static void cmd_param_read_ch(uint8_t channel, uint16_t command)
{
    uint8_t out[4];

    cmd_write_float_be(out, app_adc_get_threshold(channel));
    app_protocol_send_response(command, out, 4U);
}

static void cmd_param_write_ch(const app_frame_t *f, uint8_t channel, uint16_t command)
{
    float threshold;
    uint8_t ok = 0xFFU;

    if (f->length != 4U) {
        app_protocol_send_error();
        return;
    }

    threshold = cmd_read_float_be(f->data);
    app_adc_set_threshold(channel, threshold);
    param_set_threshold(channel, threshold);
    (void)param_store_save();
    app_protocol_send_response(command, &ok, 1U);
}

static void cmd_alert_set_mode(const app_frame_t *f)
{
    uint8_t mode;
    uint8_t ok = 0xFFU;

    if (f->length != 1U) {
        app_protocol_send_error();
        return;
    }

    mode = f->data[0];
    if ((mode != AP_ALERT_MODE_ACTIVE) && (mode != AP_ALERT_MODE_PASSIVE)) {
        app_protocol_send_error();
        return;
    }

    param_set_alert_mode((uint32_t)mode);
    (void)param_store_save();
    app_protocol_send_response(AP_CMD_ALERT_SET_MODE, &ok, 1U);
}

static void cmd_alert_query(void)
{
    uint8_t i;
    char line[ALERT_LINE_MAX_LEN];

    if (s_alert_count == 0U) {
        app_protocol_send_text("empty\r\n");
        return;
    }

    /* 从最新一条往前读 */
    for (i = 0U; i < s_alert_count; i++) {
        uint8_t idx = (uint8_t)((s_alert_write_index + ALERT_RECORD_RAM_MAX - 1U - i)
                                % ALERT_RECORD_RAM_MAX);
        (void)cmd_format_alert_line(&s_alerts[idx], line, sizeof(line));
        app_protocol_send_text(line);
    }
}

static void cmd_alert_clear(void)
{
    uint8_t ok = 0xFFU;
    uint8_t ch;

    memset(s_alerts, 0, sizeof(s_alerts));
    s_alert_count       = 0U;
    s_alert_write_index = 0U;

    /* 让边沿检测以"当前状态"为起点，避免清完立刻又记一条 */
    for (ch = 0U; ch < APP_ADC_CH_COUNT; ch++) {
        s_alert_over[ch] = (uint8_t)app_adc_over_threshold(ch);
    }

    app_protocol_send_response(AP_CMD_ALERT_CLEAR, &ok, 1U);
}

/* 分发 */

void app_cmd_dispatch(const app_frame_t *f)
{
    if (f == NULL) {
        return;
    }

    /* 不是发给我的、也不是广播 -> 静默丢弃 */
    if ((f->device_id != app_protocol_get_device_id()) &&
        (f->device_id != AP_BROADCAST_ID)) {
        return;
    }

    /* 只处理命令帧和心跳帧 */
    if ((f->frame_type != AP_TYPE_COMMAND) &&
        (f->frame_type != AP_TYPE_HEARTBEAT)) {
        return;
    }

    /* 自动上报期间除 0x0303 停止上报外，其他命令均不应答 */
    if ((s_auto_report != 0U) && (f->command != AP_CMD_CTRL_STOP_REPORT)) {
        return;
    }

    switch (f->command) {
    /* ---- 系统管理 ---- */
    case AP_CMD_SYS_REBOOT:          cmd_sys_reboot();              break;
    case AP_CMD_SYS_QUERY_VERSION:   cmd_sys_query_version();       break;
    case AP_CMD_SYS_SET_TIME:        cmd_sys_set_time(f);           break;
    case AP_CMD_SYS_QUERY_TIME:      cmd_sys_query_time();          break;
    case AP_CMD_SYS_SET_ID:          cmd_sys_set_id(f);             break;
    case AP_CMD_SYS_SET_BAUDRATE:    cmd_sys_set_baudrate(f);       break;
    case AP_CMD_SYS_QUERY_ID:        cmd_sys_query_id();            break;
    case AP_CMD_SYS_QUERY_BAUDRATE:  cmd_sys_query_baudrate();      break;

    /* ---- 数据 ---- */
    case AP_CMD_DATA_QUERY_CH0:
        cmd_query_channel(APP_ADC_CH0, AP_CMD_DATA_QUERY_CH0);      break;
    case AP_CMD_DATA_QUERY_CH1:
        cmd_query_channel(APP_ADC_CH1, AP_CMD_DATA_QUERY_CH1);      break;
    case AP_CMD_DATA_QUERY_CH2:
        cmd_query_channel(APP_ADC_CH2, AP_CMD_DATA_QUERY_CH2);      break;
    case AP_CMD_DATA_SET_RATIO_CH0:
        cmd_set_ratio(f, APP_ADC_CH0, AP_CMD_DATA_SET_RATIO_CH0);   break;
    case AP_CMD_DATA_SET_RATIO_CH1:
        cmd_set_ratio(f, APP_ADC_CH1, AP_CMD_DATA_SET_RATIO_CH1);   break;
    case AP_CMD_DATA_SET_INTERVAL:   cmd_set_interval(f);           break;

    /* ---- 控制 ---- */
    case AP_CMD_CTRL_SET_DAC:        cmd_ctrl_set_dac(f);           break;
    case AP_CMD_CTRL_START_REPORT:   cmd_ctrl_start_report();       break;
    case AP_CMD_CTRL_STOP_REPORT:    cmd_ctrl_stop_report();        break;
    case AP_CMD_CTRL_SLEEP:          cmd_ctrl_sleep();              break;

    /* ---- 参数 ---- */
    case AP_CMD_PARAM_READ_ALL:      cmd_param_read_all();          break;
    case AP_CMD_PARAM_READ_CH0:
        cmd_param_read_ch(APP_ADC_CH0, AP_CMD_PARAM_READ_CH0);      break;
    case AP_CMD_PARAM_READ_CH1:
        cmd_param_read_ch(APP_ADC_CH1, AP_CMD_PARAM_READ_CH1);      break;
    case AP_CMD_PARAM_READ_CH2:
        cmd_param_read_ch(APP_ADC_CH2, AP_CMD_PARAM_READ_CH2);      break;
    case AP_CMD_PARAM_WRITE_CH0:
        cmd_param_write_ch(f, APP_ADC_CH0, AP_CMD_PARAM_WRITE_CH0); break;
    case AP_CMD_PARAM_WRITE_CH1:
        cmd_param_write_ch(f, APP_ADC_CH1, AP_CMD_PARAM_WRITE_CH1); break;
    case AP_CMD_PARAM_WRITE_CH2:
        cmd_param_write_ch(f, APP_ADC_CH2, AP_CMD_PARAM_WRITE_CH2); break;

    /* ---- 告警 ---- */
    case AP_CMD_ALERT_SET_MODE:      cmd_alert_set_mode(f);         break;
    case AP_CMD_ALERT_QUERY:         cmd_alert_query();             break;
    case AP_CMD_ALERT_CLEAR:         cmd_alert_clear();             break;

    /* ---- 广播寻找设备 ---- */
    case AP_CMD_BROADCAST_FIND:
        app_protocol_send_heartbeat();
        break;

    default:
        app_protocol_send_error();
        break;
    }
}

/* 周期任务 */

void app_cmd_tick(void)
{
    uint32_t now;
    uint8_t  ch;
    uint8_t  out[12];

    /* 自动上报期间不做告警检测 */
    if (s_auto_report != 0U) {
        now = bsp_tick_ms();
        if ((now - s_last_report_ms) >= (uint32_t)s_report_interval_ms) {
            s_last_report_ms = now;
            cmd_build_report_data(out);
            app_protocol_send_response(AP_CMD_CTRL_START_REPORT, out, 12U);
        }
        return;
    }

    for (ch = 0U; ch < APP_ADC_CH_COUNT; ch++) {
        cmd_alert_check(ch);
    }
}
