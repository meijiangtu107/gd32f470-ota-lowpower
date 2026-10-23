/* CRC16-Modbus */

#ifndef CRC16_H
#define CRC16_H

#include <stdint.h>

/* 多项式 0xA001（反射），初值 0xFFFF */
uint16_t crc16_modbus(const uint8_t *d, uint16_t n);

#endif /* CRC16_H */
