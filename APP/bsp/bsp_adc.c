/* ADC0 采集链：TIMER1 TRGO 硬件触发规则组，DMA1_CH4 循环搬运。
 * 两路输入 PC0/PC1 = CH10/CH11，主循环只碰冻结快照，不碰 DMA 缓冲区。 */

#include "bsp_adc.h"
#include <stddef.h>
#include "bsp_pinmap.h"
#include "bsp_gpio.h"
#include "bsp_tick.h"
#include "gd32f4xx.h"

#define ADC_PERIPH      ADC0
#define ADC_RCU         RCU_ADC0

#define ADC_DMA         DMA1
#define ADC_DMA_CH      DMA_CH4
#define ADC_DMA_RCU     RCU_DMA1
#define ADC_DMA_SUB     DMA_SUBPERI0

/* DMA 中断优先级 5：0 系统关键 / 1 SysTick / 2 USART0 / 3 USART1 / 4 USART2 / 5 DMA·SPI·I2C */
#define ADC_DMA_IRQ_PRIO    5U

/* ADC 时钟 = APB2/8，APB2=120MHz → 15MHz */
#define ADC_CLK_DIV         ADC_ADCCK_PCLK2_DIV8

/* 采样时间 144 个 ADC 周期：(144+12)/15MHz ≈ 10.4µs/通道，一轮约 21µs。
 * 15 周期（1µs）在源阻抗较大时采样电容充不满，读数会系统性偏低。 */
#define ADC_SAMPLE_TIME     ADC_SAMPLETIME_144

/* 三段式数据链路：DMA 缓冲区 → FTF 冻结副本 → 主循环快照 */

/* DMA 目标缓冲区；主循环不得直接读取 */
static uint16_t s_adc_buf[BSP_ADC_CH_COUNT];

/* 冻结副本，只在 FTF 中断中写入 */
static volatile uint16_t s_adc_stable[BSP_ADC_CH_COUNT];
static volatile uint8_t  s_adc_ready = 0U;

