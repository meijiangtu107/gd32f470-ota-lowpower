/*
 * Flash 分区表（唯一来源，APP 与 Bootloader 共用）。
 *
 * 512KB Flash 切成 6 块。所有边界都按 4KB 页对齐 ——
 * FMC 擦除的最小单位就是页，边界不对齐就会误伤邻区。
 *
 *   0x08000000  BOOT      64K    Bootloader，每次复位第一个执行
 *   0x08010000  PARAM      4K    参数页（主/备参数块 + 告警区 + BL 日志）
 *   0x08011000  Slot A    96K    A 固件，链接基址 0x08011000
 *   0x08029000  Slot B    96K    B 固件，链接基址 0x08029000
 *   0x08041000  Download  96K    升级驿站，永不在此取指运行
 *   0x08059000  FACTORY  156K    预留
 *
 * 三个角色，三条铁律：
 *   Slot A / Slot B  正在运行的槽永远不碰，升级只写另一个
 *   Download         可以随便擦写，但从不在这里取指运行
 *   参数页           任何状态改变先落盘再动作，掉电不丢
 *
 * 本文件不依赖 APP 或 BL 的任何东西，两个工程同时编译。
 */

#ifndef SHARED_PARTITION_H
#define SHARED_PARTITION_H

#include <stdint.h>

/* ---- 整片 Flash ---- */
#define PART_FLASH_BASE       0x08000000UL
#define PART_FLASH_SIZE       0x00080000UL          /* 512KB */
#define PART_FLASH_END        (PART_FLASH_BASE + PART_FLASH_SIZE - 1UL)
#define PART_PAGE_SIZE        0x00001000UL          /* 4KB/页 */

/* ---- SRAM ----
 * 数值取自 GD 官方给 GD32F470xE 的链接脚本
 * （gd32f470xE_flash.ld: RAM 0x20000000 LENGTH 192K）。
 * 跳转前校验 APP 向量表要用它：初始 MSP 必须落在这段里。 */
#define PART_RAM_BASE         0x20000000UL
#define PART_RAM_SIZE         0x00030000UL          /* 192KB */
#define PART_RAM_END          (PART_RAM_BASE + PART_RAM_SIZE - 1UL)

/* ---- BOOT：Bootloader 自己 ---- */
#define PART_BOOT_BASE        0x08000000UL
#define PART_BOOT_SIZE        0x00010000UL          /* 64KB */

/* ---- PARAM：参数页，恰好一页 ---- */
#define PART_PARAM_BASE       0x08010000UL
#define PART_PARAM_SIZE       0x00001000UL          /* 4KB */

/* ---- Slot A / Slot B：两个可运行槽，互为主备 ---- */
#define PART_SLOT_A_BASE      0x08011000UL
#define PART_SLOT_A_SIZE      0x00018000UL          /* 96KB */
#define PART_SLOT_B_BASE      0x08029000UL
#define PART_SLOT_B_SIZE      0x00018000UL          /* 96KB */
#define PART_SLOT_MAX_SIZE    0x00018000UL          /* 两槽等大，取一个即可 */
#define PART_SLOT_COUNT       2U

/* ---- Download：升级暂存，只写不跑 ---- */
#define PART_DOWNLOAD_BASE    0x08041000UL
#define PART_DOWNLOAD_SIZE    0x00018000UL          /* 96KB */

/* ---- FACTORY：校准/工厂预留 ---- */
#define PART_FACTORY_BASE     0x08059000UL
#define PART_FACTORY_SIZE     0x00027000UL          /* 156KB */

/* ---- 槽号 <-> 基址 ----
 * Bootloader 不"认识" A 还是 B，它只认参数页里的 boot_slot。
 * 所以跳转目标一律从这里算出来，绝不许在代码里写死地址。 */
#define PART_SLOT_A           0U
#define PART_SLOT_B           1U

