/* 协议帧构建 / 解析 / 发送：USART0 收到的 ASCII 十六进制文本先找 "A5B6"，
 * 解成字节后由 frame_parser 拼帧，再交 app_cmd_dispatch。
 * 收发缓冲放 static，避免占用仅 1KB 的栈。 */

#include <string.h>

#include "app_protocol.h"
#include "frame_parser.h"
#include "crc16.h"
#include "bsp_uart.h"
#include "param_store.h"

/* 内部状态 */

static app_frame_t s_frame;                 /* 解析结果 */
static frame_parser_t s_fp;                 /* 二进制拼帧状态机 */

static uint8_t s_hunt;                      /* 已匹配到 "A5B6" 的第几个字符 */
static uint8_t s_hi;                        /* 半个字节（高 4 位） */
static uint8_t s_hi_valid;

static uint16_t s_device_id;

/* 发送缓冲，放 .bss 以免占用 1KB 栈 */
static uint8_t s_tx_bin[AP_BIN_MAX];
static uint8_t s_tx_ascii[AP_BIN_MAX * 2U];

/* 小工具：大端读写 */

static void ap_write_be16(uint8_t *d, uint16_t v)
{
    d[0] = (uint8_t)(v >> 8);
    d[1] = (uint8_t)(v & 0xFFU);
}

static uint8_t ap_hex_to_nibble(uint8_t ch)
{
    if ((ch >= '0') && (ch <= '9')) {
        return (uint8_t)(ch - '0');
    }
    if ((ch >= 'A') && (ch <= 'F')) {
        return (uint8_t)(ch - 'A' + 10U);
    }
    if ((ch >= 'a') && (ch <= 'f')) {
        return (uint8_t)(ch - 'a' + 10U);
    }
    return 0xFFU;
}

static uint8_t ap_nibble_to_hex(uint8_t v)
{
    static const uint8_t digits[] = "0123456789ABCDEF";

    return digits[v & 0x0FU];
}

/* 初始化 */

void app_protocol_init(void)
{
    uint32_t id = param_get_device_id();

    if ((id >= 0x0001UL) && (id <= 0xFFFEUL)) {
        s_device_id = (uint16_t)id;
    } else {
        s_device_id = 0x0001U;
    }

    frame_parser_init(&s_fp);
    s_hunt = 0U;
    s_hi_valid = 0U;
    memset(&s_frame, 0, sizeof(s_frame));
}

uint16_t app_protocol_get_device_id(void)
{
    return s_device_id;
}

void app_protocol_set_device_id(uint16_t id)
{
    if ((id >= 0x0001U) && (id <= 0xFFFEU)) {
        s_device_id = id;
    }
}

/* 接收 */

/* 把 frame_parser 拼好的二进制帧拷进 app_frame_t */
static void ap_take_frame(void)
{
    const uint8_t *b = s_fp.buf;

    s_frame.device_id  = (uint16_t)(((uint16_t)b[2] << 8) | b[3]);
    s_frame.frame_type = b[4];
    s_frame.command    = (uint16_t)(((uint16_t)b[5] << 8) | b[6]);
    s_frame.length     = b[7];
    s_frame.version    = b[8];
    if (s_frame.length > 0U) {
        memcpy(s_frame.data, &b[9], s_frame.length);
    }
}

