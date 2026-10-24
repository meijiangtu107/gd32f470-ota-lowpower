/* DAC0_OUT0 -> PA4：不配触发源、直写数据保持寄存器的模拟量输出 */

#include "bsp_dac.h"
#include "bsp_pinmap.h"
#include "bsp_gpio.h"
#include "gd32f4xx.h"

#define DAC_PERIPH      DAC0
#define DAC_OUT         DAC_OUT0
#define DAC_RCU         RCU_DAC

/* 初始化 DAC：PA4 模拟模式，初值 0V */
int bsp_dac_init(void)
{
    /* ① PA4 模拟模式，af=0；模拟模式下引脚不驱动，与 PC1 短接不会冲突 */
    bsp_gpio_config(&BSP_PIN_DAC_OUT);

    /* ② DAC 时钟 + 清除残留配置 */
    rcu_periph_clock_enable(DAC_RCU);
    dac_deinit(DAC_PERIPH);

    /* ③ 触发策略：软件写；只关闭触发，不配置 source、不使能、不操作定时器 */
    dac_trigger_disable(DAC_PERIPH, DAC_OUT);
    dac_wave_mode_config(DAC_PERIPH, DAC_OUT, DAC_WAVE_DISABLE);

    /* ④ 先写 0 再使能：PA4 跳线短接 PC1，使能前先建立确定的 0V 输出 */
    dac_data_set(DAC_PERIPH, DAC_OUT, DAC_ALIGN_12B_R, 0U);
    dac_enable(DAC_PERIPH, DAC_OUT);

    return 0;
}

/* 设置 DAC 输出值 */
int bsp_dac_set(uint16_t value)
{
    if (value > BSP_DAC_FULL_SCALE) {
        return -1;
    }

    /* 传入 12bit 原始值，不要移位；dac_data_set() 直接写 DAC_OUT0_R12DH 的 bit0~11，手动移位会截断高位 */
    dac_data_set(DAC_PERIPH, DAC_OUT, DAC_ALIGN_12B_R, value);

    return 0;
}
