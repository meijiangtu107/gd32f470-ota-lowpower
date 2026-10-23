/**
 * @file    bsp_i2c.c
 * @brief   I2C0 总线（OLED 用）
 * @author  meijiangtu
 * @date    2026-09-16
 *
 * I2C 是开漏总线，靠外部上拉拉高。所以：
 *   ① 引脚必须配成 GPIO_OTYPE_OD，推挽会导致总线争用
 *   ② 传输的每一步都要等标志位，不能想当然地连续写
 *   ③ 从机异常会把 SDA 拉死，必须有恢复手段
 */

#include "bsp_i2c.h"
#include "bsp_pinmap.h"
#include "bsp_gpio.h"
#include "bsp_tick.h"
#include "gd32f4xx.h"

#define I2C_PERIPH      I2C0
#define I2C_RCU         RCU_I2C0

/* 每一步等待的超时。400kHz 下一次传输只要几十 µs，5ms 足够 */
#define I2C_TIMEOUT_MS  5U

/*============================================================================
 * 内部：等某个标志置起，带超时
 *============================================================================*/
static int i2c_wait_flag(i2c_flag_enum flag)
{
    uint32_t start = bsp_tick_ms();

    while (RESET == i2c_flag_get(I2C_PERIPH, flag)) {
        if ((bsp_tick_ms() - start) > I2C_TIMEOUT_MS) {
            return -1;
        }
    }
    return 0;
}

/* 发停止条件，并等总线真正空闲 */
static void i2c_stop(void)
{
    uint32_t start = bsp_tick_ms();

    i2c_stop_on_bus(I2C_PERIPH);

    while (0U != (I2C_CTL0(I2C_PERIPH) & I2C_CTL0_STOP)) {
        if ((bsp_tick_ms() - start) > I2C_TIMEOUT_MS) {
            break;
        }
    }
}

/*============================================================================
 * 初始化
 *============================================================================*/
int bsp_i2c0_init(void)
{
    /* ① 引脚：开漏 + 上拉，AF4 */
    bsp_gpio_config(&BSP_PIN_I2C0_SCL);
    bsp_gpio_config(&BSP_PIN_I2C0_SDA);

    /* ② I2C0 自己的时钟 */
    rcu_periph_clock_enable(I2C_RCU);

    /* ③ 参数：400kHz 快速模式，7 位地址 */
    i2c_deinit(I2C_PERIPH);
    i2c_clock_config(I2C_PERIPH, 400000U, I2C_DTCY_2);
    i2c_mode_addr_config(I2C_PERIPH, I2C_I2CMODE_ENABLE, I2C_ADDFORMAT_7BITS,
                         BSP_I2C_OLED_ADDR);
    i2c_enable(I2C_PERIPH);
    i2c_ack_config(I2C_PERIPH, I2C_ACK_ENABLE);

    return 0;
}

/*============================================================================
 * 总线卡死恢复
 *
 * 从机（OLED）在传输中途被复位，会把 SDA 一直拉低。此时 I2C 外设
 * 认为自己还在等，之后的通信全部失败。
 *
 * 解法：把引脚切成普通 GPIO，手动发 9 个 SCL 脉冲 —— 从机每收到一个
 * 时钟就移出一位，第 9 个时钟后它会把 SDA 放掉。最后补一个 STOP，
 * 让总线回到空闲态。
 *============================================================================*/