int app_protocol_feed(uint8_t ch)
{
    static const uint8_t head[4] = { (uint8_t)'A', (uint8_t)'5',
                                     (uint8_t)'B', (uint8_t)'6' };
    uint8_t nib;
    uint8_t byte;

    /* ---- 同步帧头 "A5B6" ---- */
    if (s_hunt < 4U) {
        if (ch == head[s_hunt]) {
            s_hunt++;
            if (s_hunt == 4U) {
                frame_parser_init(&s_fp);
                s_hi_valid = 0U;
                (void)frame_parser_feed(&s_fp, 0xA5U);
                (void)frame_parser_feed(&s_fp, 0xB6U);
            }
        } else {
            /* 本字符为 'A' 时回退到已匹配 1 个字符，避免 "AA5B6" 漏判 */
            s_hunt = (ch == head[0]) ? 1U : 0U;
        }
        return 0;
    }

    /* ---- 帧头锁定后做十六进制解码 ---- */
    nib = ap_hex_to_nibble(ch);
    if (nib > 0x0FU) {
        /* 非十六进制字符：本帧作废；若当前是 'A' 则作为新帧头起点 */
        s_hi_valid = 0U;
        s_hunt = (ch == head[0]) ? 1U : 0U;
        return 0;
    }

    if (s_hi_valid == 0U) {
        s_hi = nib;
        s_hi_valid = 1U;
        return 0;
    }

    s_hi_valid = 0U;
    byte = (uint8_t)((s_hi << 4) | nib);

    if (frame_parser_feed(&s_fp, byte) != 0) {
        s_hunt = 0U;
        ap_take_frame();
        return 1;
    }

    return 0;
}

const app_frame_t *app_protocol_get_frame(void)
{
    return &s_frame;
}

/* 发送 */

/* 组二进制帧，返回总长；CRC 按大端写回 */
static uint16_t ap_build_bin(uint16_t device_id, uint8_t frame_type,
                             uint16_t command, const uint8_t *data, uint8_t len)
{
    uint16_t crc;

    ap_write_be16(&s_tx_bin[0], AP_HEADER_START);
    ap_write_be16(&s_tx_bin[2], device_id);
    s_tx_bin[4] = frame_type;
    ap_write_be16(&s_tx_bin[5], command);
    s_tx_bin[7] = len;
    s_tx_bin[8] = AP_PROTOCOL_VERSION;

    if ((len > 0U) && (data != NULL)) {
        memcpy(&s_tx_bin[9], data, len);
    }

    crc = crc16_modbus(s_tx_bin, (uint16_t)(9U + len));
    ap_write_be16(&s_tx_bin[9U + len], crc);
    ap_write_be16(&s_tx_bin[11U + len], AP_TAIL_END);

    return (uint16_t)(13U + len);
}

/* 二进制帧 -> ASCII 十六进制，然后从 USART0 发出去 */
static void ap_send_bin(uint16_t device_id, uint8_t frame_type,
                        uint16_t command, const uint8_t *data, uint8_t len)
{
    uint16_t bin_len;
    uint16_t i;

    bin_len = ap_build_bin(device_id, frame_type, command, data, len);

    for (i = 0U; i < bin_len; i++) {
        s_tx_ascii[i * 2U]      = ap_nibble_to_hex((uint8_t)(s_tx_bin[i] >> 4));
        s_tx_ascii[i * 2U + 1U] = ap_nibble_to_hex((uint8_t)(s_tx_bin[i] & 0x0FU));
    }

    (void)bsp_uart_write(USART0, s_tx_ascii, (uint16_t)(bin_len * 2U));
}

void app_protocol_send_response(uint16_t command, const uint8_t *data, uint8_t len)
{
    if ((len > 0U) && (data == NULL)) {
        return;
    }
    ap_send_bin(s_device_id, AP_TYPE_RESPONSE, command, data, len);
}

/* 错误帧：命令字固定 0xEEEE，不回原命令字 */
void app_protocol_send_error(void)
{
    ap_send_bin(s_device_id, AP_TYPE_ERROR, AP_CMD_ERROR_RESPONSE, NULL, 0U);
}

void app_protocol_send_heartbeat(void)
{
    ap_send_bin(s_device_id, AP_TYPE_HEARTBEAT, AP_CMD_HEARTBEAT_DEVICE, NULL, 0U);
}

void app_protocol_send_text(const char *text)
{
    if (text == NULL) {
        return;
    }
    (void)bsp_uart_write(USART0, (const uint8_t *)text,
                         (uint16_t)strlen(text));
}
