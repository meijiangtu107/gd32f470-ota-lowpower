/* 三通道业务量的维护与访问：CH0/CH1 读板载 ADC 快照后乘变比，
 * CH2 走 AD3344 电压到 PT100 温度的换算链（线性校准 + 点校准 + 查表插值）。 */

#include <math.h>
#include <string.h>

#include "app_adc.h"
#include "bsp_adc.h"
#include "drv_gd30ad3344.h"
#include "param_store.h"

/* 通道数据 */

typedef struct {
    float raw;          /* 原始量（ADC 码值 / 温度） */
    float ratio;
    float threshold;
} app_adc_ch_t;

static app_adc_ch_t s_ch[APP_ADC_CH_COUNT];

/* PT100 换算常数（实物标定值，不可修改）
 * 换算链：电压 -> 仪表放大器 -> RTD 电压 -> 电阻 -> 线性校准 -> 查表温度 */

#define PT100_V_REF         2.0928f     /* 差分放大器参考电压 */
#define PT100_G_DIFF        1.60f
#define PT100_G_INA         10.1827f
#define PT100_I_RTD_MA      1.1198f
#define PT100_CAL_SLOPE     1.143f
#define PT100_CAL_OFFSET   (-6.56f)

typedef struct {
    float resistance;
    float temperature;
} pt100_point_t;

/* 按阻值从小到大排，插值要用 */
static const pt100_point_t PT100_RES_TABLE[] = {
    {  80.6f,  -49.26f},
    {  82.5f,  -44.47f},
    { 100.0f,    0.00f},
    { 107.0f,   17.96f},
    { 113.0f,   33.43f},
    { 115.0f,   38.60f},
    { 118.0f,   46.37f},
    { 124.0f,   61.98f},
    { 130.0f,   77.65f},
    { 140.0f,  103.94f},
    { 150.0f,  130.45f},
    { 154.0f,  141.11f},
};

#define PT100_RES_NUM   (sizeof(PT100_RES_TABLE) / sizeof(PT100_RES_TABLE[0]))

typedef struct {
    float measured;
    float actual;
} pt100_cal_point_t;

static const pt100_cal_point_t PT100_CAL_TABLE[] = {
    { 81.036f,  80.7f },
    { 82.557f,  82.2f },
    {100.570f, 100.4f },
    {107.800f, 107.6f },
    {112.890f, 113.0f },
    {115.365f, 115.3f },
    {124.170f, 124.0f },
    {130.070f, 129.8f },
    {137.950f, 137.7f },
    {149.950f, 149.8f },
    {154.910f, 154.6f },
};

#define PT100_CAL_NUM   (sizeof(PT100_CAL_TABLE) / sizeof(PT100_CAL_TABLE[0]))

/* 线性校准：R_actual = SLOPE * R_calc + OFFSET */
static float pt100_linear_cal(float r_calc)
{
    return PT100_CAL_SLOPE * r_calc + PT100_CAL_OFFSET;
}

/* 点对点校准：在标定表里找区间做线性插值，两端外推 */
static float pt100_point_cal(float r)
{
    uint32_t i;
    const pt100_cal_point_t *p1 = &PT100_CAL_TABLE[PT100_CAL_NUM - 2U];
    const pt100_cal_point_t *p2 = &PT100_CAL_TABLE[PT100_CAL_NUM - 1U];

    if (PT100_CAL_NUM < 2U) {
        return r;
    }

    if (r <= PT100_CAL_TABLE[0].measured) {
        p1 = &PT100_CAL_TABLE[0];
        p2 = &PT100_CAL_TABLE[1];
    } else if (r >= PT100_CAL_TABLE[PT100_CAL_NUM - 1U].measured) {
        p1 = &PT100_CAL_TABLE[PT100_CAL_NUM - 2U];
        p2 = &PT100_CAL_TABLE[PT100_CAL_NUM - 1U];
    } else {
        for (i = 0U; i < (PT100_CAL_NUM - 1U); i++) {
            if ((r >= PT100_CAL_TABLE[i].measured) &&
                (r <= PT100_CAL_TABLE[i + 1U].measured)) {
                p1 = &PT100_CAL_TABLE[i];
                p2 = &PT100_CAL_TABLE[i + 1U];
                break;
            }
        }
    }

    if (p2->measured == p1->measured) {
        return p1->actual;
    }

    return p1->actual + (r - p1->measured) *
                        (p2->actual - p1->actual) /
                        (p2->measured - p1->measured);
}

