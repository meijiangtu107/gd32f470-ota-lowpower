/* 跳转进 APP 的最后一跳：向量表校验通过后关中断、停 SysTick、清 NVIC、
 * 设 VTOR，再换栈跳转。
 * 判据：基址 128 字节对齐（VTOR 要求）、MSP 8 字节对齐且在 SRAM、
 * Reset_Handler 在槽内且 Thumb 位为 1。 */

#include "gd32f4xx.h"
#include "app_jump.h"
#include "partition.h"

/* APP 的入口点签名 */
typedef void (*app_entry_t)(void);

/* 最后一步：换栈 + 开中断 + 跳转，全程不访问数据内存。
 * 输入操作数在进入汇编前已取入寄存器；执行 msr msp 后栈上局部变量不再有效。
 * 非 ARM 平台编译为空操作。 */
#if defined(__arm__) || defined(__ARM_ARCH)
#define APP_JUMP_DO_JUMP(new_sp, entry)              \
    do {                                             \
        __asm volatile (                             \
            "msr   msp, %0    \n"                    \
            "cpsie i          \n"                    \
            "bx    %1         \n"                    \
            :                                        \
            : "r" (new_sp), "r" (entry)              \
            : "memory");                             \
    } while (0)
#else
#define APP_JUMP_DO_JUMP(new_sp, entry) \
    do { (void)(new_sp); (void)(entry); } while (0)
#endif

/* 向量表检查公共部分；msp / reset 可能来自 Flash 槽或 RAM 中的待安装镜像。 */
static int vector_ok(uint32_t msp, uint32_t reset, uint32_t slot_base)
{
    /* 初始 MSP 须落在 SRAM 内且 8 字节对齐。
     * MSP 为栈顶（满递减栈），上界取 PART_RAM_END + 1，本芯片正常初值 0x20030000。 */
    if ((msp < PART_RAM_BASE) || (msp > (PART_RAM_END + 1UL))) {
        return 0;
    }
    if ((msp & 0x7UL) != 0UL) {
        return 0;
    }

    /* Reset_Handler 须落在目标槽范围内；A/B 双槽各出一份 bin，装错槽即失败。 */
    if ((reset < slot_base) || (reset >= (slot_base + PART_SLOT_MAX_SIZE))) {
        return 0;
    }

    /* Thumb 位。Cortex-M 只跑 Thumb，函数地址 bit0 恒为 1 */
    if ((reset & 1UL) == 0UL) {
        return 0;
    }

    return 1;
}

int app_vector_valid(uint32_t slot_base)
{
    return vector_ok(*(volatile uint32_t *)(uintptr_t)slot_base,
                     *(volatile uint32_t *)(uintptr_t)(slot_base + 4UL),
                     slot_base);
}

/* 检查 RAM 中的待安装镜像能否运行在目标槽：向量表取自 image，
 * Reset_Handler 须落在 Flash 的 slot_base 范围内。 */
int app_image_valid(const void *image, uint32_t slot_base)
{
    const uint32_t *v = (const uint32_t *)image;

    return vector_ok(v[0], v[1], slot_base);
}

int app_jump(uint32_t slot_base)
{
    app_entry_t entry;
    uint32_t    new_sp;
    uint32_t    i;

    /* VTOR 低 7 位保留，向量表须 128 字节对齐
     * （NVIC_VECTTAB_OFFSET_MASK = 0x1FFFFF80）。 */
    if ((slot_base & 0x7FUL) != 0UL) {
        return -1;
    }

    if (app_vector_valid(slot_base) != 1) {
        return -1;
    }

    /* 换栈前先取初始栈顶（向量表第 0 字）与 Reset_Handler（第 1 字） */
    new_sp = *(volatile uint32_t *)(uintptr_t)slot_base;
    entry  = (app_entry_t)(uintptr_t)(*(volatile uint32_t *)(uintptr_t)(slot_base + 4UL));

    /* 从这里开始不再回头 */

    __disable_irq();

    /* 停止 SysTick，避免跳转后仍按 BL 节拍打断 APP */
    SysTick->CTRL = 0UL;
    SysTick->LOAD = 0UL;
    SysTick->VAL  = 0UL;

    /* 关闭全部 NVIC 通道并清除挂起位：8 组 × 32 位 */
    for (i = 0UL; i < 8UL; i++) {
        NVIC->ICER[i] = 0xFFFFFFFFUL;
        NVIC->ICPR[i] = 0xFFFFFFFFUL;
    }

    /* 向量表切换到 APP */
    SCB->VTOR = slot_base;

    /* 确保 VTOR / NVIC 写入完成后再跳转 */
    __DSB();
    __ISB();

    APP_JUMP_DO_JUMP(new_sp, entry);

    /* 跳转成功则不会返回 */
    return -1;
}
