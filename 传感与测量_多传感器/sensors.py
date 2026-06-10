# -*- coding: utf-8 -*-
"""
传感与测量 — 多传感器融合监测系统 (Flask Web 版)
硬件: 树莓派5B + BMP280 + BH1750 + HC-SR04 + PIR + LED + Pi Camera
"""
import time, csv, os, threading, lgpio
from datetime import datetime
from flask import Flask, jsonify, render_template_string, Response

app = Flask(__name__)

# ===================== 传感器驱动 =====================

# --- BMP280 温度+气压 (I2C) ---
BMP_OK = False
BMP280_ADDR = 0x76
_bmp_t_fine = 0

def read_bmp280():
    """读取 BMP280, 返回 (temp_C, press_hPa) 或 (None, None)"""
    global _bmp_t_fine
    try:
        # 强制模式: T×1 P×1
        _bus.write_byte_data(BMP280_ADDR, 0xF4, 0x25)
        time.sleep(0.05)
        # 读原始数据 (6 字节: press[3] temp[3])
        raw = _bus.read_i2c_block_data(BMP280_ADDR, 0xF7, 6)
        p_raw = (raw[0] << 12) | (raw[1] << 4) | (raw[2] >> 4)
        t_raw = (raw[3] << 12) | (raw[4] << 4) | (raw[5] >> 4)
        # 温度补偿
        v1 = (t_raw / 16384.0 - _dig_T1 / 1024.0) * _dig_T2
        v2 = ((t_raw / 131072.0 - _dig_T1 / 8192.0) ** 2) * _dig_T3
        _bmp_t_fine = v1 + v2
        T = _bmp_t_fine / 5120.0
        # 气压补偿
        v1 = _bmp_t_fine / 2.0 - 64000.0
        v2 = v1 * v1 * _dig_P6 / 32768.0
        v2 = v2 + v1 * _dig_P5 * 2.0
        v2 = v2 / 4.0 + _dig_P4 * 65536.0
        v1 = (_dig_P3 * v1 * v1 / 524288.0 + _dig_P2 * v1) / 524288.0
        v1 = (1.0 + v1 / 32768.0) * _dig_P1
        P = 1048576.0 - p_raw
        P = (P - v2 / 4096.0) * 6250.0 / v1 if v1 else 0
        v1 = _dig_P9 * P * P / 2147483648.0
        v2 = P * _dig_P8 / 32768.0
        P = P + (v1 + v2 + _dig_P7) / 16.0
        P = P / 100.0  # Pa -> hPa
        return round(T, 1), round(P, 1)
    except:
        return None, None

# 初始化 BMP280
try:
    import smbus2
    _bus = smbus2.SMBus(1)
    # 读取校准数据 (T1-T3 P1-P9, 共24字节)
    cal = _bus.read_i2c_block_data(BMP280_ADDR, 0x88, 24)
    _dig_T1 = (cal[1] << 8) | cal[0]
    _dig_T2 = (cal[3] << 8) | cal[2] if cal[3] < 128 else ((cal[3] << 8) | cal[2]) - 65536
    _dig_T3 = (cal[5] << 8) | cal[4] if cal[5] < 128 else ((cal[5] << 8) | cal[4]) - 65536
    _dig_P1 = (cal[7] << 8) | cal[6]
    _dig_P2 = (cal[9] << 8) | cal[8] if cal[9] < 128 else ((cal[9] << 8) | cal[8]) - 65536
    _dig_P3 = (cal[11] << 8) | cal[10] if cal[11] < 128 else ((cal[11] << 8) | cal[10]) - 65536
    _dig_P4 = (cal[13] << 8) | cal[12] if cal[13] < 128 else ((cal[13] << 8) | cal[12]) - 65536
    _dig_P5 = (cal[15] << 8) | cal[14] if cal[15] < 128 else ((cal[15] << 8) | cal[14]) - 65536
    _dig_P6 = (cal[17] << 8) | cal[16] if cal[17] < 128 else ((cal[17] << 8) | cal[16]) - 65536
    _dig_P7 = (cal[19] << 8) | cal[18] if cal[19] < 128 else ((cal[19] << 8) | cal[18]) - 65536
    _dig_P8 = (cal[21] << 8) | cal[20] if cal[21] < 128 else ((cal[21] << 8) | cal[20]) - 65536
    _dig_P9 = (cal[23] << 8) | cal[22] if cal[23] < 128 else ((cal[23] << 8) | cal[22]) - 65536
    _t, _p = read_bmp280()
    if _t is not None:
        BMP_OK = True
        print(f"[BMP280] OK  {_t:.1f}C  {_p:.1f}hPa")
    else:
        print("[BMP280] SKIP: no data")
except Exception as e:
    print(f"[BMP280] ERR: {e}")

