# -*- coding: utf-8 -*-
"""传感与测量 — 多传感器实时监测仪表盘"""
import time, threading
from flask import Flask, jsonify, render_template_string

app = Flask(__name__)

# ===================== 传感器初始化 =====================
DHT_OK = BH_OK = False

# --- DHT22 (GPIO 4 = pin 7) ---
try:
    import Adafruit_DHT
    _DHT_SENSOR = Adafruit_DHT.DHT22
    _DHT_PIN = 4  # BCM 4 = pin 7
    _h, _t = Adafruit_DHT.read_retry(_DHT_SENSOR, _DHT_PIN)
    if _t is not None:
        DHT_OK = True
        print(f"[DHT22] OK  {_t:.1f}°C")
    else:
        print("[DHT22] SKIP: sensor not responding")
except Exception as e:
    print(f"[DHT22] SKIP: {e}")

# --- BH1750 (I2C 0x23) ---
try:
    import smbus2
    _bus = smbus2.SMBus(1)
    _BH_ADDR = 0x23
    def read_bh1750():
        d = _bus.read_i2c_block_data(_BH_ADDR, 0x20, 2)
        return round(int((d[0] << 8) | d[1]) / 1.2, 1)
    _ = read_bh1750()
    BH_OK = True
    print("[BH1750] OK")
except Exception as e:
    print(f"[BH1750] SKIP: {e}")

# ===================== 传感器读取线程 =====================
lock = threading.Lock()
data = {"temp": None, "humi": None, "lux": None, "ts": ""}

def sensor_loop():
    while True:
        ts = time.strftime("%H:%M:%S")
        t, h, lux = None, None, None
        if DHT_OK:
            try:
                h, t = Adafruit_DHT.read_retry(_DHT_SENSOR, _DHT_PIN)
            except: pass
        if BH_OK:
            try: lux = read_bh1750()
            except: pass
        with lock:
            data["temp"] = round(t, 1) if t is not None else None
            data["humi"] = round(h, 1) if h is not None else None
            data["lux"] = lux
            data["ts"] = ts
        time.sleep(3)

threading.Thread(target=sensor_loop, daemon=True).start()

# ===================== HTML =====================
HTML = """<!DOCTYPE html>
<html lang="zh"><head><meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>传感与测量 — 实时监测</title>
<style>
*{margin:0;padding:0;box-sizing:border-box}
body{font-family:'Microsoft YaHei',sans-serif;background:#0d1117;color:#c9d1d9;
min-height:100vh;display:flex;flex-direction:column;align-items:center;padding:15px}
h1{color:#58a6ff;font-size:20px;margin-bottom:10px}
.st{color:#3fb950;font-size:13px;margin-bottom:12px}
.grid{display:grid;grid-template-columns:repeat(3,1fr);gap:10px;max-width:600px;width:100%;margin-bottom:15px}
.c{background:#161b22;border:1px solid #30363d;border-radius:12px;padding:15px;text-align:center}
.c .v{font-size:36px;font-weight:bold;color:#58a6ff}
.c .l{font-size:12px;color:#8b949e;margin-top:4px}
.ft{margin-top:8px;font-size:12px;color:#8b949e}
</style></head><body>
<h1>📡 传感与测量 — 多传感器实时监测</h1>
<div class="st">采集中</div>
<div class="grid">
  <div class="c"><div class="v" id="t">--</div><div class="l">🌡️ 温度 °C</div></div>
  <div class="c"><div class="v" id="h">--</div><div class="l">💧 湿度 %</div></div>
  <div class="c"><div class="v" id="l">--</div><div class="l">☀️ 光照 lux</div></div>
</div>
<div class="ft" id="s">连接中...</div>
<script>
async function poll(){
  try{
    const d=await(await fetch('/api/data')).json();
    document.getElementById('t').textContent=d.temp!=null?d.temp:'--';
    document.getElementById('h').textContent=d.humi!=null?d.humi:'--';
    document.getElementById('l').textContent=d.lux!=null?d.lux:'--';
    document.getElementById('s').textContent='✅ '+d.ts;
  }catch(e){document.getElementById('s').textContent='❌ '+e.message;}
}
setInterval(poll,2000);poll();
</script></body></html>"""

@app.route("/")
def index():
    return render_template_string(HTML)

@app.route("/api/data")
def api_data():
    with lock: d = dict(data)
    return jsonify({"temp": d["temp"], "humi": d["humi"], "lux": d["lux"], "ts": d["ts"]})

if __name__ == "__main__":
    print("=" * 50)
    print("  传感与测量 — 多传感器实时监测")
    print(f"  DHT22:  {'✓' if DHT_OK else '✗'}")
    print(f"  BH1750: {'✓' if BH_OK else '✗'}")
    print("  访问: http://0.0.0.0:5000")
    print("=" * 50)
    app.run(host="0.0.0.0", port=5000, debug=False)
