/* CRC16-Modbus：初值 0xFFFF，多项式 0xA001（反射），每字节移 8 次。
 * 测试向量 "123456789" 的 CRC 为 0x4B37。 */

#include "crc16.h"

uint16_t crc16_modbus(const uint8_t *d, uint16_t n)
{
    uint16_t crc = 0xFFFFU;
    uint16_t i;
    uint8_t  j;

    for (i = 0U; i < n; i++) {
        crc ^= d[i];

        for (j = 0U; j < 8U; j++) {
            if ((crc & 1U) != 0U) {
                crc = (uint16_t)((crc >> 1) ^ 0xA001U);
            } else {
                crc >>= 1;
            }
        }
    }

    return crc;
}
