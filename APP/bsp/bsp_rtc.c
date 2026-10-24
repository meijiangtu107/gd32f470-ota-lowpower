/*
 * RTC：掉电走时 + Unix 秒读写。
 *
 * 【RTC 只存日历，不存 Unix 秒】
 * 硬件寄存器是 BCD 的年/月/日/时/分/秒，读写都要在 BCD 和十进制之间转换。
 * 对上层暴露 Unix 秒，是因为协议和告警记录都用秒数。
 *
 * 【为什么用 BKP0 判断"是否首次上电"】
 * 备份域寄存器（RTC_BKPx）和 RTC 一起由 VBAT 供电。
 * 只要电池在，BKP0 里的魔术字就一直在，说明 RTC 里的时间可信；
 * 如果 BKP0 不是魔术字，或者 RTC 时钟源位是 0（备份域被复位过），
 * 才需要重新写时间。
 *
 * 这个判定不能跳过：每次上电都无条件把时间写回去，走时就被拉回
 * 固定值，"掉电继续走时"等于作废。
 */

#include <stddef.h>

#include "bsp_rtc.h"
#include "gd32f4xx.h"

/* 写进 BKP0 的魔术字。随便挑一个不容易碰巧出现的值 */
#define RTC_BKP_MAGIC       0x32F0A5C3UL

/* 首次上电的默认时间：2026-01-01 00:00:00 UTC */
#define RTC_DEFAULT_UNIX    1767225600UL

/* 1Hz 时钟：32.768kHz ÷ 128 ÷ 256 */
#define RTC_FACTOR_ASYN     0x7FU
#define RTC_FACTOR_SYN      0xFFU

static uint8_t s_ready = 0U;

/*============================================================================
 * BCD <-> 十进制
 *============================================================================*/

static uint8_t rtc_bcd_to_bin(uint8_t bcd)
{
    return (uint8_t)(((bcd >> 4) * 10U) + (bcd & 0x0FU));
}

static uint8_t rtc_bin_to_bcd(uint8_t bin)
{
    return (uint8_t)(((bin / 10U) << 4) | (bin % 10U));
}

/*============================================================================
 * 日期 <-> Unix 秒
 *
 * 用儒略日做中转。
 * 只支持 2000~2099 年 —— RTC 的年份寄存器只有两位 BCD。
 *============================================================================*/

/* 公历日期 -> Unix 秒 */
static uint32_t rtc_date_to_unix(uint32_t year, uint32_t month, uint32_t day,
                                 uint32_t hour, uint32_t minute, uint32_t second)
{
    uint32_t a;
    uint32_t y;
    uint32_t m;
    uint32_t jdn;

    if ((month == 0U) || (month > 12U) || (day == 0U) || (day > 31U)) {
        return 0U;
    }

    a = (14U - month) / 12U;
    y = year + 4800U - a;
    m = month + 12U * a - 3U;
    jdn = day + (153U * m + 2U) / 5U + 365U * y + y / 4U
          - y / 100U + y / 400U - 32045U;

    return (jdn - 2440588U) * 86400U + hour * 3600U + minute * 60U + second;
}

/* Unix 天数 -> 公历（Howard Hinnant 的 civil_from_days，全程无符号） */
static void rtc_days_to_date(uint32_t days,
                             uint32_t *year, uint32_t *month, uint32_t *day)
{
    uint32_t z;
    uint32_t era;
    uint32_t doe;
    uint32_t yoe;
    uint32_t y;
    uint32_t doy;
    uint32_t mp;
    uint32_t d;
    uint32_t m;

    z   = days + 719468U;
    era = z / 146097U;
    doe = z - era * 146097U;
    yoe = (doe - doe / 1460U + doe / 36524U - doe / 146096U) / 365U;
    y   = yoe + era * 400U;
    doy = doe - (365U * yoe + yoe / 4U - yoe / 100U);
    mp  = (5U * doy + 2U) / 153U;
    d   = doy - (153U * mp + 2U) / 5U + 1U;
    m   = (mp < 10U) ? (mp + 3U) : (mp - 9U);
    if (m <= 2U) {
        y++;
    }

    *year  = y;
    *month = m;
    *day   = d;
}

/* 1970-01-01 是星期四；GD32 的 day_of_week 是 1=周一 ~ 7=周日 */
static uint8_t rtc_weekday_from_days(uint32_t days)
{
    return (uint8_t)(((days + 3U) % 7U) + 1U);
}

/*============================================================================
 * 内部：写时间（不检查 s_ready，init 里要用）
 *============================================================================*/
