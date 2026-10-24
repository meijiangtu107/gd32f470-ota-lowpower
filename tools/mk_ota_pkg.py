#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
固件 -> OTA 升级包打包工具，接收端为 Bootloader 的串口收包状态机。

用法:
    python tools/mk_ota_pkg.py <输入文件> [-o 输出文件]
    python tools/mk_ota_pkg.py --check <包文件>          # 校验一个已有的包

输入可以是:
    .bin    fromelf / Keil 导出的原始二进制
    .hex    Keil 生成的 Intel HEX（自动转成二进制）
    .axf    Keil 的 ELF（自动找 fromelf 转，找不到就报错提示改用 .hex）

输出默认是 <输入名>.pkg

包格式（全部小端，见 shared/ota_pack.h）:
    偏移   长度  内容
    0x00    4   magic  = b'OTA1'
    0x04    4   size   固件字节数（不含头）
    0x08    4   crc32  固件数据的 CRC32
    0x0C    4   hcrc   前 12 字节的 CRC32
    0x10    N   固件原始字节

CRC 与固件侧 shared/crc.c 一致：CRC-32/ISO-HDLC
（多项式 0xEDB88320 反射，初值 0xFFFFFFFF，出口异或 0xFFFFFFFF）。
Python 标准库的 zlib.crc32 就是它，不必自己实现。
"""

import argparse
import os
import struct
import subprocess
import sys
import zlib

# Windows 控制台默认是 GBK，中文会乱码。切成 UTF-8 再输出。
if sys.platform == "win32":
    try:
        import ctypes
        ctypes.windll.kernel32.SetConsoleOutputCP(65001)
        sys.stdout.reconfigure(encoding="utf-8")
    except Exception:
        pass

MAGIC = b"OTA1"
HEADER_SIZE = 16

# 槽基址与容量，必须和 shared/partition.h 保持一致
SLOT_A_BASE = 0x08011000
SLOT_B_BASE = 0x08029000
SLOT_SIZE = 0x18000            # 96KB
SIZE_MIN = 1024
SIZE_MAX = SLOT_SIZE


def crc32(data: bytes) -> int:
    """和 shared/crc.c 的 crc32_calc() 等价"""
    return zlib.crc32(data) & 0xFFFFFFFF


def self_test():
    """标准测试向量。实现一个 CRC 而不对向量，是常见的低级错误。"""
    got = crc32(b"123456789")
    assert got == 0xCBF43926, "CRC32 self-test FAILED: got 0x%08X" % got


# ---------------------------------------------------------------- 输入解析

def find_fromelf() -> str:
    cands = [
        r"D:\Keil_v5\ARM\ARMCLANG\Bin\fromelf.exe",
        r"C:\Keil_v5\ARM\ARMCLANG\Bin\fromelf.exe",
        r"D:\Keil_v5\ARM\ARMCC\bin\fromelf.exe",
        r"C:\Keil_v5\ARM\ARMCC\bin\fromelf.exe",
    ]
    for c in cands:
        if os.path.isfile(c):
            return c
    return ""


def hex_to_bin(path: str):
    """解析 Intel HEX，返回 (最低地址, 二进制内容)。空隙补 0xFF。"""
    mem = {}
    ext = 0
    with open(path, "r", encoding="ascii", errors="ignore") as f:
        for lineno, line in enumerate(f, 1):
            line = line.strip()
            if not line:
                continue
            if not line.startswith(":"):
                raise ValueError("第 %d 行不是 Intel HEX 记录: %s" % (lineno, line))
            rec = bytes.fromhex(line[1:])
            count, addr, rtype = rec[0], (rec[1] << 8) | rec[2], rec[3]
            data = rec[4:4 + count]
            csum = rec[4 + count]
            if (sum(rec[:4 + count]) + csum) & 0xFF != 0:
                raise ValueError("第 %d 行校验和不对: %s" % (lineno, line))

            if rtype == 0x00:                      # 数据
                base = ext << 16
                for i, b in enumerate(data):
                    mem[base + addr + i] = b
            elif rtype == 0x01:                    # 文件结束
                break
            elif rtype == 0x04:                    # 扩展线性地址
                ext = (data[0] << 8) | data[1]
            elif rtype == 0x02:                    # 扩展段地址
                ext = ((data[0] << 8) | data[1]) << 4
            # 0x03 / 0x05 起点记录，忽略

    if not mem:
        raise ValueError("HEX 里没有数据记录")

    lo, hi = min(mem), max(mem)
    out = bytearray(b"\xFF" * (hi - lo + 1))
    for a, b in mem.items():
        out[a - lo] = b
    return lo, bytes(out)


def load_firmware(path: str):
    """返回 (起始地址 或 None, 固件二进制)"""
    ext = os.path.splitext(path)[1].lower()

    if ext == ".bin":
        with open(path, "rb") as f:
            return None, f.read()

    if ext == ".hex":
        return hex_to_bin(path)

    if ext in (".axf", ".elf", ".out"):
        fe = find_fromelf()
        if not fe:
            raise SystemExit(
                "找不到 fromelf.exe。\n"
                "两条路：\n"
                "  1) 直接给我 Keil 生成的 .hex（工程里 Create HEX File 已勾上）\n"
                "  2) 自己转一次：\n"
                '     "D:\\Keil_v5\\ARM\\ARMCLANG\\Bin\\fromelf.exe" --bin -o fw.bin %s\n'
                % path)
        tmp = path + ".tmp.bin"
        r = subprocess.run([fe, "--bin", "-o", tmp, path],
                           capture_output=True)
        if r.returncode != 0 or not os.path.isfile(tmp):
            raise SystemExit("fromelf 转换失败:\n" + r.stderr.decode(errors="replace"))
        with open(tmp, "rb") as f:
            data = f.read()
        os.remove(tmp)
        return None, data

    raise SystemExit("不认识的扩展名: " + ext)


# ---------------------------------------------------------------- 打包

def build_pkg(fw: bytes) -> bytes:
    size = len(fw)
    if size < SIZE_MIN:
        raise SystemExit("固件只有 %d 字节，太小了（下限 %d）" % (size, SIZE_MIN))
    if size > SIZE_MAX:
        raise SystemExit("固件 %d 字节，超过槽容量 %d" % (size, SIZE_MAX))

    crc = crc32(fw)
    head12 = MAGIC + struct.pack("<II", size, crc)
    hcrc = crc32(head12)
    return head12 + struct.pack("<I", hcrc) + fw


def check_pkg(path: str) -> int:
    with open(path, "rb") as f:
        blob = f.read()

    if len(blob) < HEADER_SIZE:
        print("FAIL: 文件比头还短")
        return 1

    magic = blob[0:4]
    size, crc, hcrc = struct.unpack("<III", blob[4:16])
    fw = blob[16:]

    print("magic : %r  %s" % (magic, "OK" if magic == MAGIC else "*** 不对 ***"))
    print("size  : %d  (文件里实际有 %d 字节)" % (size, len(fw)))
    print("crc32 : 0x%08X" % crc)
    print("hcrc  : 0x%08X  %s" % (hcrc, "OK" if hcrc == crc32(blob[0:12]) else "*** 不对 ***"))
    print("固件 CRC: 0x%08X  %s" % (crc32(fw), "OK" if crc32(fw) == crc else "*** 不对 ***"))

    ok = (magic == MAGIC and size == len(fw) and
          hcrc == crc32(blob[0:12]) and crc32(fw) == crc)
    print("\n结论: " + ("包是好的" if ok else "*** 包有问题 ***"))
    return 0 if ok else 1


# ---------------------------------------------------------------- main

def main():
    ap = argparse.ArgumentParser(add_help=True,
                                 description="生成 BL 能接收的 OTA 升级包")
    ap.add_argument("input", help="固件文件（.bin / .hex / .axf）")
    ap.add_argument("-o", "--output", default=None, help="输出文件名，默认 <输入名>.pkg")
    ap.add_argument("--check", action="store_true",
                    help="不打包，改成校验一个已有的包文件")
    ap.add_argument("--corrupt", choices=["fw", "magic", "size"], default=None,
                    help="故意改坏：fw=固件里一个字节 / magic=包头 / size=谎报长度")
    args = ap.parse_args()

    self_test()

    if args.check:
        return check_pkg(args.input)

    base, fw = load_firmware(args.input)

    out = args.output or (os.path.splitext(args.input)[0] + ".pkg")
    pkg = bytearray(build_pkg(fw))

    # ---- 故意改坏，用来测 BL 的拒绝逻辑 ----
    if args.corrupt == "fw":
        pos = HEADER_SIZE + len(fw) // 2
        pkg[pos] ^= 0xFF
        print("*** 已改坏固件中间的一个字节（偏移 %d）" % pos)
        print("    BL 应当收满之后报 CRC MISMATCH，并且【不动目标槽】 ***")
        print()
    elif args.corrupt == "magic":
        pkg[0] ^= 0xFF
        print("*** 已改坏头里的 magic（第一个字节）")
        print("    BL 应当找不到包头，30 秒后报 no package ***")
        print()
    elif args.corrupt == "size":
        size = struct.unpack("<I", pkg[4:8])[0]
        struct.pack_into("<I", pkg, 4, size + 4096)   # 谎报长度，但 hcrc 不改
        print("*** 已把 size 谎报成 %d（实际 %d），hcrc 保持原值" % (size + 4096, size))
        print("    BL 应当在 hcrc 这一层就看穿是假包头，回退重新找 ***")
        print()

    pkg = bytes(pkg)
    with open(out, "wb") as f:
        f.write(pkg)

    size = len(fw)
    crc = crc32(fw)

    print("输入    : %s" % args.input)
    if base is not None:
        slot = {SLOT_A_BASE: "A", SLOT_B_BASE: "B"}.get(base)
        print("起始地址: 0x%08X%s" % (base, ("  (= Slot %s)" % slot) if slot else ""))
        if base not in (SLOT_A_BASE, SLOT_B_BASE):
            print("          *** 警告：不是两个槽的基址之一，烧进去可能起不来 ***")
    print("固件大小: %d 字节 (%.1f KB)" % (size, size / 1024.0))
    print("固件 CRC: 0x%08X" % crc)
    print()
    print("包总长  : %d 字节" % len(pkg))
    print("头 16 字节: %s" % pkg[:16].hex(" ").upper())
    print("输出    : %s" % out)
    print()
    print("MICU-OmniProbe 里这样配：")
    print("    串口工作台 → 发送工具 → 文件")
    print("      文件发送协议 : 原始字节（默认）")
    print("      分块字节数   : 1024")
    print("      分块间隔(ms) : 5")
    print("      选文件       : %s" % os.path.basename(out))
    return 0


if __name__ == "__main__":
    sys.exit(main())
