/* TIMER1：给 ADC 提供固定频率的 TRGO 触发脉冲。
 * 定时器时钟是算出来的：TIMER1 挂 APB1 域，APB1 分频 ≠ 1 时定时器时钟 = APB1 × 2。
 * 不硬编码 120MHz —— 问 rcu 要，时钟树改了这里自动跟上，
 * 算错了也会在 bsp_timer1_get_trig_hz() 里露出来。 */

#include "bsp_timer.h"
#include "gd32f4xx.h"

#define TIMER_PERIPH    TIMER1
#define TIMER_RCU       RCU_TIMER1

/* 把计数频率先降到 1MHz 再算周期。
 * 这样 ARR 的数值正好等于"多少个微秒触发一次"，读起来直观 */
#define TIMER_TICK_HZ   1000000U

static uint32_t s_trig_hz = 0U;

int bsp_timer1_init(void)
{
    timer_parameter_struct initpara;
    uint32_t timclk;
    uint32_t psc;
    uint32_t arr;

    timclk = rcu_clock_freq_get(CK_APB1) * 2U;

    /* 两个商都要减 1，减之前必须先确认它不为 0 —— 商为 0 时减 1 会回绕成
     * 0xFFFFFFFF，配置出一个荒诞的频率，而且不报任何错。
     *
     * 上界（psc）：APB1 至少几 MHz，而 TIMER_TICK_HZ 才 1MHz，实际到不了；
     * 下界（arr）：BSP_TIMER1_TRIG_HZ 若被填成 0，或比 TIMER_TICK_HZ 还大，
     *              这里一步就挡住。注意 0/0 在无符号除法下得 0 而不是异常，
     *              所以同一个判断能覆盖这两种填错法。
     *
     * 挡住了以后改时钟树、改触发频率，都不会突然炸一个诡异值出来 */
    if (((timclk / TIMER_TICK_HZ) == 0U) ||
        ((TIMER_TICK_HZ / BSP_TIMER1_TRIG_HZ) == 0U)) {
        return -1;
    }

    psc = (timclk / TIMER_TICK_HZ) - 1U;
    arr = (TIMER_TICK_HZ / BSP_TIMER1_TRIG_HZ) - 1U;

    /* 先清掉可能残留的配置，再写新参数。
     * 不 deinit 的话，如果之前有人配过这个定时器，
     * 没被覆盖的位会留着作怪 */
    rcu_periph_clock_enable(TIMER_RCU);
    timer_deinit(TIMER_PERIPH);

    timer_struct_para_init(&initpara);
    initpara.prescaler         = (uint16_t)psc;
    initpara.alignedmode       = TIMER_COUNTER_EDGE;
    initpara.counterdirection  = TIMER_COUNTER_UP;
    initpara.period            = arr;
    initpara.clockdivision     = TIMER_CKDIV_DIV1;
    initpara.repetitioncounter = 0U;
    timer_init(TIMER_PERIPH, &initpara);

    /* 把"更新事件"接到 TRGO 上 —— ADC 等的就是这个信号。
     * 选别的源（比如 CC0）ADC 就不会动，这是个静默失败 */
    timer_master_output_trigger_source_select(TIMER_PERIPH, TIMER_TRI_OUT_SRC_UPDATE);

    /* 记下真实频率供调试核对，顺便把整数除法的零头暴露出来 */
    s_trig_hz = timclk / (psc + 1U) / (arr + 1U);

    timer_enable(TIMER_PERIPH);

    return 0;
}

uint32_t bsp_timer1_get_trig_hz(void)
{
    return s_trig_hz;
}
