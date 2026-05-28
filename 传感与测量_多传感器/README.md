# 传感与测量技术 - 多传感器融合监测系统

## 硬件清单
- 树莓派 4B/5B x1
- Pi Camera 官方摄像头 x1
- DHT22 温湿度传感器 x1
- BH1750 光照传感器 x1
- MPU6050 六轴加速度计 x1
- HC-SR04 超声波测距模块 x1
- 面包板 + 杜邦线若干

## 接线表
| 外设 | 外设脚 | 树莓派(BCM) | 备注 |
|------|--------|-------------|------|
| DHT22 | DATA | GPIO4 | 加10k上拉 |
| BH1750 | SDA,SCL | GPIO2,3(I2C1) | 地址0x23 |
| MPU6050 | SDA,SCL | GPIO2,3(I2C1) | 地址0x68 |
| HC-SR04 | Trig,Echo | GPIO23,24 | 5V供电 |
| Camera | CSI排线 | 专用接口 | |

## 安装依赖
```bash
# 树莓派上执行
sudo apt install python3-pip python3-picamera2
pip3 install -r requirements.txt
```

## 运行步骤
1. 把 sensors.py 和 app.py 放到 Pi 的 home 目录
2. 先运行采集脚本（后台）:
   ```bash
   python3 sensors.py &
   ```
   这会每2秒采集一次，写入 ~/sensors_data.csv

3. 再运行仪表盘（新终端或前台）:
   ```bash
   python3 app.py
   ```
4. 浏览器访问 http://<树莓派IP>:5000

## 演示流程
1. Flask 网页投屏: 4条实时曲线 + 实时数值 + 摄像头画面
2. 哈气到 DHT22: 温湿度曲线跳变
3. 手遮 BH1750: 光照从几百跌到个位数 lux
4. 晃动面包板: MPU6050 三轴曲线剧烈波动
5. 手靠近 HC-SR04: 距离值从150cm骤降30cm
6. 展示标定数据表 (每种传感器10组真值vs测量值)

## 传感器种类覆盖（对应考核评分）
| 传感器 | 考核类别 | 说明 |
|--------|---------|------|
| DHT22 | 温度传感器(20分) + 湿度(20分) | 工业标准 |
| BH1750 | 光电传感器(10分) | 数字光照 |
| MPU6050 | 力学传感器(20分) | 加速度计 |
| HC-SR04 | 位移传感器(20分) | 超声波测距 |
| Camera | 光电传感器(10分) | CMOS图像传感器 |
