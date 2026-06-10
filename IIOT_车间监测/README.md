# IIOT — 车间智能通风与照明系统

## 工业场景
车间气压突变(门窗未关/密封失效) → 自动启动排风扇 → 气压恢复后关闭。
同时 HC-SR04 探测人员 + BH1750 检测照度 → 人员照明联动。

## 硬件清单
- ESP32-S3 开发板 x1
- BMP280 气压传感器 x1 (I2C 0x76)
- BH1750 光照传感器 x1 (I2C 0x23)
- HC-SR04 超声波测距模块 x1 (人员探测)
- OLED SSD1306 0.96寸 x1 (I2C 0x3C)
- 有源蜂鸣器 3.3V x1
- 130直流电机 + 桨叶 + TB6612驱动 x1 (排风扇)
- 面包板 + 杜邦线若干

## 接线表 (正点原子 DNESP32S3M)
| 外设 | 外设脚 | ESP32-S3 | 备注 |
|------|--------|----------|------|
| BMP280 | VCC,GND,SDA,SCL | 3.3V,GND,GPIO38,GPIO37 | I2C 0x76 (SDA=38,SCL=37) |
| BH1750 | VCC,GND,SDA,SCL | 3.3V,GND,GPIO38,GPIO37 | I2C 0x23，与BMP280并接 |
| OLED | VCC,GND,SDA,SCL | 3.3V,GND,GPIO38,GPIO37 | I2C 0x3C |
| HC-SR04 | VCC,GND,Trig,Echo | 5V,GND,GPIO4,GPIO5 | |
| 蜂鸣器 | +,- | GPIO6,GND | 有源 |
| TB6612 | PWMA,STBY,AIN1,AIN2,VM,VCC,GND | GPIO7,GPIO15,3.3V,GND,5V,3.3V,GND | 驱动130电机 |
| 130电机 | +/- | TB6612 AO1/AO2 | 桨叶当排风扇 |

### 接线示意图 (DNESP32S3M)
```
         ┌──────────────────────────┐
         │    DNESP32S3M (正点原子)    │
         │                          │
   3.3V──┼─┬─BMP280_VCC            │
   GND ──┼─┼─BMP280_GND            │
   D38 ──┼─┼─BMP280_SDA ──┬─BH1750_SDA ──┬─OLED_SDA
   D37 ──┼─┼─BMP280_SCL ──┼─BH1750_SCL ──┼─OLED_SCL
         │ │               │               │
   3.3V──┼─┴─BH1750_VCC    │               │
   GND ──┼───BH1750_GND    │               │
         │                  │               │
   3.3V──┼──────────────────┴─OLED_VCC      │
   GND ──┼──────────────────OLED_GND        │
         │                                   │
   D4 ───┼── HC-SR04 Trig                   │
   D5 ───┼── HC-SR04 Echo                   │
   D6 ───┼── 蜂鸣器 +                       │
   D7 ───┼── TB6612 PWMA (PWM)              │
   D15 ──┼── TB6612 STBY (使能)             │
         └──────────────────────────────────┘

TB6612其它脚: AIN1→3.3V, AIN2→GND, VM→5V, VCC→3.3V, AO1/AO2→130电机
```

## Arduino 库
- Adafruit BMP280 Library + Adafruit Unified Sensor
- BH1750 (Christopher Laws)
- Adafruit GFX + Adafruit SSD1306
- PubSubClient (Nick O'Leary)
- ArduinoJson (Benoit Blanchon)

## PC端
1. 安装 Mosquitto: https://mosquitto.org/download/
2. 安装 Python 依赖: `pip install flask paho-mqtt`
3. 启动 Mosquitto 服务
4. 启动大屏: `python iiot_dashboard.py`
5. 浏览器打开 http://localhost:5000

## MQTT 主题
| 主题 | 方向 | 说明 |
|------|------|------|
| sensor/data | ESP32→PC | JSON: pressure,lux,distance,person,fan,status |
| cmd/relay | PC→ESP32 | ON/OFF 控制排风扇 |

## 演示流程
1. 系统启动，气压基准校准完成
2. 用手靠近 HC-SR04 (<50cm) → 大屏显示"有人" + 照度不足时联动提示
3. 模拟气压突变(吹气到BMP280) → 排风扇自动启动，大屏风扇动画转动
4. PC 大屏点"停止"按钮 → 排风扇关闭
5. 气压恢复正常 → 自动关风扇
