#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
传感与测量技术 — 多传感器融合监测系统
硬件: 树莓派5B + DHT22 + BH1750 + HC-SR04 + PIR + LED + Pi Camera
输出: sensors.csv (时间戳 + 传感器数据 + 事件)
运行: python3 sensors.py
按 Ctrl+C 停止采集
"""

import time
import csv
import os
from datetime import datetime

# ===================== 传感器驱动 =====================

# --- DHT22 温湿度 ---
import adafruit_dht
import board
dht = adafruit_dht.DHT22(board.D4, use_pulseio=False)

# --- BH1750 光照 (I2C) ---
import smbus2
BH1750_ADDR = 0x23
bus = smbus2.SMBus(1)

def read_bh1750():
    """返回照度 (lux)"""
    data = bus.read_i2c_block_data(BH1750_ADDR, 0x10, 2)
    return (data[0] << 8 | data[1]) / 1.2

# --- HC-SR04 超声波 ---
import RPi.GPIO as GPIO
TRIG, ECHO = 23, 24
PIR_PIN = 17      # PIR 红外移动传感器 OUT
LED_PIN = 27      # LED 指示灯

GPIO.setmode(GPIO.BCM)
GPIO.setup(TRIG, GPIO.OUT)
GPIO.setup(ECHO, GPIO.IN)
GPIO.setup(PIR_PIN, GPIO.IN)
GPIO.setup(LED_PIN, GPIO.OUT)
GPIO.output(LED_PIN, GPIO.LOW)

def read_hcsr04():
    """返回距离 (cm)"""
    GPIO.output(TRIG, False); time.sleep(0.000002)
    GPIO.output(TRIG, True);  time.sleep(0.000010)
    GPIO.output(TRIG, False)
    timeout = time.time() + 0.03
    pulse_start = pulse_end = time.time()
    while GPIO.input(ECHO) == 0 and time.time() < timeout:
        pulse_start = time.time()
    while GPIO.input(ECHO) == 1 and time.time() < timeout:
        pulse_end = time.time()
    if time.time() >= timeout:
        return -1
    return (pulse_end - pulse_start) * 17150

def read_pir():
    """返回 PIR 状态: 1=检测到移动, 0=无"""
    return GPIO.input(PIR_PIN)

# --- Pi Camera ---
from picamera2 import Picamera2
picam = Picamera2()
picam.configure(picam.create_still_configuration(main={"size": (640, 480)}))
picam.start()

def capture(filename="captured.jpg"):
    """拍照保存"""
    picam.capture_file(filename)
    return filename

# ===================== 主程序 =====================

CSV_PATH = os.path.expanduser("~/sensors_data.csv")
EVENT_LOG = os.path.expanduser("~/event_log.txt")

def log_event(msg):
    """写事件日志"""
    ts = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
    line = f"[{ts}] {msg}"
    with open(EVENT_LOG, "a", encoding="utf-8") as f:
        f.write(line + "\n")
    return line

def main():
    print("=" * 55)
    print("  传感与测量 — 多传感器数据采集")
    print("  DHT22 + BH1750 + HC-SR04 + PIR + Camera + LED")
    print("  输出:", CSV_PATH)
    print("  事件日志:", EVENT_LOG)
    print("  按 Ctrl+C 停止")
    print("=" * 55)

    capture("captured_0.jpg")  # 初始照片
    last_pir = 0               # PIR 上一状态（防重复触发）
    photo_counter = 0

    with open(CSV_PATH, "w", newline="") as f:
        writer = csv.writer(f)
        writer.writerow([
            "timestamp", "temp_C", "humidity_%", "lux",
            "distance_cm", "pir_state", "led_state", "event"
        ])

        while True:
            try:
                ts = datetime.now().strftime("%Y-%m-%d %H:%M:%S")

                # DHT22
                try:
                    t = dht.temperature
                    h = dht.humidity
                except RuntimeError:
                    t, h = None, None

                # BH1750
                try:
                    lux = read_bh1750()
                except:
                    lux = None

                # HC-SR04
                dist = read_hcsr04()
                if dist < 0: dist = None

                # PIR
                pir = read_pir()
                event = ""

                # 检测到移动 → LED亮 + 拍照 + 记事件
                if pir == 1 and last_pir == 0:
                    GPIO.output(LED_PIN, GPIO.HIGH)
                    photo_counter += 1
                    fname = f"motion_{photo_counter}.jpg"
                    capture(fname)
                    event = f"MOTION_DETECTED|photo={fname}"
                    msg = log_event(f"人员移动检测! 已拍照: {fname}")
                    print(f"\n  !! {msg}")

                elif pir == 0 and last_pir == 1:
                    # 人离开，延时关灯
                    time.sleep(1.5)
                    GPIO.output(LED_PIN, GPIO.LOW)

                last_pir = pir
                led_state = GPIO.input(LED_PIN)

                row = [ts, t, h, lux, dist, pir, led_state, event]
                writer.writerow(row)
                f.flush()

                # 终端输出
                pir_str = "!!有人" if pir else "-"
                led_str = "LED" if led_state else "   "
                print(f"[{ts}] "
                      f"T={t or '--':>5}  H={h or '--':>5}  "
                      f"L={lux or '--':>6}  D={dist or '--':>5}cm  "
                      f"PIR={pir_str} {led_str}"
                      + (f"  -> {fname}" if event else ""))

                time.sleep(2)

            except KeyboardInterrupt:
                print("\n采集停止。")
                break

    GPIO.output(LED_PIN, GPIO.LOW)
    picam.stop()
    GPIO.cleanup()
    print(f"数据已保存至 {CSV_PATH}")
    print(f"事件日志: {EVENT_LOG}")

if __name__ == "__main__":
    main()
