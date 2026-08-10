# SHADE — Smart Home Adaptive Daylight Environment

> 普中A2 (STC89C52) + 树莓派 智能窗帘控制系统

SHADE 是一套基于 STC89C52 单片机 + 树莓派的智能窗帘系统：C51 固件负责传感器采集、PID 电机控制、本地按键交互与 LCD 显示；树莓派作为 SCADA 上位机，通过串口协议读写 42 字节寄存器映射表，实现远程监控与参数整定。整套硬件物料成本约 ¥52。

---

## 功能特性

### 固件侧（C51）
- **多传感器采集**：BME280（温/湿/压）、BH1750（光照）、HC-SR04（超声波测距）、PCF8591（光敏 ADC）
- **PID 步进电机控制**：28BYJ-48 + ULN2003，PID 闭环定位窗帘开合角度
- **本地交互**：LCD1602 实时显示、3 按键调参（MODE / UP / DOWN）
- **板载 LED 全利用**：P1.0~P1.3 状态指示（RUN/ALARM/MOTOR/备用），P1.4~P1.7 步进电机可视化
- **IDACS-Lite 协议**：自定义轻量串口协议，42 字节寄存器映射表
- **报警检测**：温/湿/光/距超阈值触发蜂鸣器 + LED 告警

### 上位机侧（树莓派）
- **SCADA 终端**：命令行交互式监控
- **寄存器读写**：`reg`/`set`/`set16` 命令直接操作 42 字节寄存器空间
- **参数整定**：`pid`/`thresh`/`speed`/`buzzer` 一键下发
- **数据上报解析**：自动解析传感器帧 + 告警帧
- **心跳保活**：周期性心跳检测通信链路

---

## 系统架构

```
┌─────────────────────── STC89C52 (普中A2) ───────────────────────┐
│                                                                  │
│  BME280 ─┐                                                       │
│  BH1750 ─┼─ 软 I2C (P2.0/P2.1) ─┐                               │
│  PCF8591 ┘                       │                               │
│                                  ├─ 1Hz 采样 ─→ 寄存器映射表      │
│  HC-SR04 ── P3.6/P3.2(INT0) ─────┘              (42 bytes)       │
│                                                                  │
│  28BYJ-48 ── ULN2003 ── P1.4~P1.7 ─→ PID ──→ 步进电机            │
│                                                                  │
│  LCD1602 ── P0 + P2.5~P2.7     按键 K2/K3/K4 ── P3.3~P3.5       │
│  LED 状态 ── P1.0~P1.3         蜂鸣器 ── P2.3                    │
│                                                                  │
└──────────────────────── UART (P3.0/P3.1) ────────────────────────┘
                              │ IDACS-Lite 协议
                              │ (USB-CH340 / 杜邦线)
                              ▼
┌─────────────────── 树莓派 (SCADA Host) ──────────────────────────┐
│  scada.py ── 命令行交互终端                                       │
│  ├─ idacs_protocol.py  帧编解码                                   │
│  ├─ serial_hal.py      串口抽象层                                 │
│  └─ register_model.py  42B 寄存器镜像 (1:1 对应 C51)              │
└──────────────────────────────────────────────────────────────────┘
```

---

## 硬件清单

| 模块 | 型号 | 接口 | 价格 |
|------|------|------|------|
| 主控 | 普中A2 开发板 (STC89C52) | — | 已有 |
| 温湿压 | BME280 | I2C 0x76 | ¥10 |
| 光照 | BH1750 GY-302 | I2C 0x23 | ¥6 |
| 测距 | HC-SR04 | GPIO (INT0) | ¥5 |
| ADC | PCF8591 | I2C 0x48 | ¥8 |
| 光敏 | LM393 4针 | 模拟 → PCF8591 | ¥4 |
| 电机 | 28BYJ-48 + ULN2003 | P1.4~P1.7 | ¥10 |
| 线材 | 杜邦线公母/公公 40根 | — | ¥8 |
| 鱼线 | 棉线卷 | — | ¥1 |
| **合计** | | | **≈¥52** |

---

## 目录结构

```
smart-curtain/
├── firmware/                        # C51 固件 (Keil C51)
│   └── src/
│       ├── main.c                   # 主程序 + Timer0/INT0 ISR
│       ├── config.h                 # 全局配置 + 引脚定义
│       ├── hal/
│       │   ├── i2c.c/h              # 软 I2C 驱动 (bit-bang)
│       ├── drivers/
│       │   ├── bme280.c/h           # 温湿压传感器
│       │   ├── bh1750.c/h           # 光照传感器
│       │   ├── hcsr04.c/h           # 超声波测距 (INT0 捕获)
│       │   ├── pcf8591.c/h          # ADC/DAC
│       │   └── stepper.c/h          # 28BYJ-48 步进电机
│       ├── control/
│       │   └── pid.c/h              # PID 位置环
│       ├── protocol/
│       │   └── idacs_lite.c/h       # IDACS-Lite 串口协议
│       └── ui/
│           └── lcd1602.c/h          # LCD1602 显示
├── host/                            # 树莓派上位机
│   ├── scada.py                     # SCADA 交互终端
│   ├── idacs_protocol.py            # 协议帧编解码
│   ├── serial_hal.py                # 串口抽象层
│   ├── register_model.py            # 42B 寄存器镜像
│   └── requirements.txt             # Python 依赖
├── 接线说明.txt                     # 完整硬件接线文档
└── README.md
```

