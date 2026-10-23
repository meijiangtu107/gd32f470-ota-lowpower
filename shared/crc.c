/* CRC-32/ISO-HDLC：反射多项式 0xEDB88320，初值 0xFFFFFFFF，结果取反。
 * 逐位实现，无查表。 */

#include "crc.h"

uint32_t crc32_calc(const uint8_t *data, uint32_t len)
{
    uint32_t crc = 0xFFFFFFFFUL;
    uint32_t i;
    uint8_t  bit;

    if (data == 0) {
        return 0UL;
    }

    for (i = 0UL; i < len; i++) {
        crc ^= (uint32_t)data[i];

        for (bit = 0U; bit < 8U; bit++) {
            if ((crc & 1UL) != 0UL) {
                /* 多项式是正常序 0x04C11DB7 的位反转 */
                crc = (crc >> 1) ^ 0xEDB88320UL;
            } else {
                crc >>= 1;
            }
        }
    }

    return crc ^ 0xFFFFFFFFUL;
}
