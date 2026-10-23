/* 8x16 ASCII 点阵字库接口 */

#ifndef DRV_OLED_FONT_H
#define DRV_OLED_FONT_H

#include <stdint.h>

#define OLED_FONT_CHARS    96U     /* ASCII 0x20 ~ 0x7F */
#define OLED_FONT_BYTES    16U     /* 8 列 x 16 行 */

/* 索引 = 字符 - 0x20 */
extern const uint8_t OLED_FONT_8X16[OLED_FONT_CHARS][OLED_FONT_BYTES];

#endif /* DRV_OLED_FONT_H */