---

## 寄存器映射表（42 字节）

C51 固件与树莓派上位机共享同一张 42 字节寄存器表，1:1 对应：

| 地址 | 名称 | 读写 | 长度 | 说明 |
|------|------|------|------|------|
| 0x00 | DEVICE_ID | RO | 2B | 设备 ID |
| 0x02 | FW_VERSION | RO | 2B | 固件版本 |
| 0x04 | SYS_STATUS | RO | 1B | 系统状态位 |
| 0x05 | ERROR_CODE | RO | 1B | 错误码 |
| 0x06 | TEMP_THRESH | RW | 2B | 温度阈值 (×10) |
| 0x08 | HUMI_THRESH | RW | 2B | 湿度阈值 (×10) |
| 0x0A | LIGHT_THRESH | RW | 2B | 光照阈值 (lux) |
| 0x0C | MOTOR_TARGET | RW | 2B | 电机目标角度 (×10) |
| 0x0E | MOTOR_SPEED | RW | 1B | 电机速度 1~10 |
| 0x0F | BUZZER_CTRL | RW | 1B | 蜂鸣器 0=off 1=慢 2=快 |
| 0x10 | PID_KP | RW | 2B | 比例 (×100) |
| 0x12 | PID_KI | RW | 2B | 积分 (×100) |
| 0x14 | PID_KD | RW | 2B | 微分 (×100) |
| 0x16 | SAMPLE_PERIOD | RW | 1B | 采样周期 (秒) |
| 0x17 | ALERT_MASK | RW | 1B | 告警掩码 |
| 0x18 | UPTIME | RO | 2B | 运行时长 (秒) |
| 0x1A | SENSOR_BASE | RO | 16B | 8 通道传感器数据 |

---

## 快速开始

### 固件编译（Keil C51）

1. 用 Keil µVision 打开 `firmware/src/` 工程
2. 选择 STC89C52RC 目标
3. 编译生成 `.hex`
4. 用 STC-ISP 烧录到普中A2 开发板

### 上位机启动（树莓派）

```bash
cd host
pip install -r requirements.txt   # pyserial + rich

# 自动检测串口
python3 scada.py

# 或指定串口
python3 scada.py /dev/ttyUSB0
```

### 常用命令

```
reg <addr> [count]         读寄存器
set <addr> <val> [val...]  写寄存器字节
set16 <addr> <val>         写 16 位寄存器
thresh <lux>               设置光照阈值
pid <kp> <ki> <kd>         设置 PID 参数 (×100)
speed <1-10>               设置电机速度
buzzer <0|1|2>             蜂鸣器模式
quit                       退出
```

---

## 引脚占用总览

| 端口 | 功能 |
|------|------|
| P0 全口 | LCD1602 数据总线 |
| P1.0~P1.3 | LED 状态指示（绿 RUN / 红 ALARM / 黄 MOTOR / 备用） |
| P1.4~P1.7 | 28BYJ-48 步进电机 IN1~IN4（+ LED 步进可视化） |
| P2.0~P2.1 | 软 I2C 总线（SCL/SDA） |
| P2.3 | 蜂鸣器 |
| P2.5~P2.7 | LCD1602 控制线（RS/RW/EN） |
| P3.0~P3.1 | UART 串口（与树莓派通信） |
| P3.2 | HC-SR04 ECHO（占用 INT0，K1 按键作废） |
| P3.3~P3.5 | 按键 K2(MODE) / K3(UP) / K4(DOWN) |
| P3.6 | HC-SR04 TRIG |

**空闲引脚**：P2.2 / P2.4 / P3.7（3 脚备用）

> 详细接线见 [接线说明.txt](接线说明.txt)

---

## 设计亮点

1. **零外接 LED** — 板载 8 颗 LED 全部利用：状态灯 + 步进电机步进可视化
2. **42 字节寄存器映射** — C51 与 Pi 共享同一张表，1:1 镜像，协议极简
3. **IDACS-Lite 自定义协议** — 轻量串口帧，含心跳、读寄存器、写寄存器、数据上报、告警 5 种帧类型
4. **软 I2C 总线复用** — BME280 + BH1750 + PCF8591 三设备挂同一对 SDA/SCL
5. **PID 位置环** — 步进电机角度闭环，避免开环丢步
6. **INT0 边沿捕获** — HC-SR04 ECHO 用硬件中断计时，精度高于软件轮询
