/**
 * @file    bsp_pmu.c
 * @brief   低功耗：Deep-sleep 进出 + 唤醒恢复
 * @author  meijiangtu
 * @date    2026-10-22
 *
 * 【进入睡眠前为什么要一路降频】
 * GD 官方例程 Examples/PMU/Deepsleep_wakeup_RTC 在 __WFI() 之前做了
 * AHB /2 → /4 → /8 → /16 → 切 IRC16M 的逐级降频，注释原文：
 *
 *   "software delay to prevent the impact of Vcore fluctuations.
 *    It is strongly recommended to include it to avoid issues caused
 *    by self-removal."
 *
 * 翻译：240MHz 直接掉进低功耗 LDO 的 deep-sleep，内核电压会抖，
 * 可能自己把自己搞死。逐级降频是为了让电压平稳过渡。
 *
 * 代价：降频之后串口波特率全乱，所以所有串口发送必须在降频之前完成。
 *
 * 【醒来后为什么要 SystemInit() + 立刻改 VTOR】
 * 睡前把时钟降到了 IRC16M，醒来必须恢复 240MHz -> 调用 SystemInit()。
 * 但 SystemInit() 最后会执行：
 *     nvic_vector_table_set(NVIC_VECTTAB_FLASH, VECT_TAB_OFFSET)
 * 而 VECT_TAB_OFFSET = 0，等于把 VTOR 写回 0x08000000（Bootloader 区）。
 * 如果此时开中断，第一个中断就会跳进 BL 的向量表 -> HardFault。
 * 所以顺序必须是：关中断 -> SystemInit -> 恢复 VTOR -> 开中断。
 */

#include "bsp_pmu.h"
#include "bsp_pinmap.h"
#include "bsp_rtc.h"
#include "bsp_tick.h"
#include "gd32f4xx.h"

/* 唤醒后保存/恢复 VTOR 用（A/B 槽基址不同，不能写死） */
static uint32_t s_vtor_save;
static bsp_pmu_restore_fn_t s_restore;

/*============================================================================
 * 工具
 *============================================================================ */

/* 固定循环延时。降频后不能用 bsp_delay_us（它按 240MHz 算周期） */
static void pmu_soft_delay(uint32_t n)
{
    volatile uint32_t i;

    for (i = 0U; i < (n * 10U); i++) {
    }
}

/* 引脚切成模拟态：输入缓冲关闭，不随电平翻转耗电 */
static void pmu_pin_analog(const bsp_pin_t *p)
{
    rcu_periph_clock_enable(p->rcu);
    gpio_mode_set(p->port, GPIO_MODE_ANALOG, GPIO_PUPD_NONE, p->pin);
}

/* 引脚保持推挽输出，并给定电平 */
static void pmu_pin_output(const bsp_pin_t *p, uint8_t level)
{
    rcu_periph_clock_enable(p->rcu);
    gpio_mode_set(p->port, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, p->pin);
    gpio_output_options_set(p->port, GPIO_OTYPE_PP, GPIO_OSPEED_2MHZ, p->pin);

    if (level != 0U) {
        gpio_bit_set(p->port, p->pin);
    } else {
        gpio_bit_reset(p->port, p->pin);
    }
}

/*============================================================================
 * 睡前：关外设
 *============================================================================ */

/* 每个外设都按"中断 -> NVIC -> DMA 请求 -> DMA 通道 -> 外设本体"关闭。
 * 只关外设不关中断，残留标志可能立刻把 CPU 叫醒；只关中断不关外设，
 * 硬件照样耗电。两层都要关。 */
static void pmu_periph_off(void)
{
    /* USART0：DMA1_CH5 */
    usart_interrupt_disable(USART0, USART_INT_IDLE);
    nvic_irq_disable(USART0_IRQn);
    usart_dma_receive_config(USART0, USART_RECEIVE_DMA_DISABLE);
    dma_channel_disable(DMA1, DMA_CH5);
    usart_disable(USART0);

    /* USART1：DMA0_CH5 */
    usart_interrupt_disable(USART1, USART_INT_IDLE);
    nvic_irq_disable(USART1_IRQn);
    usart_dma_receive_config(USART1, USART_RECEIVE_DMA_DISABLE);
    dma_channel_disable(DMA0, DMA_CH5);
    usart_disable(USART1);

    /* ADC0 + DMA1_CH4 */
    adc_disable(ADC0);
    adc_dma_mode_disable(ADC0);
    dma_channel_disable(DMA1, DMA_CH4);
    nvic_irq_disable(DMA1_Channel4_IRQn);

    /* TIMER1：ADC 的触发源 */
    timer_disable(TIMER1);

    /* DAC0：输出停掉。唤醒后从参数页恢复值 */
    dac_disable(DAC0, DAC_OUT0);

    /* SPI0 / SPI3 是轮询收发，没有 DMA，也不需要单独关通道 */
    spi_disable(SPI0);
    spi_disable(SPI3);

    /* I2C0 */
    i2c_disable(I2C0);
}

