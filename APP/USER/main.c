/* APP 主程序：初始化外设和业务模块，然后运行调度器 */

#include "gd32f4xx.h"
#include "bsp_tick.h"
#include "bsp_uart.h"
#include "bsp_led.h"
#include "bsp_key.h"
#include "bsp_i2c.h"
#include "app_sched.h"
#include "tasks.h"
#include "bsp_spi.h"
#include "drv_gd25qxx.h"
#include "bsp_adc.h"
#include "bsp_timer.h"
#include "bsp_dac.h"
#include "bsp_pinmap.h"
#include "drv_gd30ad3344.h"
#include "param_store.h"
#include "partition.h"
#include "bsp_rtc.h"
#include "bsp_pmu.h"
#include "app_adc.h"
#include "app_protocol.h"
#include "app_cmd.h"
#include "app_power.h"

/* 当前固件所在槽：A/B 双槽链接地址不同，槽基址必须是编译期常量。
 * 编译 Slot B 时在 Keil 的 Define 中加 APP_SLOT_B；SystemInit() 会把 VTOR 写回 0x08000000，不能据此反推槽位。 */
#if defined(APP_SLOT_B)
#define APP_SLOT_BASE   PART_SLOT_B_BASE
#define APP_SLOT_NAME   "B"
#else
#define APP_SLOT_BASE   PART_SLOT_A_BASE
#define APP_SLOT_NAME   "A"
#endif

int main(void)
{
    uint32_t dac_value;
    int      pr;

    /* 中断向量表指向本槽基址；必须是 main() 的第一条语句，早于任何可能开中断的初始化 */
    SCB->VTOR = APP_SLOT_BASE;

    /* 初始化顺序：tick 最先，其次 USART0 */
    bsp_tick_init();
    bsp_uart0_init(APP_UART0_BAUD);
    bsp_led_init();
    bsp_key_init();

    /* 输出启动信息和 SystemCoreClock；USART0 波特率依赖 rcu_clock_freq_get(CK_APB2) */
    log_printf(USART0, "\r\n=== ximenzi GD32F470 boot ===\r\n");
    log_printf(USART0, "SystemCoreClock = %lu\r\n", (unsigned long)SystemCoreClock);

    /* 参数页初始化：读取、校验、修复，必要时迁移版本；必须在 USART1 初始化前完成。
     * 返回 0=正常，1=主块坏并从备份恢复，2=两份都坏并使用默认值，3=v2 迁移到 v3；新芯片首次运行返回 2。 */
    pr = param_store_init();
    log_printf(USART0, "param_store_init -> %d  id=0x%04lX slot=%lu\r\n",
               pr,
               (unsigned long)param_get_device_id(),
               (unsigned long)param_get_boot_slot());

    /* RS485 业务口；当前协议走 USART0，这里只做初始化 */
    bsp_uart1_init(app_power_rs485_baud());

    /* RTC 初始化：0=保留原时间，1=首次配置，-1=失败；必须在 app_cmd_init() 前完成。
     * 是否首次配置由 BKP0 和时钟源位判断。 */
    {
        int rr = bsp_rtc_init();
        log_printf(USART0, "rtc init -> %d (ready=%d)\r\n",
                   rr, bsp_rtc_is_ready());
    }

    /* SPI0 + Flash：上电读取一次 ID 作为自检 */
    bsp_spi0_init();
    log_printf(USART0, "GD25Qxx ID = 0x%06lX\r\n",
               (unsigned long)drv_gd25q_read_id());

    /* SPI3 + AD3344：16 位外部 ADC，连续转换模式，读取寄存器即最新值 */
    bsp_spi3_init();
    drv_gd30_init();

    /* I2C0 + OLED */
    bsp_i2c0_init();
    task_oled_init();
    log_printf(USART0, "OLED init done\r\n");

    /* ADC：先配置 ADC 和 DMA 并校准，最后启动 TIMER1；定时器启动后会立即触发 ADC */
    if (bsp_adc_init() != 0) {
        log_printf(USART0, "ADC init FAILED, PC0/PC1 will never be ready\r\n");
    }
    if (bsp_timer1_init() == 0) {
        log_printf(USART0, "ADC trig = %lu Hz\r\n",
                   (unsigned long)bsp_timer1_get_trig_hz());
    } else {
        log_printf(USART0, "TIMER1 init FAILED, ADC will never trigger\r\n");
    }

    /* DAC：软件写，上电恢复参数页中的值；PA4 跳线到 PC1，可通过 CH1 回读。不配置触发源和定时器 */
    if (bsp_dac_init() != 0) {
        log_printf(USART0, "DAC init FAILED, PA4 undefined\r\n");
    } else {
        dac_value = param_get_dac_value();
        if (dac_value > 0x0FFFU) {
            dac_value = 0U;
        }
        (void)bsp_dac_set((uint16_t)dac_value);
        log_printf(USART0, "DAC init done, PA4 = %lu\r\n", (unsigned long)dac_value);
    }

    /* 三个通道的业务量层（变比/阈值来自参数页） */
    app_adc_init();

    /* 协议与命令分发 */
    app_protocol_init();
    app_cmd_init();

    /* 注册深睡唤醒后的恢复动作；bsp_pmu 负责时钟、VTOR 和 tick，外设由回调重建 */
    bsp_pmu_set_restore(app_power_restore_after_wake);

    /* 固件自证：初始化完成后向参数页写确认标记；Bootloader 只有看到标记才不回滚。
     * 未写标记时每次启动 trial+1，累计 3 次回滚到旧槽。 */
    param_boot_confirm();
    log_printf(USART0, "boot confirmed on slot %s (base 0x%08lX)\r\n",
               APP_SLOT_NAME, (unsigned long)APP_SLOT_BASE);

    /* 上电发送心跳；放在最后一条日志之后，随后关闭日志 */
    app_protocol_send_heartbeat();

    /* 此后 USART0 只跑协议；打开日志会与协议帧混流，导致上位机解析失败 */
    bsp_log_enable(0U);

    app_sched_init();
    for (;;) {
        app_sched_run();
    }
}
