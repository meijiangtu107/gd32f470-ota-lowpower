# ximenzi — GD32F470 数采仪表固件

基于 **GD32F470VE**（Cortex-M4 @ 240MHz / 512KB Flash / 192KB RAM）的多通道数据采集仪表固件，包含 APP 应用与 A/B 双槽 OTA Bootloader 两个工程。串口协议与现有上位机**逐字节兼容**，可直接对接。

当前固件版本：**2.0.1.0**

---

## 功能总览

- **三通道数据采集**
  - CH0 / CH1：内部 ADC0（PC0/PC1），TIMER1 定时触发 + DMA 搬运，采样值 × 变比上报
  - CH2：外部 16 位 ADC **GD30AD3344**（SPI3），PT100 温度（℃）直读
- **串口协议**（USART0，ASCII-Hex 帧，CRC16-Modbus 校验）
  - 设备管理：重启 / 版本查询 / RTC 时间读写 / 设备 ID / 波特率设置
  - 数据查询：三通道实时值、变比设置、上报间隔（1/3/5s）
  - 自动上报：带 Unix 时间戳的周期数据帧
  - DAC 输出控制（PA4，掉电保持）
  - 阈值读写（落 Flash）、告警记录（RAM 最多 10 条）、主动告警上报
- **RTC 实时时钟**：32.768kHz 晶振 + VBAT 备份域，复位/断电不丢时间
- **低功耗 Deep-sleep**：一键进入（协议命令 `0x03AA`），PA0 按键唤醒；RTC 定时唤醒代码就绪
- **A/B 双槽 OTA 升级**：整包收进 RAM 校验后装入非活动槽，新固件 3 次启动未自证**自动回滚**，升级全程失败不损伤现有固件
- **参数持久化**：4KB 参数页主备双副本 + CRC 保护 + 版本迁移，掉电不丢
- **OLED 显示**（SSD1306 / I2C0）：驱动与显示任务已就绪，硬件待接

---

## 硬件资源与引脚

> 引脚定义唯一来源：`APP/bsp/bsp_pinmap.h`，表实体在 `bsp_gpio.c`。

| 外设 | 引脚 | 用途 |
|---|---|---|
| USART0 | PA9 / PA10 | 协议 + 调试口，**115200 8N1**（恒定） |
| USART1 | PD5 / PD6（+RS485 DE 脚） | RS485 业务口（已初始化，波特率可由协议设置） |
| SPI0 | PB3 / PB4 / PB5（+软件 CS） | GD25Qxx SPI Flash |
| SPI3 | PE12 / PE13 / PE14，CS = PE10 | GD30AD3344 外部 16 位 ADC（连续转换） |
| I2C0 | PB8 / PB9（开漏） | SSD1306 OLED |
| ADC0 | PC0 (CH10) / PC1 (CH11) | 两路模拟输入 |
| DAC0 | PA4 (DAC0_OUT0) | DAC 输出，跳线到 PC1 可回读验证 |
| LED | PD10 ~ PD15 | 高电平点亮（LED1 作心跳指示，1s 周期） |
| 按键 | PE15 / PE6 / PE11 / PE4 / PE7 / PB0 | 上拉输入，按下为低，状态机消抖 |
| 唤醒键 | PA0（EXTI0） | Deep-sleep 唤醒 |
| RTC | 32.768kHz 晶振 + VBAT | 备份域时钟源 |

---

## Flash 分区（A/B 双槽）

> 唯一来源：`shared/partition.h`（APP 与 Bootloader 共用，含编译期对齐自检）。

```
0x08000000  BOOT      64K   Bootloader，每次复位第一个执行
0x08010000  PARAM      4K   参数页（主/备参数块 + 告警记录区 + BL 日志区）
0x08011000  Slot A    96K   固件槽 A（链接基址 0x08011000）
0x08029000  Slot B    96K   固件槽 B（链接基址 0x08029000）
0x08041000  Download  96K   升级驿站，只写不跑
0x08059000  FACTORY  156K   预留
```

三条铁律：

1. **正在运行的槽永不擦写**，升级只写另一个槽；
2. **Download 区可以随便擦写，但从不在这里取指运行**；
3. **参数页任何状态改变先落盘再动作**，掉电不丢。

---

## 软件架构

