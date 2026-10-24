/* 启动核心接口：一次上电的槽位决策、升级与交接。 */

#ifndef BL_CORE_H
#define BL_CORE_H

#include <stdint.h>

/* 启动主流程，不返回：要么跳进某个槽，要么停在错误循环。 */
void bl_core_run(void);

#endif /* BL_CORE_H */
