/* 串口：USART0 调试口（PA9/PA10，DMA1_CH5）+ USART1 RS485（PD5/PD6，DMA0_CH5，DE=PE8）。
 * 接收：DMA 循环搬运到缓冲区，线路空闲（IDLE）中断里把这一批抄进 ringbuf，
 *       主循环从 ringbuf 慢慢取，两边解耦。
 * 发送：轮询等 TBE。USART1 发送前后要控制 RS485 的 DE 引脚方向。 */

#include "bsp_uart.h"
#include <stdarg.h>
#include <stdio.h>
#include "bsp_pinmap.h"
#include "bsp_gpio.h"
#include "ringbuf.h"
#include "gd32f4xx.h"
#include "bsp_tick.h"
/* 日志缓冲。放栈上，别超过 128 字节 —— 中断嵌套时会吃掉中断栈 */
#define LOG_BUF_SIZE    128

/*============================================================================
 * USART0 调试口：PA9/PA10，DMA1_CH5
 *============================================================================*/
#define U0_USART        USART0
#define U0_USART_RCU    RCU_USART0
#define U0_DMA          DMA1
#define U0_DMA_CH       DMA_CH5

/* ringbuf 必须比 DMA 缓冲大 1 个字节：环形缓冲要留一个空位区分"满"和"空"，
 * 所以 N 个格子的数组只能存 N-1 个。DMA 一次最多搬 BSP_UART_RX_SIZE 个，
 * 存储开 +1 才不会丢最后一个字节 */
#define BSP_UART_RB_SIZE    (BSP_UART_RX_SIZE + 1U)

static uint8_t  s_rx0[BSP_UART_RX_SIZE];            /* DMA 直接搬进这里 */
static uint8_t  s_rb0_store[BSP_UART_RB_SIZE];      /* ringbuf 自己的存储 */
static ringbuf_t s_rb0;

/*============================================================================
 * USART1 RS485：PD5/PD6，DMA0_CH5，DE = PE8
 *============================================================================*/
#define U1_USART        USART1
#define U1_USART_RCU    RCU_USART1
#define U1_DMA          DMA0
#define U1_DMA_CH       DMA_CH5

static uint8_t  s_rx1[BSP_UART_RX_SIZE];            /* DMA 直接搬进这里 */
static uint8_t  s_rb1_store[BSP_UART_RB_SIZE];      /* ringbuf 自己的存储 */
static ringbuf_t s_rb1;

/*============================================================================
 * 公共：配置 USART 参数（两路只有外设号不同）
 *============================================================================*/
static void uart_param_config(uint32_t usart, uint32_t baud)
{
    usart_deinit(usart);
    usart_baudrate_set(usart, baud);
    usart_word_length_set(usart, USART_WL_8BIT);
    usart_stop_bit_set(usart, USART_STB_1BIT);
    usart_parity_config(usart, USART_PM_NONE);
    usart_receive_config(usart, USART_RECEIVE_ENABLE);
    usart_transmit_config(usart, USART_TRANSMIT_ENABLE);
    usart_dma_receive_config(usart, USART_RECEIVE_DMA_ENABLE);
    usart_enable(usart);
}

/*============================================================================
 * 公共：配置 DMA 接收（外设 → 内存，循环模式）
 *============================================================================*/
static void uart_dma_rx_config(uint32_t dma, dma_channel_enum ch,
                               uint32_t usart, uint8_t *buf)
{
    /* DMA 通道配置：外设 → 内存，循环模式，8bit */
    dma_single_data_parameter_struct dma_cfg;

    /* 先填默认值再逐项覆盖——init 会读取全部成员，栈上野值会被写进寄存器 */
    dma_single_data_para_struct_init(&dma_cfg);

    /* 复位通道，不在残留配置上叠加 */
    dma_deinit(dma, ch);

    dma_cfg.direction           = DMA_PERIPH_TO_MEMORY;
    dma_cfg.memory0_addr        = (uint32_t)buf;
    dma_cfg.memory_inc          = DMA_MEMORY_INCREASE_ENABLE;

    /* 外设侧始终是同一个数据寄存器，地址不递增 */
    dma_cfg.periph_addr         = (uint32_t)&USART_DATA(usart);
    dma_cfg.periph_inc          = DMA_PERIPH_INCREASE_DISABLE;
    dma_cfg.periph_memory_width = DMA_PERIPH_WIDTH_8BIT;
    dma_cfg.number              = BSP_UART_RX_SIZE;
    dma_cfg.priority            = DMA_PRIORITY_HIGH;
    dma_cfg.circular_mode       = DMA_CIRCULAR_MODE_ENABLE;

    dma_single_data_mode_init(dma, ch, &dma_cfg);

    /* 子外设 4 = 这条通道上的 USART RX 请求路由 */
    dma_channel_subperipheral_select(dma, ch, DMA_SUBPERI4);

    dma_channel_enable(dma, ch);
}

