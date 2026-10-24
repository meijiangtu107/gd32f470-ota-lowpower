/* ADC 采样接口：取一轮同一时刻的 2 通道 12bit 原始值快照 */

#ifndef BSP_ADC_H
#define BSP_ADC_H

#include <stdint.h>

/* 规则组通道数：PC0 = ADC0_CH10，PC1 = ADC0_CH11 */
#define BSP_ADC_CH_COUNT    2U

/* 快照中通道下标 */
#define BSP_ADC_IDX_CH0     0U      /* PC0 */
#define BSP_ADC_IDX_CH1     1U      /* PC1 */

/* 参考电压（mV）。VREF+ 接 3.3V，12bit 满量程 4095 */
#define BSP_ADC_VREF_MV     3300U
#define BSP_ADC_FULL_SCALE  4095U

/* 配置 ADC + DMA + 中断。返回 0 成功 */
int bsp_adc_init(void);

/* 取一份同一轮采样快照：buf 写入 count 个 12bit 原始值（0~4095），count 不超过 BSP_ADC_CH_COUNT。
 * 返回 0 成功；-1 表示参数错误或第一轮数据未到，调用方必须处理。 */
int bsp_adc_get_snapshot(uint16_t *buf, uint8_t count);

/* 中断转发入口，仅由 gd32f4xx_it.c 调用 */
void bsp_adc_dma_isr(void);

#endif /* BSP_ADC_H */