#define PART_SLOT_BASE(slot) \
    (((slot) == PART_SLOT_A) ? PART_SLOT_A_BASE : PART_SLOT_B_BASE)

#define PART_SLOT_SIZE(slot) \
    (((slot) == PART_SLOT_A) ? PART_SLOT_A_SIZE : PART_SLOT_B_SIZE)

/* 另一个槽 —— 升级时"写非活动槽"就靠它 */
#define PART_SLOT_OTHER(slot) \
    (((slot) == PART_SLOT_A) ? PART_SLOT_B : PART_SLOT_A)

/* ---- 编译期自检 ----
 * 下面这些不是运行时检查，是让编译器直接报错。条件不成立时数组长度为负。
 * 破坏任意一条，擦除就会跨页误伤邻区，而这类 bug 上板才炸。
 * 不占 Flash、不占 RAM、运行时不执行 —— 纯粹借编译器的手做检查。 */

/* 大小类 */
typedef char part_assert_param_one_page[(PART_PARAM_SIZE == PART_PAGE_SIZE) ? 1 : -1];
typedef char part_assert_slots_equal[(PART_SLOT_A_SIZE == PART_SLOT_B_SIZE) ? 1 : -1];
typedef char part_assert_slot_aligned[(PART_SLOT_A_SIZE % PART_PAGE_SIZE == 0UL) ? 1 : -1];
typedef char part_assert_download_aligned[(PART_DOWNLOAD_SIZE % PART_PAGE_SIZE == 0UL) ? 1 : -1];

/* 起始地址类 —— 每个区的 BASE 也必须是页的整数倍。
 * 只查 SIZE 对齐是不够的：BASE 不对齐时，擦这个区的第一页会连累前面那个区。 */
typedef char part_assert_boot_aligned[(PART_BOOT_BASE % PART_PAGE_SIZE == 0UL) ? 1 : -1];
typedef char part_assert_param_aligned[(PART_PARAM_BASE % PART_PAGE_SIZE == 0UL) ? 1 : -1];
typedef char part_assert_slot_a_aligned[(PART_SLOT_A_BASE % PART_PAGE_SIZE == 0UL) ? 1 : -1];
typedef char part_assert_slot_b_aligned[(PART_SLOT_B_BASE % PART_PAGE_SIZE == 0UL) ? 1 : -1];
typedef char part_assert_download_aligned_base[(PART_DOWNLOAD_BASE % PART_PAGE_SIZE == 0UL) ? 1 : -1];
typedef char part_assert_factory_aligned[(PART_FACTORY_BASE % PART_PAGE_SIZE == 0UL) ? 1 : -1];

/* 首尾相接类 —— 防"改了 SIZE 忘了同步后面的 BASE"。
 * 这是最容易犯的错，而且错了不会有任何报错，只是两个区悄悄重叠。 */
typedef char part_assert_param_contig[(PART_PARAM_BASE == PART_BOOT_BASE + PART_BOOT_SIZE) ? 1 : -1];
typedef char part_assert_slot_a_contig[(PART_SLOT_A_BASE == PART_PARAM_BASE + PART_PARAM_SIZE) ? 1 : -1];
typedef char part_assert_slot_b_contig[(PART_SLOT_B_BASE == PART_SLOT_A_BASE + PART_SLOT_A_SIZE) ? 1 : -1];
typedef char part_assert_download_contig[(PART_DOWNLOAD_BASE == PART_SLOT_B_BASE + PART_SLOT_B_SIZE) ? 1 : -1];
typedef char part_assert_factory_contig[(PART_FACTORY_BASE == PART_DOWNLOAD_BASE + PART_DOWNLOAD_SIZE) ? 1 : -1];

/* 最后一刀：整片正好用完，不多不少 */
typedef char part_assert_fills_flash[
    ((PART_FACTORY_BASE + PART_FACTORY_SIZE) == (PART_FLASH_BASE + PART_FLASH_SIZE)) ? 1 : -1];

#endif /* SHARED_PARTITION_H */