int bsp_adc_init(void)
{
    dma_single_data_parameter_struct dma_cfg;

    /* ① 引脚：模拟输入。表驱动，配置在 bsp_gpio.c */
    bsp_gpio_config(&BSP_PIN_ADC_CH0);
    bsp_gpio_config(&BSP_PIN_ADC_CH1);

    /* ② 时钟 + 复位外设；顺序：开时钟 → 复位 → 配置 */
    rcu_periph_clock_enable(ADC_RCU);
    rcu_periph_clock_enable(ADC_DMA_RCU);
    adc_deinit();
    adc_clock_config(ADC_CLK_DIV);

    /* ③ DMA：外设地址固定，内存递增，16bit，搬 2 个，循环模式。
     *    必须先调用 dma_single_data_para_struct_init()；dma_single_data_mode_init() 会读取 circular_mode，未初始化即读栈上野值。 */
    dma_deinit(ADC_DMA, ADC_DMA_CH);
    dma_single_data_para_struct_init(&dma_cfg);

    dma_cfg.periph_addr         = (uint32_t)(&ADC_RDATA(ADC_PERIPH));
    dma_cfg.periph_inc          = DMA_PERIPH_INCREASE_DISABLE;
    dma_cfg.memory0_addr        = (uint32_t)s_adc_buf;
    dma_cfg.memory_inc          = DMA_MEMORY_INCREASE_ENABLE;
    dma_cfg.periph_memory_width = DMA_PERIPH_WIDTH_16BIT;
    dma_cfg.circular_mode       = DMA_CIRCULAR_MODE_ENABLE;
    dma_cfg.direction           = DMA_PERIPH_TO_MEMORY;
    dma_cfg.number              = BSP_ADC_CH_COUNT;
    dma_cfg.priority            = DMA_PRIORITY_HIGH;
    dma_single_data_mode_init(ADC_DMA, ADC_DMA_CH, &dma_cfg);

    /* 循环模式已经在结构体里配好，这里不用再调 dma_circulation_enable() */
    dma_channel_subperipheral_select(ADC_DMA, ADC_DMA_CH, ADC_DMA_SUB);

    /* ④ 一轮搬完（FTF）进中断做冻结副本；DMA 由硬件写内存，__disable_irq() 挡不住，不能直接读 s_adc_buf */
    dma_flag_clear(ADC_DMA, ADC_DMA_CH, DMA_FLAG_FTF);
    dma_interrupt_enable(ADC_DMA, ADC_DMA_CH, DMA_INT_FTF);
    nvic_irq_enable(DMA1_Channel4_IRQn, ADC_DMA_IRQ_PRIO, 0U);
    dma_channel_enable(ADC_DMA, ADC_DMA_CH);

    /* ⑤ ADC 本体 */
    adc_sync_mode_config(ADC_SYNC_MODE_INDEPENDENT);
    adc_special_function_config(ADC_PERIPH, ADC_SCAN_MODE, ENABLE);
    /* 使用外部触发时必须关闭连续模式，否则触发一次后会持续转换 */
    adc_special_function_config(ADC_PERIPH, ADC_CONTINUOUS_MODE, DISABLE);
    adc_data_alignment_config(ADC_PERIPH, ADC_DATAALIGN_RIGHT);

    adc_channel_length_config(ADC_PERIPH, ADC_ROUTINE_CHANNEL, BSP_ADC_CH_COUNT);
    adc_routine_channel_config(ADC_PERIPH, 0U, ADC_CHANNEL_10, ADC_SAMPLE_TIME);  /* PC0 */
    adc_routine_channel_config(ADC_PERIPH, 1U, ADC_CHANNEL_11, ADC_SAMPLE_TIME);  /* PC1 —— DAC 回读点 */

    /* ⑥ 触发源为 TIMER1 TRGO；GD32 没有 EXTERNAL_TRIGGER_ENABLE，使能外部触发需传 EXTERNAL_TRIGGER_RISING。
     *    外部触发使能后 adc_software_trigger_enable() 会被硬件忽略，不报错也不置标志。 */
    adc_external_trigger_source_config(ADC_PERIPH, ADC_ROUTINE_CHANNEL,
                                       ADC_EXTTRIG_ROUTINE_T1_TRGO);
    adc_external_trigger_config(ADC_PERIPH, ADC_ROUTINE_CHANNEL,
                                EXTERNAL_TRIGGER_RISING);

    adc_dma_request_after_last_enable(ADC_PERIPH);
    adc_dma_mode_enable(ADC_PERIPH);

    /* ⑦ 顺序：adc_enable() → 等 1ms → adc_calibration_enable()；校准早于使能时校准值恒为 0 且不报错 */
    adc_enable(ADC_PERIPH);
    bsp_delay_ms(1U);
    adc_calibration_enable(ADC_PERIPH);

    return 0;
}

/* FTF 中断：DMA 一轮搬完后做冻结副本 */
void bsp_adc_dma_isr(void)
{
    if (RESET == dma_flag_get(ADC_DMA, ADC_DMA_CH, DMA_FLAG_FTF)) {
        return;     /* 不是我们的中断源，别乱清标志 */
    }
    dma_flag_clear(ADC_DMA, ADC_DMA_CH, DMA_FLAG_FTF);

    /* 中断上下文中拷贝，两个值来自同一轮采样 */
    s_adc_stable[0] = s_adc_buf[0];
    s_adc_stable[1] = s_adc_buf[1];
    s_adc_ready = 1U;
}

/* 主循环读取冻结副本 */
int bsp_adc_get_snapshot(uint16_t *buf, uint8_t count)
{
    uint32_t primask;
    uint8_t  i;

    if ((buf == NULL) || (count == 0U) || (count > BSP_ADC_CH_COUNT)) {
        return -1;
    }
    if (s_adc_ready == 0U) {
        return -1;      /* 第一轮还没采到，别把 0 当成真实值 */
    }

    /* 关中断只挡住中断中的拷贝；DMA 写 s_adc_buf，本函数读 s_adc_stable，互不干扰 */
    primask = __get_PRIMASK();
    __disable_irq();
    for (i = 0U; i < count; i++) {
        buf[i] = s_adc_stable[i];
    }
    __set_PRIMASK(primask);

    return 0;
}
