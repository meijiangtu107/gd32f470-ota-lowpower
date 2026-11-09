/* RTOS 迁移：中断优先级分组的集中设置点 */
#include "bsp_irq_prio.h"
#include "gd32f4xx.h"

void bsp_irq_prio_init(void)
{
    nvic_priority_group_set(NVIC_PRIGROUP_PRE4_SUB0);
}
