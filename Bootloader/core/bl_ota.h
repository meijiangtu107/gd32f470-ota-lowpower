/* OTA 接收接口：把整个升级包从串口收进 RAM 并整包校验。
 * 16 字节头的布局见 shared/ota_pack.h。 */

#ifndef BL_OTA_H
#define BL_OTA_H

#include <stdint.h>

/* 等待并接收完整包，阻塞至收齐或超时。
 * 返回 OTA_OK(0) 表示整包 CRC 校验通过，负值见 ota_pack.h 的 OTA_ERR_*。
 * 期间只读串口、写 RAM，不碰 Flash。 */
int bl_ota_receive(void);

/* 收到的固件数据在哪、多大。bl_ota_receive() 返回 OTA_OK 之后才有效 */
const uint8_t *bl_ota_buf(void);
uint32_t       bl_ota_size(void);

/* 把收到的镜像装进非活动槽。
 * 前置条件：bl_ota_receive() 已返回 OTA_OK。
 * 成功则切槽并 NVIC_SystemReset()，不返回；失败返回负值。
 * 始终不碰正在运行的槽。 */
int bl_ota_install(void);

#endif /* BL_OTA_H */
