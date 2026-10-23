/**
 * @file    fmc.h
 * @brief   片上 Flash 擦写（FMC = Flash Memory Controller）
 * @author  meijiangtu
 * @date    2026-10-10
 *
 * 本层只管"物理上把字节写进 Flash"，不认识参数结构体、不认识分区语义。
 * 参数怎么组织、写哪一块，是 param_store 的事。
 *
 * 两个必须记住的硬件事实：
 *   1. 擦除的最小单位是"页"（4KB），没有"擦 256 字节"这回事
 *   2. 写入只能把 1 变 0，不能把 0 变 1 —— 想改回 1 必须先擦整页
 * 第 2 条直接决定了参数保存必须"整页读-改-写"。
 */

#ifndef SHARED_FMC_H
#define SHARED_FMC_H

#include <stdint.h>

/* 返回值：0 = 成功，-1 = 失败 */
#define FMC_OK      0
#define FMC_ERR    (-1)

/* 擦除覆盖 [addr, addr+size) 的所有页。
 * addr / size 不必页对齐 —— 内部会自动向外扩到整页边界，
 * 也就是说"擦 256 字节"实际会擦掉包含它的那 4KB。
 * 调用者必须自己保证被扩进来的邻区是可以牺牲的。 */
int fmc_erase_pages(uint32_t addr, uint32_t size);

/* 编程 [addr, addr+size)。支持任意对齐、任意长度：
 * 头部不对齐的部分逐字节写，中间按字写，尾巴逐字节写。 */
int fmc_program(uint32_t addr, const uint8_t *data, uint32_t size);

/* 判断 [addr, addr+size) 是否全为 0xFF（擦干净的状态）。
 * 返回 1 = 全 FF，0 = 有数据，-1 = 参数非法 */
int fmc_is_erased(uint32_t addr, uint32_t size);

/* 从 Flash 读一段到 RAM。Flash 是内存映射的，本质就是 memcpy。 */
int fmc_read(uint32_t addr, uint8_t *dst, uint32_t size);

#endif /* SHARED_FMC_H */