/*============================================================================
 * 公共：IDLE 中断处理 —— 把 DMA 已经搬到的数据抄进 ringbuf
 *============================================================================*/
static void uart_idle_handler(uint32_t usart, uint32_t dma, dma_channel_enum ch,
                              uint8_t *buf, ringbuf_t *rb)
{
    uint16_t remain;
    uint16_t got;

    /* 读一次 DATA 寄存器才能清掉 IDLE 标志 —— 硬件设计如此，不读会一直触发 */
    usart_data_receive(usart);

    /* DMA 计数器从 BSP_UART_RX_SIZE 倒数到 0，所以搬了的字节数 = 总数 - 剩余 */
    remain = (uint16_t)dma_transfer_number_get(dma, ch);
    got = (uint16_t)(BSP_UART_RX_SIZE - remain);

    if (got > 0U) {
        (void)ringbuf_write(rb, buf, got);

        /* 把读指针推到已处理的位置，并重装 DMA 计数从头收。
         * 循环模式下搬满会自动绕回，但这里要的是"每次 IDLE 都从头数"，
         * 所以手动重装，逻辑最直观 */
        dma_channel_disable(dma, ch);
        dma_flag_clear(dma, ch, DMA_FLAG_FTF);
        dma_transfer_number_config(dma, ch, BSP_UART_RX_SIZE);
        dma_memory_address_config(dma, ch, DMA_MEMORY_0, (uint32_t)buf);
        dma_channel_enable(dma, ch);
    }
}

/*============================================================================
 * USART0 调试口
 *============================================================================*/

int bsp_uart0_init(uint32_t baud)
{
    /* 引脚配置走 pinmap，AF7 和上下拉都在表里 */
    bsp_gpio_config(&BSP_PIN_UART0_TX);
    bsp_gpio_config(&BSP_PIN_UART0_RX);

    /* 外设自己的时钟。pinmap 表里只有 GPIO 时钟，这两行不能省。
     * 漏掉的表现：程序不崩，串口一个字都不出 */
    rcu_periph_clock_enable(U0_USART_RCU);
    rcu_periph_clock_enable(RCU_DMA1);

    /* 中断优先级分组：4 位抢占优先级，0 位子优先级 */
    nvic_priority_group_set(NVIC_PRIGROUP_PRE4_SUB0);

    uart_param_config(U0_USART, baud);
    uart_dma_rx_config(U0_DMA, U0_DMA_CH, U0_USART, s_rx0);

    ringbuf_init(&s_rb0, s_rb0_store, BSP_UART_RB_SIZE);

    /* 开空闲中断。数据由 DMA 搬，CPU 只在"一帧收完"时被打断一次 */
    usart_interrupt_enable(U0_USART, USART_INT_IDLE);
    nvic_irq_enable(USART0_IRQn, 2U, 0U);

    return 0;
}

void bsp_uart0_irq_handler(void)
{
    if (usart_interrupt_flag_get(U0_USART, USART_INT_FLAG_IDLE) != RESET) {
        uart_idle_handler(U0_USART, U0_DMA, U0_DMA_CH, s_rx0, &s_rb0);
    }
}

/*============================================================================
 * USART1 RS485
 *============================================================================*/

