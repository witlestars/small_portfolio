# IIOT - 基于MQTT的车间环境与设备状态监测系统

## 硬件清单
- ESP32-S3 开发板 x1
- DHT22 温湿度传感器 x1（3脚模块）
- BH1750 光照传感器 x1
- ACS712 5A 电流传感器 x1
- HC-SR04 超声波测距模块 x1
- OLED SSD1306 0.96寸 x1
- 2路5V继电器模块 x1（低电平触发）
- 有源蜂鸣器 3.3V x1
- 130直流电机 + 桨叶 + L9110S驱动 x1
- 面包板 + 杜邦线若干

## 接线表
| 外设 | 外设脚 | ESP32-S3 | 备注 |
|------|--------|----------|------|
| DHT22 | DATA | GPIO4 | DATA线加10k上拉到3.3V |
| BH1750 | SDA,SCL | GPIO21,22 | I2C地址0x23 |
| ACS712 | VOUT | GPIO34(ADC) | 串联在电机供电回路中 |
| HC-SR04 | Trig,Echo | GPIO5,18 | 5V供电 |
| OLED | SDA,SCL | GPIO21,22 | I2C地址0x3C |
| 继电器 | IN | GPIO26 | COM-NC串在电机回路 |
| 蜂鸣器 | + | GPIO27 | 有源 |
| L9110S | INA,INB | GPIO32,33 | PWM控制电机 |

## PC端环境
1. 安装 Mosquitto MQTT Broker
   https://mosquitto.org/download/
2. 安装 Node-RED
   npm install -g node-red node-red-dashboard
3. 启动顺序
   - 先启动 Mosquitto 服务
   - 再 cmd 执行 node-red
   - 浏览器打开 http://localhost:1880

## Node-RED 仪表盘搭建
1. 拖入 mqtt in 节点:
   - Server: localhost:1883
   - Topic: sensor/data
   - QoS: 1
   - Output: parsed JSON
2. 连接 chart 节点 x4 (temp/humi/lux/current/distance 各一条)
3. 连接 gauge 节点 x2 (温度/电流瞬时值)
4. function 节点报警逻辑:
   if (msg.payload.current > 0.5) { msg.payload = "ALERT"; }
5. 连接 ui_text + ui_led 显示报警
6. 添加 ui_switch --> mqtt out (Topic: cmd/relay) 远程断电
7. Dashboard 布局: 环境区|设备区|控制区

## Arduino 库安装
- DHT sensor library (Adafruit)
- BH1750 (Christopher Laws)
- Adafruit GFX + Adafruit SSD1306
- PubSubClient (Nick O'Leary)
- ArduinoJson (Benoit Blanchon)

## 烧录
- 开发板: ESP32S3 Dev Module
- Flash Size: 16MB
- 修改代码中 WIFI_SSID/WIFI_PASS/MQTT_BROKER 为实际值
- 上传

## 演示流程
1. 电机空转: 电流 0.18A, 仪表盘绿灯, OLED显示NORMAL
2. 手指轻压电机轴增加负载: 电流飙升至 0.6~0.8A
3. 连续3次超阈值(0.5A): 蜂鸣+OLED显示ALERT+继电器断电
4. 仪表盘点"恢复"按钮: 继电器闭合, 电机重新运转