```
┌────────────────────────── APP 应用 ──────────────────────────┐
│  app/     业务层                                             │
│    app_sched    时间片轮询调度器（非阻塞，tick 驱动）          │
│    task_*       按键10ms / 协议10ms / OLED200ms / 采样200ms / LED500ms │
│    app_protocol 协议帧收发（心跳/应答/自动上报）               │
│    app_cmd      命令分发（系统/数据/控制/参数/告警/睡眠）       │
│    app_adc      三通道业务量（变比/阈值/告警判断）              │
│    app_power    深睡唤醒后的外设重建                           │
├──────────────────────────────────────────────────────────────┤
│  middleware/  通用中间件                                      │
│    frame_parser 流式状态机拆帧（粘包/断帧/前导垃圾均可恢复）     │
│    ringbuf      环形缓冲   crc16  CRC16-Modbus                │
├──────────────────────────────────────────────────────────────┤
│  drivers/     器件驱动：GD25Qxx Flash / GD30AD3344 / SSD1306  │
│  bsp/         外设初始化层：uart/spi/i2c/adc/timer/dac/       │
│               rtc/pmu/led/key/gpio/tick（引脚表统一配置）       │
├──────────────────────────────────────────────────────────────┤
│  shared/      APP 与 Bootloader 共用                          │
│    partition.h   Flash 分区表（唯一来源，编译期自检）           │
│    param_store   参数页存储（主备双副本 + 整页读改写）          │
│    fmc / crc32 / app_jump / ota_pack.h                        │
├──────────────────────────────────────────────────────────────┤
│  Bootloader/  独立工程                                        │
│    bl_core     启动状态机：读参数→升级标志→回滚判定→验表跳槽     │
│    bl_ota      OTA 整包接收（三层对齐）→ 校验 → 装非活动槽      │
│    bl_debug    5 秒调试命令窗口（构造各种启动状态用于测试）      │
├──────────────────────────────────────────────────────────────┤
│  Library/ CMSIS/    GD32F4xx 标准外设库 + CMSIS 内核层         │
│  tools/             mk_ota_pkg.py 升级包打包/校验脚本           │
└──────────────────────────────────────────────────────────────┘
```

### 启动流程（Bootloader）

```
复位 → BL 读参数页（主备双副本择优，坏哪份修哪份）
     → 5 秒调试命令窗口（收到升级请求则跳过）
     → FAST_UPGRADE？→ 整包收进 RAM → CRC32 校验 → 装入非活动槽 → 切槽复位
     → 回滚判定：boot_confirm 未签字则 trials+1，累计 3 次 → 回滚另一槽
     → 校验目标槽向量表（MSP 落在 RAM 区 + Reset 向量在槽内）→ 跳转
     → APP 初始化完成后 param_boot_confirm() 自证签字
```

### 目录结构

```
ximenzi/
├── APP/                 # 应用工程（Keil APP.uvprojx）
│   ├── USER/            #   main.c、中断服务
│   ├── app/             #   业务层（调度器/任务/协议/命令/低功耗）
│   ├── middleware/      #   拆帧状态机 / 环形缓冲 / CRC16
│   ├── bsp/             #   外设初始化 + 引脚表
│   └── drivers/         #   器件驱动（Flash / 外部ADC / OLED）
├── Bootloader/          # Bootloader 工程（Keil BL.uvprojx）
│   ├── USER/
│   ├── core/            #   启动状态机 / OTA / 调试窗口
│   └── bsp/             #   精简版 tick / uart
├── shared/              # 两工程共用：分区表 / 参数存储 / FMC / CRC32 / 跳转
├── Library/ CMSIS/      # GD32 固件库
├── tools/
│   └── mk_ota_pkg.py    # OTA 升级包打包 / 校验脚本
├── docs/
│   ├── 协议命令与预期返回.md     # 全部命令的发送帧与预期应答（含上板验证步骤）
│   ├── ota_protocol.md          # OTA 包格式与升级流程
│   └── 低功耗测量.md            # Deep-sleep 电流测量方法
├── 外设配置速查手册.md   # 每个外设的四步配置法 + 上板实测记录
└── Bootloader跳转故障排查实录.md
```

---

## 串口协议

**USART0，115200 8N1，ASCII-Hex 文本传输**（1 字节 → 2 个 ASCII 字符，串口助手不要勾“HEX 发送”）。

```
A5B6 | dev_id(2) | type(1) | cmd(2) | len(1) | ver(1)=0x02 | payload | CRC16(2) | B6A5
```

- 多字节字段与 CRC 均**大端**；CRC16-Modbus（初值 `0xFFFF`，多项式 `0xA001`）
- 帧类型：`0x01` 命令 / `0x02` 应答 / `0x05` 心跳 / `0xFF` 错误
- `dev_id = 0xFFFF` 为广播；上电主动发心跳帧
- 流式拆帧状态机：一帧拆多发、两帧粘连、前导垃圾均能正确解析；CRC 错整帧丢弃

### 命令速览

| 类别 | 命令 |
|---|---|
| 系统管理 | `0x0101` 重启 · `0x0104` 版本 · `0x0105/06` RTC 时间读写 · `0x0111` 设备 ID · `0x0112` 波特率 · `0x01A1/A2` 设置 |
| 数据 | `0x0201/02` CH0/CH1（ADC×变比）· `0x0221` CH2 PT100 温度 · `0x0241/42` 变比 · `0x0261` 上报间隔 |
| 控制 | `0x0301` DAC 输出 · `0x0302/03` 开始/停止自动上报 · `0x03AA` 进入深睡 |
| 参数 | `0x0400~03` 读阈值 · `0x0411~13` 写阈值（CH0/CH1 落 Flash） |
| 告警 | `0x0601` 告警模式 · `0x0602` 查询记录（纯文本）· `0x0603` 清除 |

