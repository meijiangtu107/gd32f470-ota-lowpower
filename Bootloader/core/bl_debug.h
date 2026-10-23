/* Bootloader 调试命令窗口。
 * 参数页受 CRC 保护，直接改字段会被判为主块损坏并回滚，造状态须走本窗口的
 * param_store_save() 路径。BL_DEBUG_CMDS 置 0 时窗口关闭。 */

#ifndef BL_DEBUG_H
#define BL_DEBUG_H

/* 1 = 开启命令窗口（启动时多等几秒）
 * 0 = 关闭，启动无等待窗口 */
#define BL_DEBUG_CMDS   0

void bl_debug_window(void);

#endif /* BL_DEBUG_H */
