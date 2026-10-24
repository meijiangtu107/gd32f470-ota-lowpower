/* 跳转 APP 的公共接口，BL 与 APP 共用。
 * app_jump() 只管 CPU 侧（关中断、停 SysTick、清 NVIC、设 VTOR/MSP），
 * 已开的外设由调用方自己关。 */

#ifndef SHARED_APP_JUMP_H
#define SHARED_APP_JUMP_H

#include <stdint.h>

/* 检查 slot_base 处的向量表：MSP 在 SRAM 且 8 字节对齐、
 * Reset_Handler 落在本槽范围内、Thumb 位为 1。返回 1 = 合法，0 = 不合法。 */
int app_vector_valid(uint32_t slot_base);

/* 检查 RAM 中的待安装镜像能否运行在 slot_base 槽：向量表取自 image，
 * Reset_Handler 须落在 Flash 的 slot_base 范围内。返回 1 = 可安装，0 = 不可。 */
int app_image_valid(const void *image, uint32_t slot_base);

/* 跳转到 slot_base 处的固件，成功则不返回；校验失败返回 -1。
 * 调用前须关闭自己开的外设。 */
int app_jump(uint32_t slot_base);

#endif /* SHARED_APP_JUMP_H */
