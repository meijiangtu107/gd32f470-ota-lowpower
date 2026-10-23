/* 三通道业务量：CH0 PC0 电位器，CH1 PC1 DAC 回读，CH2 AD3344 AIN0-GND -> PT100 温度。
 * 上报值 = raw_value * ratio，不是电压。 */

#ifndef APP_ADC_H
#define APP_ADC_H

#include <stdint.h>

#define APP_ADC_CH_COUNT    3U

#define APP_ADC_CH0         0U      /* 电位器 */
#define APP_ADC_CH1         1U      /* DAC 回读 */
#define APP_ADC_CH2         2U      /* PT100 温度 */

/* CH2 默认阈值：150.0℃（赛题量程 -60~152℃） */
#define APP_ADC_CH2_THRESHOLD   150.0f

/* 上电读参数：CH0/CH1 的变比与阈值从参数页来，CH2 固定 */
void app_adc_init(void);

/* task_adc 周期调用：读 ADC 快照 + AD3344，更新三个通道的 raw_value；
 * 读失败时保留上一次的值 */
void app_adc_update(void);

float app_adc_get_raw(uint8_t channel);
float app_adc_get_value(uint8_t channel);       /* raw * ratio */

float app_adc_get_ratio(uint8_t channel);
void  app_adc_set_ratio(uint8_t channel, float ratio);

float app_adc_get_threshold(uint8_t channel);
void  app_adc_set_threshold(uint8_t channel, float threshold);

/* 当前值是否超过阈值 */
int app_adc_over_threshold(uint8_t channel);

#endif /* APP_ADC_H */