# --- BH1750 光照 (I2C, 共用 _bus) ---
BH_OK = False
try:
    BH1750_ADDR = 0x23
    def read_bh1750():
        data = _bus.read_i2c_block_data(BH1750_ADDR, 0x10, 2)
        return round((data[0] << 8 | data[1]) / 1.2, 1)
    _ = read_bh1750()
    BH_OK = True
    print("[BH1750] OK")
except Exception as e:
    print(f"[BH1750] SKIP: {e}")

# --- HC-SR04 超声波 (lgpio) ---
SR04_OK = False
TRIG, ECHO = 23, 24
PIR_PIN = 17
LED_PIN = 27
try:
    _h_gpio = lgpio.gpiochip_open(0)
    lgpio.gpio_claim_output(_h_gpio, TRIG, 0)
    lgpio.gpio_claim_input(_h_gpio, ECHO)
    lgpio.gpio_claim_input(_h_gpio, PIR_PIN)
    lgpio.gpio_claim_output(_h_gpio, LED_PIN, 0)

    def read_hcsr04():
        lgpio.gpio_write(_h_gpio, TRIG, 0); time.sleep(0.000002)
        lgpio.gpio_write(_h_gpio, TRIG, 1); time.sleep(0.000010)
        lgpio.gpio_write(_h_gpio, TRIG, 0)
        timeout = time.time() + 0.03
        pulse_start = pulse_end = time.time()
        while lgpio.gpio_read(_h_gpio, ECHO) == 0 and time.time() < timeout:
            pulse_start = time.time()
        while lgpio.gpio_read(_h_gpio, ECHO) == 1 and time.time() < timeout:
            pulse_end = time.time()
        if time.time() >= timeout:
            return -1
        return round((pulse_end - pulse_start) * 17150, 1)

    _ = read_hcsr04()
    SR04_OK = True
    print("[HC-SR04] OK")
except Exception as e:
    print(f"[HC-SR04] SKIP: {e}")

# --- PIR 红外移动传感器 (lgpio) ---
PIR_OK = False
try:
    if SR04_OK:
        def read_pir():
            return lgpio.gpio_read(_h_gpio, PIR_PIN)
        PIR_OK = True
        print("[PIR] OK")
except Exception as e:
    print(f"[PIR] SKIP: {e}")

# --- Pi Camera ---
CAM_OK = False
try:
    from picamera2 import Picamera2
    _picam = Picamera2()
    _picam.configure(_picam.create_still_configuration(main={"size": (640, 480)}))
    _picam.start()
    CAM_OK = True
    print("[Camera] OK")
except Exception as e:
    print(f"[Camera] SKIP: {e}")

# ===================== 数据共享 =====================
lock = threading.Lock()
data = {
    "temp": None, "press": None, "lux": None,
    "dist": None, "pir": 0, "led": 0,
    "ts": "", "event": ""
}

CSV_PATH = os.path.expanduser("~/sensors_data.csv")
EVENT_LOG = os.path.expanduser("~/event_log.txt")

def log_event(msg):
    ts = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
    line = f"[{ts}] {msg}"
    with open(EVENT_LOG, "a", encoding="utf-8") as f:
        f.write(line + "\n")

# ===================== 传感器轮询 =====================
def sensor_loop():
    last_pir = 0
    photo_counter = 0

    with open(CSV_PATH, "w", newline="") as f:
        writer = csv.writer(f)
        writer.writerow(["timestamp", "temp_C", "press_hPa", "lux", "distance_cm", "pir_state", "led_state", "event"])

        while True:
            ts = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
            t, p, lux, dist, pir, led_state = None, None, None, None, 0, 0
            event = ""

            # BMP280
            if BMP_OK:
                try:
                    _t, _p = read_bmp280()
                    t = round(_t, 1) if _t else None
                    p = round(_p, 1) if _p else None
                except: pass

            # BH1750
            if BH_OK:
                try: lux = read_bh1750()
                except: pass

            # HC-SR04
            if SR04_OK:
                try:
                    d = read_hcsr04()
                    dist = d if d > 0 else None
                except: pass

            # PIR
            if PIR_OK:
                try:
                    pir = read_pir()
                    if pir == 1 and last_pir == 0:
                        lgpio.gpio_write(_h_gpio, LED_PIN, 1)
                        photo_counter += 1
                        fname = f"motion_{photo_counter}.jpg"
                        if CAM_OK:
                            try: _picam.capture_file(os.path.expanduser(f"~/{fname}"))
                            except: pass
                        event = f"MOTION|{fname}"
                        log_event(f"人员移动! 拍照: {fname}")
                    elif pir == 0 and last_pir == 1:
                        time.sleep(1.5)
                        lgpio.gpio_write(_h_gpio, LED_PIN, 0)
                    last_pir = pir
                    led_state = lgpio.gpio_read(_h_gpio, LED_PIN)
                except: pass

            with lock:
                data.update(temp=t, press=p, lux=lux, dist=dist, pir=pir, led=led_state, ts=ts, event=event)

            # CSV
            try:
                writer.writerow([ts, t, p, lux, dist, pir, led_state, event])
                f.flush()
            except: pass

            time.sleep(3)