int bsp_i2c_bus_recover(void)
{
    uint32_t i;

    /* ① I2C 外设先停掉，免得它干扰引脚 */
    i2c_disable(I2C_PERIPH);

    /* ② 引脚切成普通开漏输出（开漏才能被外部上拉拉高） */
    gpio_mode_set(BSP_PIN_I2C0_SCL.port, GPIO_MODE_OUTPUT, GPIO_PUPD_PULLUP,
                  BSP_PIN_I2C0_SCL.pin | BSP_PIN_I2C0_SDA.pin);
    gpio_output_options_set(BSP_PIN_I2C0_SCL.port, GPIO_OTYPE_OD, GPIO_OSPEED_50MHZ,
                            BSP_PIN_I2C0_SCL.pin | BSP_PIN_I2C0_SDA.pin);

    /* ③ 先释放两根线，让上拉把它们拉高 */
    gpio_bit_set(BSP_PIN_I2C0_SCL.port, BSP_PIN_I2C0_SCL.pin | BSP_PIN_I2C0_SDA.pin);
    bsp_delay_us(10U);

    /* ④ 发 9 个 SCL 脉冲。从机在这期间会把 SDA 放掉 */
    for (i = 0U; i < 9U; i++) {
        gpio_bit_reset(BSP_PIN_I2C0_SCL.port, BSP_PIN_I2C0_SCL.pin);
        bsp_delay_us(5U);
        gpio_bit_set(BSP_PIN_I2C0_SCL.port, BSP_PIN_I2C0_SCL.pin);
        bsp_delay_us(5U);
    }

    /* ⑤ 补一个 STOP：SCL 高时 SDA 由低变高 */
    gpio_bit_reset(BSP_PIN_I2C0_SDA.port, BSP_PIN_I2C0_SDA.pin);
    bsp_delay_us(5U);
    gpio_bit_set(BSP_PIN_I2C0_SCL.port, BSP_PIN_I2C0_SCL.pin);
    bsp_delay_us(5U);
    gpio_bit_set(BSP_PIN_I2C0_SDA.port, BSP_PIN_I2C0_SDA.pin);
    bsp_delay_us(5U);

    /* ⑥ 恢复成 I2C 复用，重新初始化外设 */
    bsp_gpio_config(&BSP_PIN_I2C0_SCL);
    bsp_gpio_config(&BSP_PIN_I2C0_SDA);

    i2c_deinit(I2C_PERIPH);
    i2c_clock_config(I2C_PERIPH, 400000U, I2C_DTCY_2);
    i2c_mode_addr_config(I2C_PERIPH, I2C_I2CMODE_ENABLE, I2C_ADDFORMAT_7BITS,
                         BSP_I2C_OLED_ADDR);
    i2c_enable(I2C_PERIPH);
    i2c_ack_config(I2C_PERIPH, I2C_ACK_ENABLE);

    /* ⑦ 确认 SDA 真的被放开了 */
    if (RESET == gpio_input_bit_get(BSP_PIN_I2C0_SDA.port, BSP_PIN_I2C0_SDA.pin)) {
        return -1;      /* 还是被拉低，恢复失败 */
    }

    return 0;
}

/*============================================================================
 * 写一帧
 *============================================================================*/
int bsp_i2c_write(uint8_t dev_addr, uint8_t ctrl_byte,
                  const uint8_t *data, uint16_t len)
{
    uint16_t i;

    if ((data == NULL) || (len == 0U)) {
        return -1;
    }

    /* 总线忙就先等，等不到就恢复一次 */
    if (RESET != i2c_flag_get(I2C_PERIPH, I2C_FLAG_I2CBSY)) {
        uint32_t start = bsp_tick_ms();

        while (RESET != i2c_flag_get(I2C_PERIPH, I2C_FLAG_I2CBSY)) {
            if ((bsp_tick_ms() - start) > I2C_TIMEOUT_MS) {
                if (bsp_i2c_bus_recover() != 0) {
                    return -1;
                }
                break;
            }
        }
    }

    /* 起始条件 */
    i2c_start_on_bus(I2C_PERIPH);
    if (i2c_wait_flag(I2C_FLAG_SBSEND) != 0) {
        i2c_stop();
        return -1;
    }

    /* 从地址 + 写方向。这里地址是 8 位的（最低位是方向位） */
    i2c_master_addressing(I2C_PERIPH, dev_addr, I2C_TRANSMITTER);
    if (i2c_wait_flag(I2C_FLAG_ADDSEND) != 0) {
        i2c_stop();                 /* 没有从机应答，多半是地址错或没接 */
        return -1;
    }
    i2c_flag_clear(I2C_PERIPH, I2C_FLAG_ADDSEND);

    /* 控制字节：0x00 = 命令，0x40 = 数据 */
    if (i2c_wait_flag(I2C_FLAG_TBE) != 0) {
        i2c_stop();
        return -1;
    }
    i2c_data_transmit(I2C_PERIPH, ctrl_byte);

    /* 数据 */
    for (i = 0U; i < len; i++) {
        if (i2c_wait_flag(I2C_FLAG_TBE) != 0) {
            i2c_stop();
            return -1;
        }
        i2c_data_transmit(I2C_PERIPH, data[i]);
    }

    /* 最后一个字节要等 BTC（字节传输完成），否则 STOP 会截断它 */
    if (i2c_wait_flag(I2C_FLAG_BTC) != 0) {
        i2c_stop();
        return -1;
    }

    i2c_stop();
    return 0;
}
