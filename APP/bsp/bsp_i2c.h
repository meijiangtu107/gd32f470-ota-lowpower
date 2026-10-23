/**
 * @file    bsp_i2c.h
 * @brief   I2C0 总线（OLED 用）
 * @author  meijiangtu
 * @date    2026-09-16
 * @note    只做"往某个从设备写一串字节"。I2C 是开漏总线，
 *          引脚必须配成 GPIO_OTYPE_OD，否则会总线争用。
 */

#ifndef BSP_I2C_H
#define BSP_I2C_H

#include <stdint.h>

/* OLED 的 7 位地址。模块丝印常见 0x3C/0x3D，旧工程用 0x72（8 位写地址） */
#define BSP_I2C_OLED_ADDR   0x78U

int  bsp_i2c0_init(void);

/* 写一帧：起始 + 从地址(写) + 控制字节 + 数据 + 停止。
 * 返回 0 成功，负值失败（超时或 NACK） */
int  bsp_i2c_write(uint8_t dev_addr, uint8_t ctrl_byte,
                   const uint8_t *data, uint16_t len);

/* 总线卡死恢复：从机在传输中被复位会把 SDA 一直拉低，
 * 之后的通信全部失败。手动发 9 个 SCL 脉冲 + 一个 STOP 解锁。
 * 返回 0 = 恢复成功（SDA 已释放） */
int  bsp_i2c_bus_recover(void);

#endif /* BSP_I2C_H */