threading.Thread(target=sensor_loop, daemon=True).start()

# ===================== 摄像头视频流 =====================
def gen_frames():
    if not CAM_OK:
        return
    while True:
        try:
            import io
            stream = io.BytesIO()
            _picam.capture_file(stream, format='jpeg')
            stream.seek(0)
            yield (b'--frame\r\nContent-Type: image/jpeg\r\n\r\n' + stream.read() + b'\r\n')
            time.sleep(0.5)
        except: pass

@app.route("/video_feed")
def video_feed():
    return Response(gen_frames(), mimetype='multipart/x-mixed-replace; boundary=frame')

# ===================== Web 页面 =====================
HTML = """<!DOCTYPE html>
<html lang="zh"><head><meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>传感与测量 多传感器监测</title>
<style>
*{margin:0;padding:0;box-sizing:border-box}
body{font-family:'Microsoft YaHei',sans-serif;background:#0d1117;color:#c9d1d9;
min-height:100vh;display:flex;flex-direction:column;align-items:center;padding:15px}
h1{color:#58a6ff;font-size:20px;margin-bottom:10px}
.grid{display:grid;grid-template-columns:repeat(3,1fr);gap:10px;max-width:700px;width:100%;margin-bottom:15px}
.c{background:#161b22;border:1px solid #30363d;border-radius:12px;padding:15px;text-align:center}
.c .v{font-size:36px;font-weight:bold;color:#58a6ff}
.c .l{font-size:12px;color:#8b949e;margin-top:4px}
.status{display:inline-block;width:12px;height:12px;border-radius:50%;margin-right:4px}
.on{background:#3fb950} .off{background:#484f58}
.cam{background:#161b22;border:1px solid #30363d;border-radius:12px;padding:10px;margin-bottom:15px;max-width:700px;width:100%;text-align:center}
.cam img{width:100%;max-width:640px;border-radius:8px}
.ev{background:#161b22;border:1px solid #30363d;border-radius:12px;padding:10px;max-width:700px;width:100%;margin-top:10px;font-size:13px;color:#f0883e;min-height:20px}
.ft{margin-top:8px;font-size:12px;color:#8b949e}
</style></head><body>
<h1>传感与测量 多传感器实时监测</h1>
<div class="grid">
  <div class="c"><div class="v" id="t">--</div><div class="l">温度 °C</div></div>
  <div class="c"><div class="v" id="p">--</div><div class="l">气压 hPa</div></div>
  <div class="c"><div class="v" id="l">--</div><div class="l">光照 lux</div></div>
  <div class="c"><div class="v"><span class="status" id="pir_s"></span><span id="pir">--</span></div><div class="l">PIR 移动检测</div></div>
  <div class="c"><div class="v"><span class="status" id="led_s"></span><span id="led">--</span></div><div class="l">LED 状态</div></div>
</div>
<div class="cam">
  <div style="margin-bottom:8px;color:#58a6ff;font-size:14px">Pi Camera 实时画面</div>
  <img src="/video_feed" alt="摄像头离线">
</div>
<div class="ev" id="ev">事件: 等待中...</div>
<div class="ft" id="s">连接中...</div>
<script>
async function poll(){
  try{
    const d=await(await fetch('/api/data')).json();
    document.getElementById('t').textContent=d.temp!=null?d.temp:'--';
    document.getElementById('p').textContent=d.press!=null?d.press:'--';
    document.getElementById('l').textContent=d.lux!=null?d.lux:'--';
    document.getElementById('pir').textContent=d.pir?'有人':'无人';
    document.getElementById('pir_s').className='status '+(d.pir?'on':'off');
    document.getElementById('led').textContent=d.led?'亮':'灭';
    document.getElementById('led_s').className='status '+(d.led?'on':'off');
    document.getElementById('ev').textContent='事件: '+(d.event||'无');
    document.getElementById('s').textContent=d.ts;
  }catch(e){document.getElementById('s').textContent='连接错误';}
}
setInterval(poll,2000);poll();
</script></body></html>"""

@app.route("/")
def index():
    return render_template_string(HTML)

@app.route("/api/data")
def api_data():
    with lock: d = dict(data)
    return jsonify(d)

if __name__ == "__main__":
    print("=" * 50)
    print("  传感与测量 多传感器监测 (Web版)")
    print(f"  BMP280:   {'OK' if BMP_OK else 'FAIL'}")
    print(f"  BH1750:   {'OK' if BH_OK else 'FAIL'}")
    print(f"  HC-SR04:  {'OK' if SR04_OK else 'FAIL'}")
    print(f"  PIR:      {'OK' if PIR_OK else 'FAIL'}")
    print(f"  Camera:   {'OK' if CAM_OK else 'FAIL'}")
    print("=" * 50)
    app.run(host="0.0.0.0", port=5000, debug=False)
