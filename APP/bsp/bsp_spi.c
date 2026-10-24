/* SPI 总线：两条链路（SPI0 → GD25Qxx Flash，SPI3 → GD30AD3344），轮询收发。
 * 不用 DMA —— 挂在总线上的器件一次只读写几个字节，DMA 的配置开销比轮询还大。
 * 轮询方式下不存在"FTF 早于 TRANS"的 CS 时机坑：SPI_FLAG_RBNE 置起时字节
 * 已经完整收完；将来若改用 DMA 传输，必须补等 SPI_FLAG_TRANS 清零再拉 CS。 */

#include "bsp_spi.h"
#include "bsp_pinmap.h"
#include "bsp_gpio.h"
#include "gd32f4xx.h"

/*============================================================================
 * SPI0：GD25Qxx Flash，PB3/PB4/PB5，AF5
 *============================================================================*/
#define SPI0_PERIPH     SPI0
#define SPI0_RCU        RCU_SPI0

static void spi_param_config(uint32_t spi, uint32_t ck_pl_ph, uint32_t psc)
{
    spi_parameter_struct spi_cfg;

    spi_i2s_deinit(spi);
    spi_struct_para_init(&spi_cfg);

    spi_cfg.trans_mode           = SPI_TRANSMODE_FULLDUPLEX;
    spi_cfg.device_mode          = SPI_MASTER;
    spi_cfg.frame_size           = SPI_FRAMESIZE_8BIT;
    spi_cfg.clock_polarity_phase = ck_pl_ph;
    spi_cfg.nss                  = SPI_NSS_SOFT;    /* NSS 软件控制，CS 用普通 GPIO */
    spi_cfg.prescale             = psc;
    spi_cfg.endian               = SPI_ENDIAN_MSB;

    spi_init(spi, &spi_cfg);
    spi_enable(spi);
}

int bsp_spi0_init(void)
{
    /* ① 引脚。PB3/PB4/PB5 走 pinmap，AF5 */
    bsp_gpio_config(&BSP_PIN_SPI0_SCK);
    bsp_gpio_config(&BSP_PIN_SPI0_MISO);
    bsp_gpio_config(&BSP_PIN_SPI0_MOSI);

    /* ② 片选也是普通 GPIO，空闲时拉高 */
    bsp_gpio_config(&BSP_PIN_SPI0_CS);
    gpio_bit_set(BSP_PIN_SPI0_CS.port, BSP_PIN_SPI0_CS.pin);

    /* ③ SPI0 自己的时钟。pinmap 里只有 GPIO 时钟，这行不能省 */
    rcu_periph_clock_enable(SPI0_RCU);

    /* ④ 参数：Flash 支持模式 3，SPI0 在 APB2 = 120MHz，PSC 8 → 15MHz */
    spi_param_config(SPI0_PERIPH, SPI_CK_PL_HIGH_PH_2EDGE, SPI_PSC_8);

    return 0;
}

/*============================================================================
 * SPI3：GD30AD3344，PE12/PE13/PE14，AF5
 *============================================================================*/
#define SPI3_PERIPH     SPI3
#define SPI3_RCU        RCU_SPI3

int bsp_spi3_init(void)
{
    /* ① 引脚。PE12/PE13/PE14 走 pinmap，AF5 */
    bsp_gpio_config(&BSP_PIN_SPI3_SCK);
    bsp_gpio_config(&BSP_PIN_SPI3_MISO);
    bsp_gpio_config(&BSP_PIN_SPI3_MOSI);

    /* ② 片选也是普通 GPIO，空闲时拉高 */
    bsp_gpio_config(&BSP_PIN_SPI3_CS);
    gpio_bit_set(BSP_PIN_SPI3_CS.port, BSP_PIN_SPI3_CS.pin);

    /* ③ SPI3 自己的时钟 */
    rcu_periph_clock_enable(SPI3_RCU);

    /* ④ 参数：AD3344 用模式 1；SPI3 在 APB1 = 60MHz，
     *    PSC 256 → 234kHz。慢才安全，ADS1118 类芯片的时钟上限受电源电压限制 */
    spi_param_config(SPI3_PERIPH, SPI_CK_PL_LOW_PH_2EDGE, SPI_PSC_256);

    return 0;
}

/*============================================================================
 * 收发
 *============================================================================*/

uint8_t bsp_spi_xfer_byte(uint32_t spi, uint8_t tx)
{
    /* 等发送缓冲区空，写一个字节出去 */
    while (RESET == spi_i2s_flag_get(spi, SPI_FLAG_TBE)) {
    }
    spi_i2s_data_transmit(spi, tx);

    /* 等收到一个字节。全双工，发的同时也在收 */
    while (RESET == spi_i2s_flag_get(spi, SPI_FLAG_RBNE)) {
    }

    return (uint8_t)spi_i2s_data_receive(spi);
}

void bsp_spi_xfer(uint32_t spi, const uint8_t *tx, uint8_t *rx, uint16_t n)
{
    uint16_t i;
    uint8_t  dummy;

    for (i = 0U; i < n; i++) {
        dummy = bsp_spi_xfer_byte(spi, (tx != NULL) ? tx[i] : 0xFFU);

        if (rx != NULL) {
            rx[i] = dummy;
        }
    }
}
