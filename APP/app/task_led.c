/* LED 状态指示：LED1 每 500ms 翻转（亮灭各 500ms，周期 1s），
 * LED2 自动上报期间常亮，其余熄灭。 */

#include "tasks.h"
#include "bsp_led.h"

void task_led_run(void)
{
    bsp_led_toggle(BSP_LED_1);
}

void task_led_set_work(uint8_t on)
{
    bsp_led_write(BSP_LED_2, (on != 0U) ? true : false);
}