int bsp_uart1_init(uint32_t baud)
{
    bsp_gpio_config(&BSP_PIN_UART1_TX);
    bsp_gpio_config(&BSP_PIN_UART1_RX);
    bsp_gpio_config(&BSP_PIN_RS485_DE);

    /* 初始进接收模式。上电默认是发的话，总线会被一直占着 */
    gpio_bit_reset(BSP_PIN_RS485_DE.port, BSP_PIN_RS485_DE.pin);

    rcu_periph_clock_enable(U1_USART_RCU);
    rcu_periph_clock_enable(RCU_DMA0);

    nvic_priority_group_set(NVIC_PRIGROUP_PRE4_SUB0);

    uart_param_config(U1_USART, baud);
    uart_dma_rx_config(U1_DMA, U1_DMA_CH, U1_USART, s_rx1);

    ringbuf_init(&s_rb1, s_rb1_store, BSP_UART_RB_SIZE);

    usart_interrupt_enable(U1_USART, USART_INT_IDLE);
    nvic_irq_enable(USART1_IRQn, 3U, 0U);

    return 0;
}

void bsp_uart1_irq_handler(void)
{
    if (usart_interrupt_flag_get(U1_USART, USART_INT_FLAG_IDLE) != RESET) {
        uart_idle_handler(U1_USART, U1_DMA, U1_DMA_CH, s_rx1, &s_rb1);
    }
}

/*============================================================================
 * 收发
 *============================================================================*/

int bsp_uart_write(uint32_t usart, const uint8_t *data, uint16_t len)
{
    uint16_t i;
    int is_rs485 = (usart == U1_USART) ? 1 : 0;

    if ((data == NULL) || (len == 0U)) {
        return -1;
    }

    if (is_rs485 != 0) {
        /* 切到发送方向，等收发器真正开始驱动总线 */
        gpio_bit_set(BSP_PIN_RS485_DE.port, BSP_PIN_RS485_DE.pin);
        bsp_delay_us(RS485_DE_SETUP_US);
    }

    for (i = 0U; i < len; i++) {
        /* 先等数据寄存器空，再写。顺序不能反 */
        while (RESET == usart_flag_get(usart, USART_FLAG_TBE)) {
        }
        usart_data_transmit(usart, data[i]);
    }

    /* 必须等 TC 而不是 TBE：
     * TBE 只说明数据寄存器空了，移位寄存器可能还在吐最后一个字节。
     * 这时候拉低 DE，最后一个字节就被截断 */
    while (RESET == usart_flag_get(usart, USART_FLAG_TC)) {
    }

    if (is_rs485 != 0) {
        /* 保持一个字符时间，再切回接收 */
        bsp_delay_us(RS485_DE_HOLD_US);
        gpio_bit_reset(BSP_PIN_RS485_DE.port, BSP_PIN_RS485_DE.pin);
    }

    return (int)len;
}

uint32_t bsp_uart_read(uint32_t usart, uint8_t *data, uint16_t len)
{
    if (usart == U0_USART) {
        return ringbuf_read(&s_rb0, data, len);
    }
    if (usart == U1_USART) {
        return ringbuf_read(&s_rb1, data, len);
    }
    return 0U;
}

/*============================================================================
 * 日志开关
 *============================================================================*/

/* 默认开：上电初始化阶段的日志要能看到。协议开始收发前由 main() 关掉 */
static volatile uint8_t s_log_enabled = 1U;

void bsp_log_enable(uint8_t on)
{
    s_log_enabled = on;
}

int log_printf(uint32_t usart, const char *fmt, ...)
{
    char buf[LOG_BUF_SIZE];
    va_list ap;
    int len;

    if (s_log_enabled == 0U) {
        return 0;
    }

    va_start(ap, fmt);
    len = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    if (len <= 0) {
        return len;
    }

    /* vsnprintf 返回的是"假如缓冲够大会写多少"，可能大于 sizeof(buf)。
     * 不夹紧就会越界读栈，往串口吐垃圾 */
    if (len > (int)sizeof(buf) - 1) {
        len = (int)sizeof(buf) - 1;
    }

    (void)bsp_uart_write(usart, (const uint8_t *)buf, (uint16_t)len);
    return len;
}
