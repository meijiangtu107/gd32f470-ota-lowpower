/* GD25Qxx NOR Flash 驱动接口 */

#ifndef DRV_GD25QXX_H
#define DRV_GD25QXX_H

#include <stdint.h>

/* JEDEC ID 0xC84013 → GD25Q40，512KB */
#define GD25Q_CAPACITY      (512U * 1024U)

/* 一页 256 字节（写操作的粒度），一个扇区 4KB（擦除的粒度） */
#define GD25Q_PAGE_SIZE     256U
#define GD25Q_SECTOR_SIZE   4096U

/* 读 JEDEC ID，正常返回 0xC8 开头（GigaDevice 厂商码） */
uint32_t drv_gd25q_read_id(void);

/* 读任意长度。addr 从 0 开始，最大 GD25Q_CAPACITY-1。返回 0 成功 */
int drv_gd25q_read(uint32_t addr, uint8_t *buf, uint16_t len);

/* 按 256B 页拆分写入。只能 1→0，改数据前须擦扇区。返回 0 成功 */
int drv_gd25q_write(uint32_t addr, const uint8_t *buf, uint16_t len);

/* 擦除 4KB 扇区，擦后全 0xFF；addr 向下对齐。返回 0 成功 */
int drv_gd25q_erase_sector(uint32_t addr);

#endif /* DRV_GD25QXX_H */
