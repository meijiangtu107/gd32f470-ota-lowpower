/* 启动期调试命令窗口（BL_DEBUG_CMDS 置 0 关闭，量产建议关掉）。
 * 参数页受 CRC 保护，绕过本窗口直接改字段会被判为主块损坏并回滚，
 * 构造状态必须走这里的 param_store_save() 落盘路径。 */

#ifndef BL_DEBUG_H
#define BL_DEBUG_H

/* 1 = 开启命令窗口（启动时多等几秒）
 * 0 = 关闭，启动无等待窗口 */
#define BL_DEBUG_CMDS   0

void bl_debug_window(void);

#endif /* BL_DEBUG_H */