/*============================================================================
 * 睡前：GPIO
 *============================================================================ */

static void pmu_gpio_lowpower(void)
{
    uint32_t i;

    /* LED：先拉低熄灭，再切模拟态 */
    for (i = 0U; i < (uint32_t)BSP_LED_COUNT; i++) {
        gpio_bit_reset(BSP_PIN_LED[i].port, BSP_PIN_LED[i].pin);
        pmu_pin_analog(&BSP_PIN_LED[i]);
    }

    /* 6 个普通按键：切模拟态。唤醒键 PA0 单独保留 */
    for (i = 0U; i < (uint32_t)BSP_KEY_COUNT; i++) {
        pmu_pin_analog(&BSP_PIN_KEY[i]);
    }

    /* 两路串口的数据线 */
    pmu_pin_analog(&BSP_PIN_UART0_TX);
    pmu_pin_analog(&BSP_PIN_UART0_RX);
    pmu_pin_analog(&BSP_PIN_UART1_TX);
    pmu_pin_analog(&BSP_PIN_UART1_RX);

    /* RS485 方向脚：保持低电平 = 接收模式 */
    pmu_pin_output(&BSP_PIN_RS485_DE, 0U);

    /* SPI0：数据线模拟态，CS 必须保持高 —— 悬空可能让 Flash 误选中 */
    pmu_pin_analog(&BSP_PIN_SPI0_SCK);
    pmu_pin_analog(&BSP_PIN_SPI0_MISO);
    pmu_pin_analog(&BSP_PIN_SPI0_MOSI);
    pmu_pin_output(&BSP_PIN_SPI0_CS, 1U);

    /* SPI3（AD3344）同理 */
    pmu_pin_analog(&BSP_PIN_SPI3_SCK);
    pmu_pin_analog(&BSP_PIN_SPI3_MISO);
    pmu_pin_analog(&BSP_PIN_SPI3_MOSI);
    pmu_pin_output(&BSP_PIN_SPI3_CS, 1U);

    /* I2C（OLED） */
    pmu_pin_analog(&BSP_PIN_I2C0_SCL);
    pmu_pin_analog(&BSP_PIN_I2C0_SDA);

    /* 本来就是模拟输入的脚，写一遍保持状态一致 */
    pmu_pin_analog(&BSP_PIN_ADC_CH0);
    pmu_pin_analog(&BSP_PIN_ADC_CH1);
    pmu_pin_analog(&BSP_PIN_DAC_OUT);

    /* 唤醒键：上拉输入。切成模拟态就收不到 EXTI 边沿了 */
    rcu_periph_clock_enable(BSP_PIN_WAKEUP_KEY.rcu);
    gpio_mode_set(BSP_PIN_WAKEUP_KEY.port, GPIO_MODE_INPUT,
                  GPIO_PUPD_PULLUP, BSP_PIN_WAKEUP_KEY.pin);
}

/*============================================================================
 * 睡前：逐级降频（官方例程的顺序，不要改）
 *============================================================================ */

static void pmu_clock_downshift(void)
{
    rcu_ahb_clock_config(RCU_AHB_CKSYS_DIV2);
    pmu_soft_delay(0x50U);

    rcu_ahb_clock_config(RCU_AHB_CKSYS_DIV4);
    pmu_soft_delay(0x50U);

    rcu_ahb_clock_config(RCU_AHB_CKSYS_DIV8);
    pmu_soft_delay(0x50U);

    rcu_ahb_clock_config(RCU_AHB_CKSYS_DIV16);
    pmu_soft_delay(0x50U);

    rcu_system_clock_source_config(RCU_CKSYSSRC_IRC16M);
    pmu_soft_delay(200U);

    rcu_ahb_clock_config(RCU_AHB_CKSYS_DIV1);
}

/*============================================================================
 * 唤醒源
 *============================================================================ */

static void pmu_rtc_wakeup_start(uint16_t seconds)
{
    if (seconds == 0U) {
        seconds = 1U;
    }

    /* 先清历史标志，否则刚进睡眠就会被上一次的残留立刻叫醒 */
    exti_interrupt_flag_clear(EXTI_22);
    rtc_flag_clear(RTC_FLAG_WT);

    (void)rtc_wakeup_disable();
    /* WAKEUP_CKSPRE = ck_spre = 1Hz，所以计 N 次就是 N 秒 */
    (void)rtc_wakeup_clock_set(WAKEUP_CKSPRE);
    (void)rtc_wakeup_timer_set((uint16_t)(seconds - 1U));

    rtc_interrupt_enable(RTC_INT_WAKEUP);
    exti_init(EXTI_22, EXTI_INTERRUPT, EXTI_TRIG_RISING);
    nvic_irq_enable(RTC_WKUP_IRQn, 1U, 0U);

    /* 最后才启动计数器 */
    rtc_wakeup_enable();
}

