/* CRC-32/ISO-HDLC（反射多项式 0xEDB88320），用于参数块完整性与 OTA 整包校验。 */

#ifndef SHARED_CRC_H
#define SHARED_CRC_H

#include <stdint.h>

/* 计算 CRC32：初值 0xFFFFFFFF，结果取反。
 * 测试向量："123456789" → 0xCBF43926 */
uint32_t crc32_calc(const uint8_t *data, uint32_t len);

#endif /* SHARED_CRC_H */
