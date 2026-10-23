/* GD30AD3344 / ADS1118 16 位 SPI ADC 驱动接口 */

#ifndef DRV_GD30AD3344_H
#define DRV_GD30AD3344_H

#include <stdint.h>

/* 输入通道选择（MUX 位段） */
typedef enum {
    DRV_GD30_MUX_AIN0_AIN1 = 0,     /* 差分 AIN0-AIN1 */
    DRV_GD30_MUX_AIN0_AIN3 = 1,
    DRV_GD30_MUX_AIN1_AIN3 = 2,
    DRV_GD30_MUX_AIN2_AIN3 = 3,
    DRV_GD30_MUX_AIN0_GND  = 4,     /* 单端 AIN0 对 GND */
    DRV_GD30_MUX_AIN1_GND  = 5,
    DRV_GD30_MUX_AIN2_GND  = 6,
    DRV_GD30_MUX_AIN3_GND  = 7
} drv_gd30_mux_t;

/* 量程（PGA 位段），本驱动固定 ±2.048V */
#define DRV_GD30_FSR_V      2.048f
#define DRV_GD30_FSR_RAW    32768.0f

/* 初始化：配置成连续转换模式，之后随时读都是最新值 */
void drv_gd30_init(void);

/* 读一次 16 位有符号原始值。芯片为流水线结构，读到的是上一次配置的转换结果；
 * 切换通道后第一次读仍是旧通道值，需等一个转换周期。 */
int16_t drv_gd30_read_raw(drv_gd30_mux_t mux);

/* 原始值换算成电压（伏） */
float drv_gd30_raw_to_volt(int16_t raw);

/* 进低功耗前调用：切单次转换且不启动转换，芯片进入 power-down（约 1µA）。
 * 唤醒后需重新调用 drv_gd30_init()。 */
void drv_gd30_powerdown(void);

#endif /* DRV_GD30AD3344_H */
