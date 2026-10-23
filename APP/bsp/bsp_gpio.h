/* GPIO 统一配置入口 */

#ifndef BSP_GPIO_H
#define BSP_GPIO_H

#include <stdint.h>
#include "bsp_pinmap.h"

/* 按描述符配置一个引脚：开时钟 + 设复用 + 设模式 + 设电气特性；传 NULL 直接返回 */
void bsp_gpio_config(const bsp_pin_t *pin);

/* 配一整张表，比如 6 个 LED、6 个按键 */
void bsp_gpio_config_array(const bsp_pin_t *arr, uint32_t count);

#endif /* BSP_GPIO_H */
