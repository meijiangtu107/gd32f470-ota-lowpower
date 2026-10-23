/* SSD1306 OLED 驱动（I2C）。控制字节 0x00 = 命令，0x40 = 显示数据。
 * 显存按页组织，每页 8 行像素，一列 1 字节（低位在上）。 */

#include "drv_oled.h"
#include <stddef.h>
#include "bsp_i2c.h"
#include "bsp_tick.h"

#define OLED_I2C_ADDR       0x78U       /* 8 位写地址（7 位是 0x3C） */
#define OLED_CTRL_CMD       0x00U
#define OLED_CTRL_DATA      0x40U

/* 上电后等 OLED 内部稳压稳定 */
#define OLED_POWER_ON_MS    100U

/* 复用比和 COM 引脚配置跟屏高绑定，改 OLED_HEIGHT 就自动跟着变 */
#if (OLED_HEIGHT == 32)
#define OLED_MUX_RATIO      0x1FU        /* 32 行 - 1 */
#define OLED_COM_PINS       0x02U        /* 32 行用 0x02 */
#elif (OLED_HEIGHT == 64)
#define OLED_MUX_RATIO      0x3FU        /* 64 行 - 1 */
#define OLED_COM_PINS       0x12U        /* 64 行用 0x12 */
#else
#error "OLED_HEIGHT 只支持 32 或 64"
#endif

/* 初始化命令序列，复用比与 COM 配置随屏高变化 */
static const uint8_t s_init_cmds[] = {
    0xAEU,                  /* 关显示 */
    0xD5U, 0x80U,           /* 时钟分频/振荡频率 */
    0xA8U, OLED_MUX_RATIO,  /* 复用比 */
    0xD3U, 0x00U,           /* 显示偏移 */
    0x40U,                  /* 起始行 = 0 */
    0x8DU, 0x14U,           /* 电荷泵开 */
    0xA1U,                  /* 段重映射 */
    0xC8U,                  /* COM 扫描方向：从下往上 */
    0xDAU, OLED_COM_PINS,   /* COM 引脚配置 */
    0x81U, 0x80U,           /* 对比度 */
    0xD9U, 0x1FU,           /* 预充电周期 */
    0xDBU, 0x40U,           /* VCOMH 反选电平 */
    0xA4U,                  /* 显示跟随显存（不是全亮） */
    0xAFU                   /* 开显示 */
};

int drv_oled_write_cmd(uint8_t cmd)
{
    return bsp_i2c_write(OLED_I2C_ADDR, OLED_CTRL_CMD, &cmd, 1U);
}

int drv_oled_write_data(const uint8_t *data, uint16_t len)
{
    return bsp_i2c_write(OLED_I2C_ADDR, OLED_CTRL_DATA, data, len);
}

int drv_oled_init(void)
{
    uint16_t i;
    int ret;

    /* 等待 OLED 内部电荷泵稳定 */
    bsp_delay_ms(OLED_POWER_ON_MS);

    for (i = 0U; i < (uint16_t)sizeof(s_init_cmds); i++) {
        ret = drv_oled_write_cmd(s_init_cmds[i]);
        if (ret != 0) {
            return ret;     /* 从机无应答则退出 */
        }
    }

    return drv_oled_fill(0x00U);
}

int drv_oled_set_pos(uint8_t page, uint8_t x)
{
    int ret;

    if ((page >= OLED_PAGES) || (x >= OLED_WIDTH)) {
        return -1;
    }

    ret = drv_oled_write_cmd((uint8_t)(0xB0U + page));          /* 页地址 */
    if (ret != 0) {
        return ret;
    }
    ret = drv_oled_write_cmd((uint8_t)(0x10U | (x >> 4)));      /* 列高 4 位 */
    if (ret != 0) {
        return ret;
    }
    return drv_oled_write_cmd((uint8_t)(x & 0x0FU));            /* 列低 4 位 */
}

int drv_oled_fill(uint8_t pattern)
{
    uint8_t  buf[OLED_WIDTH];
    uint8_t  page;
    uint16_t i;

    for (i = 0U; i < OLED_WIDTH; i++) {
        buf[i] = pattern;
    }

    for (page = 0U; page < OLED_PAGES; page++) {
        if (drv_oled_set_pos(page, 0U) != 0) {
            return -1;
        }
        if (drv_oled_write_data(buf, OLED_WIDTH) != 0) {
            return -1;
        }
    }

    return 0;
}

int drv_oled_flush(const uint8_t *buf)
{
    uint8_t page;

    if (buf == NULL) {
        return -1;
    }

    for (page = 0U; page < OLED_PAGES; page++) {
        if (drv_oled_set_pos(page, 0U) != 0) {
            return -1;
        }
        if (drv_oled_write_data(&buf[(uint16_t)page * OLED_WIDTH], OLED_WIDTH) != 0) {
            return -1;
        }
    }

    return 0;
}

/* 低功耗：先关显示，再关电荷泵（电荷泵典型 100~300µA，顺序不可颠倒） */
int drv_oled_display_off(void)
{
    if (drv_oled_write_cmd(0xAEU) != 0) {   /* display off */
        return -1;
    }
    if (drv_oled_write_cmd(0x8DU) != 0) {   /* charge pump setting */
        return -1;
    }
    if (drv_oled_write_cmd(0x10U) != 0) {   /* charge pump off */
        return -1;
    }
    return 0;
}
