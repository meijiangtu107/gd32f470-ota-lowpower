/* 流式拆帧状态机。帧格式 A5B6 | dev_id(2) | type(1) | cmd(2) | len(1) | ver(1) |
 * payload | CRC16(2) | B6A5，总长 13+len；CRC 及其他多字节字段均为大端。
 * 支持分片到达、粘包与前导垃圾。 */

#ifndef FRAME_PARSER_H
#define FRAME_PARSER_H

#include <stdint.h>

#define FP_BUF_MAX      268U        /* 13 + 255，len 是 1 字节 */

typedef enum {
    FP_HEAD0 = 0,       /* 等 A5 */
    FP_HEAD1,           /* 等 B6 */
    FP_BODY             /* 收剩下的 */
} fp_state_t;

typedef struct {
    fp_state_t state;
    uint8_t  buf[FP_BUF_MAX];
    uint16_t index;         /* 已收字节数 */
    uint16_t expected;      /* 总长，收到第 9 字节时算出 */
} frame_parser_t;

void frame_parser_init(frame_parser_t *fp);

/* 喂一个字节，返回 1 表示拼出完整帧（fp->buf，长度 fp->expected） */
int frame_parser_feed(frame_parser_t *fp, uint8_t byte);

#endif /* FRAME_PARSER_H */
