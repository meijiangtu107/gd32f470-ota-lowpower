/* GD25Qxx NOR Flash 驱动（SPI0）：页 256B、扇区 4KB。
 * 写/擦前发 WREN(0x06)，完成后轮询状态寄存器 WIP 位；只能 1→0，改数据须先擦扇区。 */

#include "drv_gd25qxx.h"
#include "bsp_spi.h"
#include "bsp_pinmap.h"
#include "bsp_tick.h"
#include "gd32f4xx.h"

/* Flash 指令 */
#define GD25Q_CMD_WREN      0x06U   /* 写使能 */
#define GD25Q_CMD_RDSR      0x05U   /* 读状态寄存器 */
#define GD25Q_CMD_READ      0x03U   /* 读数据 */
#define GD25Q_CMD_PP        0x02U   /* 页编程（写） */
#define GD25Q_CMD_SE        0x20U   /* 扇区擦除（4KB） */
#define GD25Q_CMD_RDID      0x9FU   /* 读 JEDEC ID */

#define GD25Q_SR_WIP        0x01U   /* 状态寄存器 bit0：1 = 正在忙 */
#define GD25Q_DUMMY_BYTE    0xFFU

/* WIP 清零超时（擦扇区典型几十毫秒） */
#define GD25Q_TIMEOUT_MS    1000U

/* 片选低有效 */
#define GD25Q_CS_LOW()      gpio_bit_reset(BSP_PIN_SPI0_CS.port, BSP_PIN_SPI0_CS.pin)
#define GD25Q_CS_HIGH()     gpio_bit_set(BSP_PIN_SPI0_CS.port, BSP_PIN_SPI0_CS.pin)

/* 发写使能 WREN */
static void gd25q_write_enable(void)
{
    GD25Q_CS_LOW();
    (void)bsp_spi_xfer_byte(SPI0, GD25Q_CMD_WREN);
    GD25Q_CS_HIGH();
}

/* 轮询 WIP 位，返回 0 = 空闲，-1 = 超时 */
static int gd25q_wait_busy(void)
{
    uint8_t  sr;
    uint32_t start = bsp_tick_ms();

    GD25Q_CS_LOW();
    (void)bsp_spi_xfer_byte(SPI0, GD25Q_CMD_RDSR);

    do {
        sr = bsp_spi_xfer_byte(SPI0, GD25Q_DUMMY_BYTE);

        /* 减法判超时，tick 回绕也正确 */
        if ((bsp_tick_ms() - start) > GD25Q_TIMEOUT_MS) {
            GD25Q_CS_HIGH();
            return -1;
        }
    } while ((sr & GD25Q_SR_WIP) != 0U);

    GD25Q_CS_HIGH();
    return 0;
}

/* 发 3 字节地址，高位在前 */
static void gd25q_send_addr(uint32_t addr)
{
    (void)bsp_spi_xfer_byte(SPI0, (uint8_t)(addr >> 16));
    (void)bsp_spi_xfer_byte(SPI0, (uint8_t)(addr >> 8));
    (void)bsp_spi_xfer_byte(SPI0, (uint8_t)(addr));
}

/* 对外接口 */

uint32_t drv_gd25q_read_id(void)
{
    uint8_t id0;
    uint8_t id1;
    uint8_t id2;

    GD25Q_CS_LOW();

    /* 先发指令，再连发 3 个哑元字节把 ID 读回来 */
    (void)bsp_spi_xfer_byte(SPI0, GD25Q_CMD_RDID);
    id0 = bsp_spi_xfer_byte(SPI0, GD25Q_DUMMY_BYTE);
    id1 = bsp_spi_xfer_byte(SPI0, GD25Q_DUMMY_BYTE);
    id2 = bsp_spi_xfer_byte(SPI0, GD25Q_DUMMY_BYTE);

    GD25Q_CS_HIGH();

    return ((uint32_t)id0 << 16) | ((uint32_t)id1 << 8) | (uint32_t)id2;
}

int drv_gd25q_read(uint32_t addr, uint8_t *buf, uint16_t len)
{
    if ((buf == NULL) || (len == 0U)) {
        return -1;
    }
    if ((addr + (uint32_t)len) > GD25Q_CAPACITY) {
        return -1;                  /* 越界，芯片只有 512KB */
    }

    GD25Q_CS_LOW();
    (void)bsp_spi_xfer_byte(SPI0, GD25Q_CMD_READ);
    gd25q_send_addr(addr);
    /* 读不需 WREN，也不等忙；tx 为 NULL 时内部发 0xFF */
    bsp_spi_xfer(SPI0, NULL, buf, len);
    GD25Q_CS_HIGH();

    return 0;
}

int drv_gd25q_write(uint32_t addr, const uint8_t *buf, uint16_t len)
{
    uint16_t n;

    if ((buf == NULL) || (len == 0U)) {
        return -1;
    }
    if ((addr + (uint32_t)len) > GD25Q_CAPACITY) {
        return -1;
    }

    /* 写不能跨页，按 256B 拆分 */
    while (len > 0U) {
        uint16_t page_remain = (uint16_t)(GD25Q_PAGE_SIZE - (addr % GD25Q_PAGE_SIZE));

        n = (len < page_remain) ? len : page_remain;

        gd25q_write_enable();           /* WREN 单次有效，每页重发 */

        GD25Q_CS_LOW();
        (void)bsp_spi_xfer_byte(SPI0, GD25Q_CMD_PP);
        gd25q_send_addr(addr);
        bsp_spi_xfer(SPI0, buf, NULL, n);   /* 只发不收 */
        GD25Q_CS_HIGH();

        if (gd25q_wait_busy() != 0) {   /* 等芯片写完 */
            return -1;
        }

        addr += (uint32_t)n;
        buf  += n;
        len  -= n;
    }

    return 0;
}

int drv_gd25q_erase_sector(uint32_t addr)
{
    /* 向下对齐到 4KB 扇区边界 */
    addr &= ~(uint32_t)(GD25Q_SECTOR_SIZE - 1U);

    if (addr >= GD25Q_CAPACITY) {
        return -1;
    }

    gd25q_write_enable();               /* 擦除前也要发 WREN */

    GD25Q_CS_LOW();
    (void)bsp_spi_xfer_byte(SPI0, GD25Q_CMD_SE);
    gd25q_send_addr(addr);
    GD25Q_CS_HIGH();

    return gd25q_wait_busy();           /* 等待擦除完成 */
}
