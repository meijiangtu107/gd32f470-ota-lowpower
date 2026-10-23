/* ADC 采样任务：硬件以 1kHz 采样，本任务 200ms 读一次快照，
 * 同时读一次 AD3344，更新三通道业务量。 */

#include "tasks.h"
#include "app_adc.h"

void task_adc_run(void)
{
    app_adc_update();
}
