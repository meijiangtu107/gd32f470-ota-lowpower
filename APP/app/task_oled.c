/* OLED 显示：本地维护一份显存镜像，按 8x16 字库排版字符串，
 * 刷新时整屏交给 drv_oled。 */

#include "tasks.h"
#include "drv_oled.h"
#include "drv_oled_font.h"
#include "bsp_tick.h"

/* 显存镜像。128 列 x OLED_PAGES 页 */
static uint8_t s_gram[OLED_WIDTH * OLED_PAGES];

void task_oled_init(void)
{
    uint16_t i;

    for (i = 0U; i < (uint16_t)sizeof(s_gram); i++) {
        s_gram[i] = 0x00U;
    }

    (void)drv_oled_init();
}

/* 在 (page, x) 画一个 8x16 字符。
 * 8x16 占两页，所以最后一行画不下时只画上半页 */
static void oled_draw_char(uint8_t page, uint8_t x, char c)
{
    uint8_t idx;
    uint8_t col;

    if ((c < 0x20) || (c > 0x7F)) {
        c = ' ';
    }
    idx = (uint8_t)(c - 0x20);

    if ((page >= OLED_PAGES) || (x > (OLED_WIDTH - 8U))) {
        return;                     /* 越界，直接丢 */
    }

    /* 字库一个字符 16 字节：前 8 个是上半页，后 8 个是下半页 */
    for (col = 0U; col < 8U; col++) {
        s_gram[(uint16_t)page * OLED_WIDTH + x + col] = OLED_FONT_8X16[idx][col];
        if ((page + 1U) < OLED_PAGES) {
            s_gram[(uint16_t)(page + 1U) * OLED_WIDTH + x + col] =
                OLED_FONT_8X16[idx][8U + col];
        }
    }
}

/* 在 (page, x) 画一串字符 */
static void oled_draw_str(uint8_t page, uint8_t x, const char *str)
{
    while (*str != '\0') {
        oled_draw_char(page, x, *str);
        x = (uint8_t)(x + 8U);
        str++;
    }
}

void task_oled_run(void)
{
    char line[20];
    uint32_t sec;
    char tmp[12];
    int  i;
    int  len;

    /* 第一行：固定字符串 */
    oled_draw_str(0U, 0U, "ximenzi GD32F470");

    /* 最后一行显示运行秒数；OLED_PAGES-2 使 8x16 字符完整占两页 */
    sec = bsp_tick_ms() / 1000U;
    i = 0;
    if (sec == 0U) {
        tmp[i++] = '0';
    } else {
        while ((sec > 0U) && (i < 10)) {
            tmp[i++] = (char)('0' + (sec % 10U));
            sec /= 10U;
        }
    }

    len = 0;
    line[len++] = 't';
    line[len++] = '=';
    while (i > 0) {
        line[len++] = tmp[--i];
    }
    line[len++] = 's';
    line[len] = '\0';

    oled_draw_str((uint8_t)(OLED_PAGES - 2U), 0U, line);

    (void)drv_oled_flush(s_gram);
}
