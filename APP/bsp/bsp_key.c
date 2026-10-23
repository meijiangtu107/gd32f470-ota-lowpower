/**
 * @file    bsp_key.c
 * @brief   按键扫描与消抖（边沿检测法）
 * @author  meijiangtu
 * @date    2026-09-16
 *
 * 思路：不写消抖延时，靠"每 10ms 采样一次"过滤掉机械抖动。
 *       按下/松开用异或检测边沿，一次按下只产生一个事件。
 *
 *        key_val  本次电平（每位一个键）
 *        key_down = key_val & (key_old ^ key_val)    下降沿 = 刚按下
 *        key_up   = ~key_val & (key_old ^ key_val)   上升沿 = 刚松开
 */

#include "bsp_key.h"
#include "bsp_pinmap.h"
#include "bsp_gpio.h"
#include "gd32f4xx.h"

/* 长按判定：10ms 一次 × 100 = 1 秒 */
#define KEY_LONG_CNT    100U

/* 每位对应一个键，bit0 = KEY1 ... bit5 = KEY6 */
static uint8_t s_key_val  = 0U;     /* 本次采样电平 */
static uint8_t s_key_old  = 0U;     /* 上次采样电平 */
static uint8_t s_key_down = 0U;     /* 本次刚按下的键 */
static uint8_t s_key_up   = 0U;     /* 本次刚松开的键 */
static uint8_t s_key_long = 0U;     /* 本次刚够长按的键 */
static uint8_t s_key_cnt[BSP_KEY_COUNT];    /* 每个键按住多久了 */

/* 把 6 个引脚的电平拼成位图。按下读到低电平，置对应位 */
static uint8_t key_read(void)
{
    uint8_t v = 0U;
    uint32_t i;

    for (i = 0U; i < (uint32_t)BSP_KEY_COUNT; i++) {
        if (RESET == gpio_input_bit_get(BSP_PIN_KEY[i].port, BSP_PIN_KEY[i].pin)) {
            v |= (uint8_t)(1U << i);
        }
    }

    return v;
}

void bsp_key_init(void)
{
    uint32_t i;

    bsp_gpio_config_array(BSP_PIN_KEY, (uint32_t)BSP_KEY_COUNT);

    for (i = 0U; i < (uint32_t)BSP_KEY_COUNT; i++) {
        s_key_cnt[i] = 0U;
    }
}

void bsp_key_scan(void)
{
    uint32_t i;

    s_key_val = key_read();

    /* 异或找出"变了的位"，再和当前值相与 = 变低的位（刚按下） */
    s_key_down = (uint8_t)(s_key_val & (s_key_old ^ s_key_val));
    /* 和取反的当前值相与 = 变高的位（刚松开） */
    s_key_up = (uint8_t)((uint8_t)(~s_key_val) & (s_key_old ^ s_key_val));

    s_key_old = s_key_val;

    /* 长按计数 */
    for (i = 0U; i < (uint32_t)BSP_KEY_COUNT; i++) {
        uint8_t mask = (uint8_t)(1U << i);

        if ((s_key_val & mask) != 0U) {
            if (s_key_cnt[i] < KEY_LONG_CNT) {
                s_key_cnt[i]++;
                if (s_key_cnt[i] == KEY_LONG_CNT) {
                    s_key_long |= mask;     /* 刚到 1 秒，记一次 */
                }
            }
        } else {
            s_key_cnt[i] = 0U;
            s_key_long &= (uint8_t)(~mask);
        }
    }
}

bsp_key_event_t bsp_key_get_event(bsp_key_t key)
{
    uint8_t mask;

    if ((uint32_t)key >= (uint32_t)BSP_KEY_COUNT) {
        return BSP_KEY_EVENT_NONE;
    }

    mask = (uint8_t)(1U << (uint32_t)key);

    /* 长按优先报：按住不放时先出 LONG_PRESS，松开时才出 RELEASE */
    if ((s_key_long & mask) != 0U) {
        s_key_long &= (uint8_t)(~mask);
        return BSP_KEY_EVENT_LONG_PRESS;
    }

    if ((s_key_down & mask) != 0U) {
        s_key_down &= (uint8_t)(~mask);
        return BSP_KEY_EVENT_PRESS;
    }

    if ((s_key_up & mask) != 0U) {
        s_key_up &= (uint8_t)(~mask);
        return BSP_KEY_EVENT_RELEASE;
    }

    return BSP_KEY_EVENT_NONE;
}