/* 电阻 -> 温度：段内线性插值，段外取端点 */
static float pt100_res_to_temp_table(float resistance)
{
    uint32_t i;

    if (resistance <= PT100_RES_TABLE[0].resistance) {
        return PT100_RES_TABLE[0].temperature;
    }
    if (resistance >= PT100_RES_TABLE[PT100_RES_NUM - 1U].resistance) {
        return PT100_RES_TABLE[PT100_RES_NUM - 1U].temperature;
    }

    for (i = 0U; i < (PT100_RES_NUM - 1U); i++) {
        if ((resistance >= PT100_RES_TABLE[i].resistance) &&
            (resistance <= PT100_RES_TABLE[i + 1U].resistance)) {
            float r1 = PT100_RES_TABLE[i].resistance;
            float t1 = PT100_RES_TABLE[i].temperature;
            float r2 = PT100_RES_TABLE[i + 1U].resistance;
            float t2 = PT100_RES_TABLE[i + 1U].temperature;

            if (r2 == r1) {
                return t1;
            }
            return t1 + (resistance - r1) * (t2 - t1) / (r2 - r1);
        }
    }

    return PT100_RES_TABLE[0].temperature;
}

/* 公式法：查表落空时的兜底 */
static float pt100_res_to_temp_formula(float r)
{
    const float r0 = 100.0f;
    const float a  = 3.9083e-3f;
    const float b  = -5.775e-7f;
    float ratio;
    float disc;

    ratio = r / r0;
    disc = a * a - 4.0f * b * (1.0f - ratio);
    if (disc < 0.0f) {
        return -999.0f;
    }

    return (-a + sqrtf(disc)) / (2.0f * b);
}

/* 完整换算链：AD3344 电压 -> PT100 温度 */
static float pt100_voltage_to_temp(float volt)
{
    float v_ina;
    float v_rtd;
    float r_raw;
    float r_cal;

    v_ina  = PT100_V_REF - volt / PT100_G_DIFF;
    v_rtd  = v_ina / PT100_G_INA;
    r_raw  = v_rtd / PT100_I_RTD_MA * 1000.0f;

    r_cal = pt100_linear_cal(r_raw);
    r_cal = pt100_point_cal(r_cal);

    if ((r_cal < PT100_RES_TABLE[0].resistance) ||
        (r_cal > PT100_RES_TABLE[PT100_RES_NUM - 1U].resistance)) {
        return pt100_res_to_temp_formula(r_cal);
    }
    return pt100_res_to_temp_table(r_cal);
}

/* 生命周期 */

void app_adc_init(void)
{
    memset(s_ch, 0, sizeof(s_ch));

    s_ch[APP_ADC_CH0].ratio     = param_get_ratio(0U);
    s_ch[APP_ADC_CH0].threshold = param_get_threshold(0U);
    s_ch[APP_ADC_CH1].ratio     = param_get_ratio(1U);
    s_ch[APP_ADC_CH1].threshold = param_get_threshold(1U);

    s_ch[APP_ADC_CH2].ratio     = 1.0f;             /* PT100 不需要变比 */
    s_ch[APP_ADC_CH2].threshold = APP_ADC_CH2_THRESHOLD;
}

void app_adc_update(void)
{
    uint16_t snap[BSP_ADC_CH_COUNT];
    int16_t  raw16;
    float    volt;

    /* CH0 / CH1：快照读取失败（上电第一轮还没到）就保留上次的值 */
    if (bsp_adc_get_snapshot(snap, BSP_ADC_CH_COUNT) == 0) {
        s_ch[APP_ADC_CH0].raw = (float)snap[BSP_ADC_IDX_CH0];
        s_ch[APP_ADC_CH1].raw = (float)snap[BSP_ADC_IDX_CH1];
    }

    /* CH2：AD3344 单端 AIN0-GND */
    raw16 = drv_gd30_read_raw(DRV_GD30_MUX_AIN0_GND);
    volt  = drv_gd30_raw_to_volt(raw16);
    s_ch[APP_ADC_CH2].raw = pt100_voltage_to_temp(volt);
}

/* 访问 */

float app_adc_get_raw(uint8_t channel)
{
    if (channel >= APP_ADC_CH_COUNT) {
        return 0.0f;
    }
    return s_ch[channel].raw;
}

float app_adc_get_value(uint8_t channel)
{
    if (channel >= APP_ADC_CH_COUNT) {
        return 0.0f;
    }
    return s_ch[channel].raw * s_ch[channel].ratio;
}

float app_adc_get_ratio(uint8_t channel)
{
    if (channel >= APP_ADC_CH_COUNT) {
        return 0.0f;
    }
    return s_ch[channel].ratio;
}

void app_adc_set_ratio(uint8_t channel, float ratio)
{
    if (channel >= APP_ADC_CH_COUNT) {
        return;
    }
    s_ch[channel].ratio = ratio;
}

float app_adc_get_threshold(uint8_t channel)
{
    if (channel >= APP_ADC_CH_COUNT) {
        return 0.0f;
    }
    return s_ch[channel].threshold;
}

void app_adc_set_threshold(uint8_t channel, float threshold)
{
    if (channel >= APP_ADC_CH_COUNT) {
        return;
    }
    s_ch[channel].threshold = threshold;
}

int app_adc_over_threshold(uint8_t channel)
{
    if (channel >= APP_ADC_CH_COUNT) {
        return 0;
    }
    return (app_adc_get_value(channel) > s_ch[channel].threshold) ? 1 : 0;
}
