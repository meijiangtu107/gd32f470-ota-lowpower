/* SPI 总线对外接口。只做字节收发；片选（CS）由调用方（drv_ 层）控制 ——
 * 不同器件的 CS 时序不一样，总线层不该管。 */

#ifndef BSP_SPI_H
#define BSP_SPI_H

#include <stdint.h>

/* 初始化两条 SPI 链路。
 * SPI0：Flash 用，模式 3（CPOL=1 CPHA=2Edge），PSC 8  → 120MHz/8 = 15MHz
 * SPI3：AD3344 用，模式 1，PSC 256 → 60MHz/256 = 234kHz */
int bsp_spi0_init(void);
int bsp_spi3_init(void);

/* 收发一个字节。SPI 是全双工，发的同时必定收一个。
 * 只想读数据时，tx 传 0xFF */
uint8_t bsp_spi_xfer_byte(uint32_t spi, uint8_t tx);

/* 收发一块。rx 传 NULL 表示只发不收 */
void bsp_spi_xfer(uint32_t spi, const uint8_t *tx, uint8_t *rx, uint16_t n);

#endif /* BSP_SPI_H */
