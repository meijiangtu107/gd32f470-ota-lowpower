/*
 * RTC 接口：掉电走时 + Unix 秒读写。
 *
 * 时钟源是外部 32.768kHz 晶振（LXTAL），
 * 异步预分频 0x7F（÷128）+ 同步预分频 0xFF（÷256）→ 1Hz。
 *
 * 备份域由 VBAT（纽扣电池）供电，所以主电源断了时间照样走。
 * 前提是板子上真的接了电池 —— 没电池的话，断电后 RTC 也会停。
 */

#ifndef BSP_RTC_H
#define BSP_RTC_H

#include <stdint.h>

/* 上电初始化。
 * 返回： 0 = 已有有效时间，原样保留
 *        1 = 首次上电（或备份域丢过电），时间和 BKP0 已重新写入
 *       -1 = LXTAL 不起振或 RTC 配置失败
 *
 * "是否首次配置"必须判定（依据 BKP0 魔术字 + 时钟源位）：
 * 跳过判定会让每次上电都重设时间，这正是"断电后时间被重置"的根因。 */
int bsp_rtc_init(void);

/* 1 = RTC 可用；0 = 初始化失败，不要依赖 RTC 时间 */
int bsp_rtc_is_ready(void);

/* 读写 Unix 秒（UTC）。成功返回 0，失败返回 -1 */
int bsp_rtc_set_unix(uint32_t sec);
int bsp_rtc_get_unix(uint32_t *sec);

#endif /* BSP_RTC_H */