static void pmu_rtc_wakeup_stop(void)
{
    (void)rtc_wakeup_disable();
    rtc_interrupt_disable(RTC_INT_WAKEUP);
    rtc_flag_clear(RTC_FLAG_WT);
    exti_interrupt_flag_clear(EXTI_22);
    nvic_irq_disable(RTC_WKUP_IRQn);
}

static void pmu_key_wakeup_start(void)
{
    rcu_periph_clock_enable(RCU_SYSCFG);

    gpio_mode_set(BSP_PIN_WAKEUP_KEY.port, GPIO_MODE_INPUT,
                  GPIO_PUPD_PULLUP, BSP_PIN_WAKEUP_KEY.pin);

    syscfg_exti_line_config(EXTI_SOURCE_GPIOA, EXTI_SOURCE_PIN0);
    /* 按键按下是下降沿（上拉，按下接地） */
    exti_init(EXTI_0, EXTI_INTERRUPT, EXTI_TRIG_FALLING);
    exti_interrupt_flag_clear(EXTI_0);
    nvic_irq_enable(EXTI0_IRQn, 1U, 0U);
}

static void pmu_key_wakeup_stop(void)
{
    exti_interrupt_disable(EXTI_0);
    exti_interrupt_flag_clear(EXTI_0);
    nvic_irq_disable(EXTI0_IRQn);
}

/*============================================================================
 * 进出
 *============================================================================ */

static void pmu_sleep_prepare(void)
{
    /* 存 VTOR：A 槽 0x08011000 / B 槽 0x08029000，不能写死 */
    s_vtor_save = SCB->VTOR;

    __disable_irq();

    rcu_periph_clock_enable(RCU_PMU);

    pmu_periph_off();
    pmu_gpio_lowpower();

    /* SysTick 停。库函数 pmu_to_deepsleepmode() 内部也会屏蔽它，
     * 但自己停更明确，不依赖库的实现细节 */
    SysTick->CTRL &= ~SysTick_CTRL_TICKINT_Msk;

    pmu_flag_clear(PMU_FLAG_RESET_WAKEUP);
    pmu_flag_clear(PMU_FLAG_RESET_STANDBY);

    /* 降频必须放最后：这之后串口就不能用了 */
    pmu_clock_downshift();
}

static void pmu_wake_restore(void)
{
    __disable_irq();

    /* 恢复 240MHz。SystemInit() 会把 VTOR 打回 0x08000000，
     * 下一行必须立刻改回来 —— 中间不能开中断 */
    SystemInit();
    SCB->VTOR = s_vtor_save;
    SystemCoreClockUpdate();

    pmu_rtc_wakeup_stop();
    pmu_key_wakeup_stop();
    exti_interrupt_flag_clear(EXTI_22);
    exti_interrupt_flag_clear(EXTI_0);

    /* 唤醒中断可能还挂在 NVIC 的 pending 位上。
     * 只清外设标志不够 —— pending 位独立存在，开中断后会立刻再进一次 ISR */
    NVIC_ClearPendingIRQ(RTC_WKUP_IRQn);
    NVIC_ClearPendingIRQ(EXTI0_IRQn);

    __enable_irq();

    /* 重建 1ms 时基。睡眠期间 SysTick 停过，计数从睡前的值继续 */
    bsp_tick_init();

    /* 其余外设由上层恢复（bsp 层不认识 drivers/app） */
    if (s_restore != NULL) {
        s_restore();
    }
}

void bsp_pmu_set_restore(bsp_pmu_restore_fn_t fn)
{
    s_restore = fn;
}

int bsp_pmu_deepsleep_rtc(uint16_t seconds)
{
    /* RTC 没起来 -> 唤醒定时器不会触发 -> 睡下去就醒不来。
     * 宁可不睡，也不能把设备焊死在睡眠里 */
    if (bsp_rtc_is_ready() != 1) {
        return -1;
    }

    pmu_sleep_prepare();
    pmu_rtc_wakeup_start(seconds);

    /* 唤醒靠中断，WFI 之前必须开中断 */
    __enable_irq();

    pmu_to_deepsleepmode(PMU_LDO_LOWPOWER, PMU_LOWDRIVER_ENABLE, WFI_CMD);

    pmu_wake_restore();
    return 0;
}

int bsp_pmu_deepsleep_key(void)
{
    pmu_sleep_prepare();
    pmu_key_wakeup_start();
    __enable_irq();

    pmu_to_deepsleepmode(PMU_LDO_LOWPOWER, PMU_LOWDRIVER_ENABLE, WFI_CMD);

    pmu_wake_restore();
    return 0;
}

/*============================================================================
 * 中断转发
 *============================================================================ */

void bsp_pmu_rtc_isr(void)
{
    if (SET == rtc_flag_get(RTC_FLAG_WT)) {
        rtc_flag_clear(RTC_FLAG_WT);
    }
    exti_interrupt_flag_clear(EXTI_22);
}

void bsp_pmu_key_isr(void)
{
    exti_interrupt_flag_clear(EXTI_0);
}