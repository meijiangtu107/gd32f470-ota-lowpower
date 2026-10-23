/* 环形缓冲区接口 */

#ifndef RINGBUF_H
#define RINGBUF_H

#include <stdint.h>

typedef struct {
    uint8_t *buf;
    uint32_t size;              /* 总长。最多只能存 size-1 个字节 */
    volatile uint32_t head;     /* 写指针，只由生产者改 */
    volatile uint32_t tail;     /* 读指针，只由消费者改 */
} ringbuf_t;

/* size 必须 >= 2，否则 free() 会算成负数 */
void ringbuf_init(ringbuf_t *rb, uint8_t *buf, uint32_t size);

uint32_t ringbuf_available(const ringbuf_t *rb);    /* 有多少字节可读 */
uint32_t ringbuf_free(const ringbuf_t *rb);         /* 还能写多少字节 */

/* 两个都返回"实际处理了多少个字节"，放不下/读空时小于 n */
uint32_t ringbuf_write(ringbuf_t *rb, const uint8_t *d, uint32_t n);
uint32_t ringbuf_read(ringbuf_t *rb, uint8_t *d, uint32_t n);

#endif /* RINGBUF_H */
