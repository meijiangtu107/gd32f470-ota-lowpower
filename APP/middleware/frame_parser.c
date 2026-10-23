/* 流式拆帧状态机：3 个状态，收满 expected 字节后统一校验帧尾和 CRC */

#include "frame_parser.h"
#include "crc16.h"

/* 帧内固定偏移 */
#define FP_HEAD_0       0U      /* A5 */
#define FP_HEAD_1       1U      /* B6 */
#define FP_LEN_IDX      7U      /* len 字段 */
#define FP_HEAD_SIZE    9U      /* 帧头 + 长度字段，收够这些才能算总长 */
#define FP_OVERHEAD     13U     /* 13 + len = 总长 */
#define FP_CRC_SIZE     2U
#define FP_TAIL_SIZE    2U

/* 帧尾与 CRC 同时校验，避免数据中的 A5B6 / B6A5 造成假帧或半帧 */
static int frame_check(const frame_parser_t *fp)
{
    uint16_t crc_calc;
    uint16_t crc_recv;

    /* 帧尾 */
    if ((fp->buf[fp->expected - FP_TAIL_SIZE] != 0xB6U) ||
        (fp->buf[fp->expected - 1U] != 0xA5U)) {
        return 0;
    }

    /* CRC 覆盖 expected-4 个字节，帧内 CRC 按大端存放（高字节在前） */
    crc_calc = crc16_modbus(fp->buf, (uint16_t)(fp->expected - FP_CRC_SIZE - FP_TAIL_SIZE));
    crc_recv = (uint16_t)(((uint16_t)fp->buf[fp->expected - FP_CRC_SIZE - FP_TAIL_SIZE] << 8) |
                          fp->buf[fp->expected - FP_CRC_SIZE - FP_TAIL_SIZE + 1U]);

    return (crc_calc == crc_recv) ? 1 : 0;
}

void frame_parser_init(frame_parser_t *fp)
{
    fp->state    = FP_HEAD0;
    fp->index    = 0U;
    fp->expected = 0U;
}

int frame_parser_feed(frame_parser_t *fp, uint8_t byte)
{
    switch (fp->state) {
    case FP_HEAD0:
        if (byte == 0xA5U) {
            fp->buf[FP_HEAD_0] = byte;
            fp->index = 1U;
            fp->state = FP_HEAD1;
        }
        break;

    case FP_HEAD1:
        if (byte == 0xB6U) {
            fp->buf[FP_HEAD_1] = byte;
            fp->index = 2U;
            fp->state = FP_BODY;
        } else if (byte != 0xA5U) {
            fp->state = FP_HEAD0;       /* 不是 B6 也不是 A5，重新找头 */
        }
        /* byte == A5 时留在本状态：A5 A5 B6 也要能认出来 */
        break;

    case FP_BODY:
    default:
        fp->buf[fp->index] = byte;
        fp->index++;

        if (fp->index == FP_HEAD_SIZE) {
            /* 固定头收完，len 字段到手，能算总长了 */
            fp->expected = (uint16_t)(FP_OVERHEAD + fp->buf[FP_LEN_IDX]);
        }

        if (fp->index == fp->expected) {
            fp->state = FP_HEAD0;

            if (frame_check(fp) != 0) {
                return 1;
            }
            /* 校验失败：整帧丢掉，从下一个字节重新找头 */
        }
        break;
    }

    return 0;
}
