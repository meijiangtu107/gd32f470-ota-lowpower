/* GD30AD3344 / ADS1118 16 位 SPI ADC：每次传输 16 位进、16 位出，发出配置字，收回转换结果。
 * 配置位：bit15 SS，bit14-12 MUX，bit11-9 PGA，bit8 MODE，bit7-5 DR，
 * bit4 RESERVED_1，bit3 PULL_UP，bit2-1 NOP，bit0 RESERVED。 */

#include "drv_gd30ad3344.h"
#include "bsp_spi.h"
#include "bsp_pinmap.h"
#include "gd32f4xx.h"

/* 固定位：SS=0，PGA=2(±2.048V)，MODE=0(连续)，DR=2(25SPS)，
 * RESERVED_1=0，PULL_UP=0，NOP=1(更新配置)，RESERVED=1 → 0x0443。
 * bit4(RESERVED_1) 必须为 0：ADS1118 语义下该位是 TS_MODE，写 1 会返回芯片温度。 */
#define GD30_CFG_FIXED      0x0443U

/* 通道位在 bit14-12 */
#define GD30_CFG_MUX_SHIFT  12U

#define GD30_CS_LOW()       gpio_bit_reset(BSP_PIN_SPI3_CS.port, BSP_PIN_SPI3_CS.pin)
#define GD30_CS_HIGH()      gpio_bit_set(BSP_PIN_SPI3_CS.port, BSP_PIN_SPI3_CS.pin)

/* 拼配置字 */
static uint16_t gd30_make_cfg(drv_gd30_mux_t mux)
{
    return (uint16_t)(GD30_CFG_FIXED | ((uint16_t)mux << GD30_CFG_MUX_SHIFT));
}

/* 发 16 位，同时收 16 位。高位先出 */
static uint16_t gd30_xfer16(uint16_t tx)
{
    uint8_t hi;
    uint8_t lo;

    hi = bsp_spi_xfer_byte(SPI3, (uint8_t)(tx >> 8));
    lo = bsp_spi_xfer_byte(SPI3, (uint8_t)tx);

    return (uint16_t)(((uint16_t)hi << 8) | (uint16_t)lo);
}

void drv_gd30_init(void)
{
    /* 首次配置进入连续转换，本次数据无效丢弃 */
    GD30_CS_LOW();
    (void)gd30_xfer16(gd30_make_cfg(DRV_GD30_MUX_AIN0_GND));
    GD30_CS_HIGH();
}

int16_t drv_gd30_read_raw(drv_gd30_mux_t mux)
{
    uint16_t rx;

    GD30_CS_LOW();
    rx = gd30_xfer16(gd30_make_cfg(mux));
    GD30_CS_HIGH();

    return (int16_t)rx;
}

float drv_gd30_raw_to_volt(int16_t raw)
{
    return ((float)raw * DRV_GD30_FSR_V) / DRV_GD30_FSR_RAW;
}

void drv_gd30_powerdown(void)
{
    /* 置 bit8 改为单次转换，SS=0 不启动转换，芯片进入 power-down；本次传输即生效 */
    GD30_CS_LOW();
    (void)gd30_xfer16((uint16_t)(GD30_CFG_FIXED |
                                 ((uint16_t)DRV_GD30_MUX_AIN0_GND << GD30_CFG_MUX_SHIFT) |
                                 (1U << 8)));
    GD30_CS_HIGH();
}
