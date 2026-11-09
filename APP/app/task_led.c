/* LED 状态指示：LED1 每 500ms 翻转（亮灭各 500ms，周期 1s），
 * LED2 自动上报期间常亮，其余熄灭。 */

#include "tasks.h"
#include "bsp_led.h"

void task_led_run(void)
{
    bsp_led_toggle(BSP_LED_1);
}

/* 工作灯：唯一写者是 t_cmd（上报开关、睡眠命令），t_led 不读该状态。
 * 单写单读 + 对齐的单字节写入在 Cortex-M4 上是原子的，所以不加锁。
 * 将来若增加第二个写者，必须先重新设计同步方式。 */
void task_led_set_work(uint8_t on)
{
    bsp_led_write(BSP_LED_2, (on != 0U) ? true : false);
}
