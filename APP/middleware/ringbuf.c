/* 环形缓冲区：head == tail 为空，free == 0 为满；
 * 必须留一个空位，size 个格子最多存 size-1 个字节。 */

#include "ringbuf.h"

void ringbuf_init(ringbuf_t *rb, uint8_t *buf, uint32_t size)
{
    rb->buf  = buf;
    rb->size = size;
    rb->head = 0U;
    rb->tail = 0U;
}

uint32_t ringbuf_available(const ringbuf_t *rb)
{
    /* 先加 size 再取模：head < tail 时直接相减会下溢成很大的数 */
    return (rb->head - rb->tail + rb->size) % rb->size;
}

uint32_t ringbuf_free(const ringbuf_t *rb)
{
    return rb->size - 1U - ringbuf_available(rb);
}

uint32_t ringbuf_write(ringbuf_t *rb, const uint8_t *d, uint32_t n)
{
    uint32_t i;

    for (i = 0U; i < n; i++) {
        if (ringbuf_free(rb) == 0U) {
            return i;                   /* 满了，返回实际写了几个 */
        }
        rb->buf[rb->head] = d[i];
        rb->head = (rb->head + 1U) % rb->size;
    }

    return i;
}

uint32_t ringbuf_read(ringbuf_t *rb, uint8_t *d, uint32_t n)
{
    uint32_t i;

    for (i = 0U; i < n; i++) {
        if (ringbuf_available(rb) == 0U) {
            return i;                   /* 空了 */
        }
        d[i] = rb->buf[rb->tail];
        rb->tail = (rb->tail + 1U) % rb->size;
    }

    return i;
}
