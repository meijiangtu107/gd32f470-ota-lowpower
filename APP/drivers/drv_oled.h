/* SSD1306 OLED 驱动接口（I2C）：仅负责初始化、写命令、写数据、定位、清屏；
 * 排版在 app 层，字库在 drv_oled_font.c。 */

#ifndef DRV_OLED_H
#define DRV_OLED_H

#include <stdint.h>

/* 屏高：32 = 128x32，64 = 128x64；复用比与 COM 引脚配置随之自动变化 */
#define OLED_HEIGHT     32U

#define OLED_WIDTH      128U
#define OLED_PAGES      (OLED_HEIGHT / 8U)      /* 页数 = 高度 / 8 */

/* 初始化。返回 0 成功，负值表示从机无应答（没接好或地址不对） */
int drv_oled_init(void);

/* 写一条命令 */
int drv_oled_write_cmd(uint8_t cmd);

/* 写一段显示数据 */
int drv_oled_write_data(const uint8_t *data, uint16_t len);

/* 把光标定位到第 page 页、第 x 列。page 0~(OLED_PAGES-1)，x 0~127 */
int drv_oled_set_pos(uint8_t page, uint8_t x);

/* 整屏填充同一个字节。0x00 = 全黑，0xFF = 全亮 */
int drv_oled_fill(uint8_t pattern);

/* 显存刷到屏幕：把 buf（OLED_WIDTH * OLED_PAGES 字节）整屏写过去 */
int drv_oled_flush(const uint8_t *buf);

/* 关显示并关电荷泵，进低功耗前调用；再次显示需先 drv_oled_init()。
 * 从机不在线时返回负值。 */
int drv_oled_display_off(void);

#endif /* DRV_OLED_H */