自动上报期间设备**只接受 `0x0303`**（停止上报），其余命令静默丢弃——上位机既有约定，须严格保持。

完整命令表（含每条命令的发送帧、预期应答帧、CRC、上板验证顺序）见 **docs/协议命令与预期返回.md**。

---

## OTA 升级

### 升级包格式（全部小端）

```
0x00  4   magic  = "OTA1"
0x04  4   size   固件字节数（1024 ~ 98304）
0x08  4   crc32  固件数据的 CRC32
0x0C  4   hcrc   前 12 字节的 CRC32
0x10  N   固件原始字节
```

CRC-32/ISO-HDLC（同 zip/gzip/PNG），打包工具与固件 `shared/crc.c` 两侧已交叉验证。

### 升级流程

```
① 复位，在 Bootloader 5 秒调试窗口发字符 '1'
   → update_flag = FAST_UPGRADE，落盘后自动复位
② BL 打印 "upgrade requested, entering OTA mode"，等待升级包（30s 超时）
③ 上位机整包连续发送（原始字节，无需等应答）
   BL 在字节流中三层对齐：magic → hcrc → size 合理性，然后只数长度
④ 收完整包在 RAM 中 CRC32 校验（接收期间全程不碰 Flash，失败零损伤）
⑤ 通过 → 擦写装入非活动槽 → 切 boot_slot → 清自证标记 → 复位
⑥ 新固件启动并初始化成功后写入自证标记；累计 3 次启动未自证 → 自动回滚旧槽
```

生成升级包：

```bash
python tools/mk_ota_pkg.py APP/Objects/SlotB/APP_B.hex -o ota_pkg.bin
python tools/mk_ota_pkg.py --check ota_pkg.bin   # 反向校验
```

详细说明见 **docs/ota_protocol.md**。

---

## 参数存储

- 4KB 参数页内布局：**主参数块 + 备份参数块**（各带 CRC）+ 告警记录区（10 条）+ BL 日志区
- 上电流程：读 → CRC 校验 → 主块坏用备份恢复 → 两份都坏重建默认 → v2 老参数自动迁移
- 写入采用**整页读-改-写**（Flash 擦除最小单位是 4KB 页，直接改会误伤同页其它区）
- 掉电保持的参数：设备 ID、波特率代码、CH0/CH1 变比与阈值、告警模式、上报间隔、DAC 值、启动槽位

---

## 低功耗

- 协议命令 `0x03AA` → 应答后关 OLED/电荷泵、AD3344 进 power-down、外设全部关闭 → MCU 进 **Deep-sleep**
- 唤醒：**PA0 按键**（当前默认）；RTC 定时唤醒代码已就绪（`bsp_pmu_deepsleep_rtc(10U)`，装上纽扣电池即可切换）
- 唤醒后按依赖顺序重建外设（`app_power_restore_after_wake`），随后串口输出 `instrument wakeup`
- 电流测量方法（全板电流 vs 仅 MCU 电流）见 **docs/低功耗测量.md**

---

## 构建与烧录

**工具链**：Keil MDK-ARM（ARM Compiler 6，ARMCLANG）

1. 打开工作区 `GD32F470.uvmpw`，内含两个工程：
   - `Bootloader/BL.uvprojx` — 链接基址 `0x08000000`
   - `APP/APP.uvprojx` — 默认编译 Slot A（链接基址 `0x08011000`）
2. 编译 Slot B 固件：在 APP 工程编译 Define 中加入 `APP_SLOT_B`（工程内已建对应目标，输出 `APP/Objects/SlotB/APP_B.hex`）
3. 首次烧录（调试器）：先烧 Bootloader，再烧 APP（Slot A 或 B 皆可）
4. 后续升级走串口 OTA，无需调试器

> 槽基址是编译期常量：`main()` 第一条语句把 VTOR 指向本槽基址，A/B 两槽由宏 `APP_SLOT_B` 区分。

---

## 已知限制 / 待办

| 项 | 现状 |
|---|---|
| OTA 进入方式 | 当前通过 BL 调试窗口发 `'1'`；App 侧协议命令 `0x0501` 待接入 |
| CH2 阈值持久化 | 只写 RAM，重启恢复默认 150.0 |
| RS485 业务口 | USART1 已初始化（含方向控制脚），尚未收发 |
| OLED | 驱动与任务就绪，硬件未接；深睡唤醒后暂不恢复 |
| RTC 定时唤醒 | 代码就绪，待装纽扣电池实测（当前用 PA0 按键唤醒） |
| OTA 分块 ACK / 断点续传 | 未做（设计取舍：丢包整包重发，约 2.5 秒） |

---

## 相关文档

- docs/协议命令与预期返回.md — 协议全量命令与预期应答
- docs/ota_protocol.md — OTA 包格式、三层对齐、失败日志
- docs/低功耗测量.md — Deep-sleep 电流测量方法
- 外设配置速查手册.md — 各外设四步配置法与上板实测记录
- Bootloader跳转故障排查实录.md — 跳转问题排查过程
