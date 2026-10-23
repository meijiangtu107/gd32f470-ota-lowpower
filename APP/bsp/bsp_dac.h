/* DAC0_OUT0（PA4）：软件写的模拟量输出，PA4 跳线到 PC1 作为回读点 */

#ifndef BSP_DAC_H
#define BSP_DAC_H

#include <stdint.h>

/* 12bit 满量程；输出 = value / 4096 × VREF，LSB ≈ 0.806mV */
#define BSP_DAC_FULL_SCALE  4095U
#define BSP_DAC_VREF_MV     3300U

/* 配置 PA4、使能 DAC0，初值 0V。返回 0 成功 */
int bsp_dac_init(void);

/* 设置 DAC 输出：value 为 0~BSP_DAC_FULL_SCALE 的 12bit 右对齐原始值，不要移位。
 * 返回 0 成功，-1 越界；值域校验在此完成。 */
int bsp_dac_set(uint16_t value);

#endif /* BSP_DAC_H */