static int rtc_write_unix(uint32_t sec)
{
    rtc_parameter_struct p;
    uint32_t days;
    uint32_t rem;
    uint32_t year;
    uint32_t month;
    uint32_t day;

    /* 写备份寄存器前必须再开一次写保护（备份域复位会把它清掉） */
    pmu_backup_write_enable();

    days = sec / 86400U;
    rem  = sec % 86400U;

    rtc_days_to_date(days, &year, &month, &day);

    if ((year < 2000U) || (year > 2099U)) {
        return -1;
    }

    p.factor_asyn   = RTC_FACTOR_ASYN;
    p.factor_syn    = RTC_FACTOR_SYN;
    p.year          = rtc_bin_to_bcd((uint8_t)(year - 2000U));
    p.month         = rtc_bin_to_bcd((uint8_t)month);
    p.date          = rtc_bin_to_bcd((uint8_t)day);
    p.day_of_week   = rtc_weekday_from_days(days);
    p.hour          = rtc_bin_to_bcd((uint8_t)(rem / 3600U));
    p.minute        = rtc_bin_to_bcd((uint8_t)((rem / 60U) % 60U));
    p.second        = rtc_bin_to_bcd((uint8_t)(rem % 60U));
    p.am_pm         = RTC_AM;
    p.display_format = RTC_24HOUR;

    return (rtc_init(&p) == SUCCESS) ? 0 : -1;
}

/*============================================================================
 * 初始化
 *============================================================================*/
int bsp_rtc_init(void)
{
    uint32_t rtc_src;
    int ret;

    s_ready = 0U;

    /* 备份域写保护。不开这个，RTC_BKP0 和 RTCSEL 都写不进去 */
    rcu_periph_clock_enable(RCU_PMU);
    pmu_backup_write_enable();

    /* LXTAL 起振。板子上没有 32.768kHz 晶振就会超时返回 ERROR */
    rcu_osci_on(RCU_LXTAL);
    if (rcu_osci_stab_wait(RCU_LXTAL) != SUCCESS) {
        return -1;
    }

    /* 直接选时钟源，**不做备份域复位**。
     *
     * 备份域复位会把 LXTALEN 一起清掉，紧接着立刻重开可能被硬件忽略，
     * 随后 rcu_osci_stab_wait 超时，bsp_rtc_is_ready() 恒为 0，
     * 0x03AA 只回错误帧 —— 这条路在板子上起不来。
     *
     * POR 本身已经复位过备份域，RTCSEL 此时可写，无须再复位。 */
    rcu_rtc_clock_config(RCU_RTCSRC_LXTAL);
    rcu_periph_clock_enable(RCU_RTC);

    /* 非致命，不依赖这一句。
     * 真正的寄存器同步在下面 rtc_write_unix() -> rtc_init() 里还会做一次 */
    (void)rtc_register_sync_wait();

    rtc_src = GET_BITS(RCU_BDCTL, 8U, 9U);

    if ((RTC_BKP0 == RTC_BKP_MAGIC) && (rtc_src != 0U)) {
        /* 电池还在、时钟源也在 -> 保留走时，一个字都不改 */
        s_ready = 1U;
        return 0;
    }

    /* 首次上电：写默认时间并做标记 */
    ret = rtc_write_unix(RTC_DEFAULT_UNIX);
    if (ret != 0) {
        return -1;
    }
    RTC_BKP0 = RTC_BKP_MAGIC;

    s_ready = 1U;
    return 1;
}

int bsp_rtc_is_ready(void)
{
    return (s_ready != 0U) ? 1 : 0;
}

/*============================================================================
 * 对外读写
 *============================================================================*/
int bsp_rtc_set_unix(uint32_t sec)
{
    if (s_ready == 0U) {
        return -1;
    }
    return rtc_write_unix(sec);
}

int bsp_rtc_get_unix(uint32_t *sec)
{
    rtc_parameter_struct p;
    uint32_t year;
    uint32_t month;
    uint32_t day;
    uint32_t hour;
    uint32_t minute;
    uint32_t second;

    if ((sec == NULL) || (s_ready == 0U)) {
        return -1;
    }

    /* 读影子寄存器前先等一次同步，否则可能读到半更新的值 */
    (void)rtc_register_sync_wait();
    rtc_current_time_get(&p);

    year   = 2000U + rtc_bcd_to_bin(p.year);
    month  = rtc_bcd_to_bin(p.month);
    day    = rtc_bcd_to_bin(p.date);
    hour   = rtc_bcd_to_bin(p.hour);
    minute = rtc_bcd_to_bin(p.minute);
    second = rtc_bcd_to_bin(p.second);

    *sec = rtc_date_to_unix(year, month, day, hour, minute, second);
    return 0;
}