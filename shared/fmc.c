/* 片上 Flash 擦写。擦除粒度是 4KB 整页；写只能 1→0，改数据必须先擦页。
 * 擦写函数放 .ramfunc 段在 RAM 里执行，每次操作后要清标志。 */

#include "gd32f4xx.h"
#include "fmc.h"
#include "partition.h"
#include <string.h>

#if defined(__ARMCC_VERSION) || defined(__GNUC__)
#define FMC_RAMFUNC  __attribute__((section(".ramfunc"), noinline))
#else
#define FMC_RAMFUNC
#endif

/* 可写范围下限为 PART_PARAM_BASE，BOOT 区（0x08000000 - 0x0800FFFF）不可擦写 */
#define FMC_MIN_ADDR   PART_PARAM_BASE
#define FMC_MAX_ADDR   PART_FLASH_END

static FMC_RAMFUNC void fmc_clear_flags(void)
{
    fmc_flag_clear(FMC_FLAG_END);
    fmc_flag_clear(FMC_FLAG_WPERR);
    fmc_flag_clear(FMC_FLAG_PGSERR);
    fmc_flag_clear(FMC_FLAG_PGMERR);
}

/* 检查 [addr, addr+size) 在可写范围内；用减法比较避免 32 位溢出 */
static int fmc_range_ok(uint32_t addr, uint32_t size)
{
    if (size == 0UL) {
        return 0;
    }
    if (addr < FMC_MIN_ADDR) {
        return 0;
    }
    if (addr > FMC_MAX_ADDR) {
        return 0;
    }
    if (size > (FMC_MAX_ADDR - addr + 1UL)) {
        return 0;
    }
    return 1;
}

FMC_RAMFUNC int fmc_erase_pages(uint32_t addr, uint32_t size)
{
    uint32_t first;
    uint32_t last;
    uint32_t cur;

    if (size == 0UL) {
        return FMC_ERR;
    }

    /* 向外扩展到整页边界；先算 last 再校验，避免 addr+size 溢出 */
    first = addr - (addr % PART_PAGE_SIZE);
    last  = addr + size - 1UL;
    last  = last - (last % PART_PAGE_SIZE);

    if (first < FMC_MIN_ADDR) {
        return FMC_ERR;
    }
    if (last > FMC_MAX_ADDR) {
        return FMC_ERR;
    }

    fmc_unlock();
    fmc_clear_flags();

    for (cur = first; cur <= last; cur += PART_PAGE_SIZE) {
        if (fmc_page_erase(cur) != FMC_READY) {
            fmc_lock();
            fmc_clear_flags();
            return FMC_ERR;
        }
        fmc_clear_flags();
    }

    fmc_lock();
    return FMC_OK;
}

FMC_RAMFUNC int fmc_program(uint32_t addr, const uint8_t *data, uint32_t size)
{
    uint32_t word;
    uint32_t i;
    int      ret = FMC_OK;

    if ((data == 0) || (size == 0UL)) {
        return FMC_ERR;
    }
    if (!fmc_range_ok(addr, size)) {
        return FMC_ERR;
    }

    fmc_unlock();
    fmc_clear_flags();

    /* 1. 头部：地址对齐到 4 字节之前的字节逐个写 */
    while (((addr & 3UL) != 0UL) && (size > 0UL)) {
        if (fmc_byte_program(addr, *data) != FMC_READY) {
            ret = FMC_ERR;
            goto done;
        }
        addr++;
        data++;
        size--;
    }

    /* 2. 中间：按字写。用 memcpy 拼字，避免 data 非对齐访问触发 UNALIGNED fault
     *    （CFSR bit 24）。 */
    while (size >= 4UL) {
        memcpy(&word, data, 4U);
        if (fmc_word_program(addr, word) != FMC_READY) {
            ret = FMC_ERR;
            goto done;
        }
        addr += 4UL;
        data += 4;
        size -= 4UL;
    }

    /* 3. 尾部：不足 4 字节的部分逐字节写 */
    for (i = 0UL; i < size; i++) {
        if (fmc_byte_program(addr + i, data[i]) != FMC_READY) {
            ret = FMC_ERR;
            goto done;
        }
    }

done:
    fmc_clear_flags();
    fmc_lock();
    return ret;
}

int fmc_is_erased(uint32_t addr, uint32_t size)
{
    const uint8_t *p = (const uint8_t *)(uintptr_t)addr;
    uint32_t i;

    if (!fmc_range_ok(addr, size)) {
        return -1;
    }

    for (i = 0UL; i < size; i++) {
        if (p[i] != 0xFFU) {
            return 0;
        }
    }
    return 1;
}

int fmc_read(uint32_t addr, uint8_t *dst, uint32_t size)
{
    if ((dst == 0) || (size == 0UL)) {
        return FMC_ERR;
    }
    if (!fmc_range_ok(addr, size)) {
        return FMC_ERR;
    }

    memcpy(dst, (const void *)(uintptr_t)addr, size);
    return FMC_OK;
}
