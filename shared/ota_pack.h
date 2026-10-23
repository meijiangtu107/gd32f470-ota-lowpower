/**
 * @file    ota_pack.h
 * @brief   OTA 升级包的格式定义（上位机与 Bootloader 的约定）
 * @author  meijiangtu
 * @date    2026-10-14
 *
 * 【为什么不用"静默判帧"】
 * 旧工程靠"串口安静超过 N 毫秒就算一帧结束"来断句。这是用**时间**定边界，
 * 三个致命问题：
 *   1. 发送方中途卡一下 → 一帧被切成两帧
 *   2. 两帧挨着来     → 被粘成一帧
 *   3. **丢一个字节 → 因为没有长度字段，后面全部永久错位，无法自愈**
 *
 * 本协议用**格式**定边界：头里有 size 字段，收够那么多字节就结束。
 * 不依赖时间、不怕中途停顿、不怕两包连发。
 *
 * 【包的字节布局】全部小端（和 Cortex-M、和 Python 的 struct 一致）
 *
 *   偏移   长度   字段     含义
 *   ────────────────────────────────────────────────────────────
 *   0x00    4    magic    'O','T','A','1'（小端 0x31544F41）
 *   0x04    4    size     固件字节数（不含头）
 *   0x08    4    crc32    对【固件数据】算的 CRC32
 *   0x0C    4    hcrc     对【前 12 字节】算的 CRC32
 *   ────────────────────────────────────────────────────────────
 *   0x10    N    固件      size 个字节，原样
 *   ────────────────────────────────────────────────────────────
 *            总长 = 16 + size
 *
 * 【三层对齐，逐层收紧】
 *   第 1 层  magic（4 字节）  —— 在字节流里找包头，粗筛
 *   第 2 层  hcrc（12 字节）  —— 排除"碰巧撞上 magic"的假包头
 *   第 3 层  size 上下限      —— 长度不合理也是假的
 *
 * 【为什么需要 hcrc】
 * 光靠 4 字节 magic，随机数据撞上的概率不高但也不是零；更要命的是
 * "BL 从字节流中间切入、后面的字节恰好凑成 OTA1"这种情况。
 * 一旦误信，会读到一个完全错误的 size，然后白白收一整轮垃圾。
 * hcrc 把 12 个字节一起校验，误判概率降到 1/43 亿。
 */

#ifndef SHARED_OTA_PACK_H
#define SHARED_OTA_PACK_H

#include <stdint.h>
#include "partition.h"

/* ---- 头常量 ---- */

/* 'O','T','A','1' 按小端拼起来的 32 位值 */
#define OTA_MAGIC               0x31544F41UL

#define OTA_HEADER_SIZE         16UL

#define OTA_HDR_OFF_MAGIC       0UL
#define OTA_HDR_OFF_SIZE        4UL
#define OTA_HDR_OFF_CRC32       8UL
#define OTA_HDR_OFF_HCRC        12UL

/* hcrc 覆盖范围：从头开头到 hcrc 字段之前，正好 12 字节 */
#define OTA_HDR_HCRC_SPAN       12UL

/* ---- 固件长度上下限 ----
 * 上限取"槽的满容量"—— 这样"装得进槽的固件，一定收得下"，
 * 不需要事先知道固件多大就能确定缓冲要开多大。 */
#define OTA_SIZE_MIN            1024UL
#define OTA_SIZE_MAX            PART_SLOT_MAX_SIZE

/* ---- 校验结果 ---- */
#define OTA_OK                  0
#define OTA_ERR_TIMEOUT        (-1)     /* 等不到数据 */
#define OTA_ERR_BAD_HEADER     (-2)     /* size/hcrc 不合理（会回退重找） */
#define OTA_ERR_BAD_CRC        (-3)     /* 整包 CRC 不过 */
#define OTA_ERR_TOO_BIG        (-4)     /* size 超过缓冲 */

#endif /* SHARED_OTA_PACK_H */
