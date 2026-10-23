/* 任务函数声明：被调度器函数指针表引用，不能声明成 static；
 * 每个任务必须快速返回，不得阻塞。 */

#ifndef TASKS_H
#define TASKS_H

#include <stdint.h>

/* 按键扫描 + 事件处理，10ms 一次 */
void task_key_run(void);

/* LED 状态指示，500ms 一次（LED1 翻转 = 1s 周期） */
void task_led_run(void);

/* 采集工作灯（LED2）：1 = 常亮，0 = 熄灭 */
void task_led_set_work(uint8_t on);

/* 协议接收，10ms 一次 */
void task_uart_run(void);

/* OLED 初始化与显示，200ms 一次 */
void task_oled_init(void);
void task_oled_run(void);

/* 三通道采样更新（ADC 快照 + AD3344），200ms 一次 */
void task_adc_run(void);

#endif /* TASKS_H */
