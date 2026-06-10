#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
传感与测量技术 期末项目 — 多传感器实时监测（单文件版）
运行: python3 dashboard.py
访问: http://<树莓派IP>:5000
"""

import time, threading, os
from datetime import datetime
from flask import Flask, jsonify, render_template_string, send_file

# ===================== 传感器 =====================
try:
    import adafruit_dht
    import board
    dht = adafruit_dht.DHT22(board.D4, use_pulseio=False)
    DHT_OK = True
except: DHT_OK = False

try:
    import smbus2
    bus = smbus2.SMBus(1)
    BH1750_ADDR = 0x23
    def read_bh1750():
        data = bus.read_i2c_block_data(BH1750_ADDR, 0x20, 2)
        return int((data[0] << 8) | data[1]) / 1.2
    BH_OK = True
except: BH_OK = False

try:
    from picamera2 import Picamera2
    picam2 = Picamera2()
    picam2.configure(picam2.create_preview_configuration(
        main={"size": (640, 480)}))
    picam2.start()
    CAM_OK = True
except: CAM_OK = False

# ===================== 共享数据 =====================
data_lock = threading.Lock()
sensor_data = {
    "temp": None, "humi": None, "lux": None,
    "distance": 0, "pir": 0, "led": 0,
    "ts": "", "cam_ok": False
}

# ===================== 后台采集 =====================
def sensor_loop():
    while True:
        ts = datetime.now().strftime("%H:%M:%S")
        t, h, lux = None, None, None

        if DHT_OK:
            try:
                t = dht.temperature
                h = dht.humidity
            except: pass

        if BH_OK:
            try: lux = read_bh1750()
            except: pass

        with data_lock:
            sensor_data["temp"] = round(t, 1) if t else None
            sensor_data["humi"] = round(h, 1) if h else None
            sensor_data["lux"] = int(lux) if lux else None
            sensor_data["ts"] = ts
        time.sleep(2)

def camera_loop():
    if not CAM_OK: return
    while True:
        try:
            picam2.capture_file("captured.jpg")
            with data_lock:
                sensor_data["cam_ok"] = True
        except: pass
        time.sleep(1)

threading.Thread(target=sensor_loop, daemon=True).start()
threading.Thread(target=camera_loop, daemon=True).start()

# ===================== Flask =====================
app = Flask(__name__)

HTML = """<!DOCTYPE html>
<html lang="zh">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>传感与测量 - 实时监测</title>
<style>
*{margin:0;padding:0;box-sizing:border-box}
body{font-family:'Microsoft YaHei',sans-serif;background:#0d1117;color:#c9d1d9;min-height:100vh;padding:15px}
h1{text-align:center;color:#58a6ff;font-size:20px;margin-bottom:12px}
.live{display:grid;grid-template-columns:repeat(5,1fr);gap:8px;text-align:center;margin-bottom:15px}
.live>div{background:#161b22;border:1px solid #30363d;border-radius:8px;padding:12px 5px}
.val{font-size:32px;font-weight:bold;color:#58a6ff}
.lbl{font-size:11px;color:#8b949e;margin-top:4px}
.cam{display:flex;justify-content:center;margin-top:10px}
.cam img{max-width:100%;max-height:480px;border-radius:10px;border:2px solid #30363d;background:#000}
.status{text-align:center;margin-top:10px;font-size:12px;color:#3fb950}
</style>
</head>
<body>
<h1>📡 传感与测量 — 多传感器实时监测</h1>
<div class="live">
  <div><div class="val" id="t">--</div><div class="lbl">温度 °C</div></div>
  <div><div class="val" id="h">--</div><div class="lbl">湿度 %</div></div>
  <div><div class="val" id="l">--</div><div class="lbl">光照 lux</div></div>
  <div><div class="val" id="d">--</div><div class="lbl">距离 cm</div></div>
  <div><div class="val" id="p">--</div><div class="lbl">人体红外</div></div>
</div>
<div class="cam">
  <img id="cam" src="/photo" alt="摄像头画面">
</div>
<div class="status" id="st">连接中...</div>

<script>
async function poll(){
  try{
    let d=await(await fetch('/api/data')).json();
    document.getElementById('t').textContent=d.temp??'--';
    document.getElementById('h').textContent=d.humi??'--';
    document.getElementById('l').textContent=d.lux??'--';
    document.getElementById('d').textContent=d.dist??'--';
    document.getElementById('p').textContent=d.pir?'有人':'无人';
    document.getElementById('cam').src='/photo?'+Date.now();
    document.getElementById('st').textContent='✅ 实时更新中 — '+d.ts;
  }catch(e){
    document.getElementById('st').textContent='❌ 连接失败';
  }
}
setInterval(poll,2000);poll();
</script>
</body>
</html>"""

@app.route("/")
def index():
    return render_template_string(HTML)

@app.route("/api/data")
def api_data():
    with data_lock:
        d = dict(sensor_data)
    return jsonify({
        "temp": d["temp"], "humi": d["humi"], "lux": d["lux"],
        "dist": d["distance"], "pir": d["pir"], "ts": d["ts"]
    })

@app.route("/photo")
def photo():
    if os.path.exists("captured.jpg"):
        return send_file("captured.jpg", mimetype="image/jpeg")
    return "No image", 404

if __name__ == "__main__":
    print("=" * 50)
    print("  传感与测量 — 单文件实时监测")
    print(f"  DHT22: {'OK' if DHT_OK else 'FAIL'}")
    print(f"  BH1750: {'OK' if BH_OK else 'FAIL'}")
    print(f"  Camera: {'OK' if CAM_OK else 'FAIL'}")
    print(f"  访问: http://0.0.0.0:5000")
    print("=" * 50)
    app.run(host="0.0.0.0", port=5000, debug=False)
