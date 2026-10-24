/* CRC32（ISO-HDLC 变体，反射多项式 0xEDB88320）：参数块与 OTA 包共用的完整性校验。 */

#ifndef SHARED_CRC_H
#define SHARED_CRC_H

#include <stdint.h>

/* 计算 CRC32：初值 0xFFFFFFFF，结果取反。
 * 测试向量："123456789" → 0xCBF43926 */
uint32_t crc32_calc(const uint8_t *data, uint32_t len);

#endif /* SHARED_CRC_H */
