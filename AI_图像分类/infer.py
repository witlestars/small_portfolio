#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
AI 期末项目 — 基于预训练 MobileNetV2 的实时图像分类
硬件: 树莓派5B + Pi Camera
推理: ONNX MobileNetV2 (ImageNet 1000类, 无需训练)
展示: Flask 网页 — 持续识别，实时显示 Top-5 结果
"""

import os
import sys
import time
import json
import threading
import urllib.request
import numpy as np
from flask import Flask, jsonify, render_template_string, send_file

# ===================== 模型 & 标签 =====================
LABELS_URL = ("https://storage.googleapis.com/download.tensorflow.org/"
              "data/ImageNetLabels.txt")
MODEL_PATH = "mobilenetv2.onnx"
LABELS_PATH = "imagenet_labels.txt"

def download_labels():
    if os.path.exists(LABELS_PATH):
        return
    print("正在下载 ImageNet 标签...")
    urllib.request.urlretrieve(LABELS_URL, LABELS_PATH)

def load_labels():
    with open(LABELS_PATH, "r") as f:
        return [line.strip() for line in f.readlines()]

download_labels()
LABELS = load_labels()

# ===================== ONNX 推理 =====================
import onnxruntime as ort

session = ort.InferenceSession(MODEL_PATH, providers=['CPUExecutionProvider'])
input_name = session.get_inputs()[0].name
output_name = session.get_outputs()[0].name
INPUT_H, INPUT_W = 224, 224

# ===================== Camera =====================
from picamera2 import Picamera2
picam = Picamera2()
picam.configure(picam.create_still_configuration(
    main={"size": (INPUT_W, INPUT_H)}))
picam.start()
time.sleep(0.5)

# ===================== 持续推理线程 =====================
from PIL import Image

lock = threading.Lock()
current_result = {
    "predictions": [],
    "time_ms": 0,
    "frame_time": ""
}

def classify():
    """拍照 + 预处理 + 推理，返回 Top-5"""
    picam.capture_file("snapshot.jpg")
    img = Image.open("snapshot.jpg").resize((INPUT_W, INPUT_H))
    img_array = np.array(img, dtype=np.float32) / 255.0
    img_array = np.transpose(img_array, (2, 0, 1))
    img_array = np.expand_dims(img_array, axis=0)

    t0 = time.time()
    outputs = session.run([output_name], {input_name: img_array})
    dt = (time.time() - t0) * 1000
    output = outputs[0][0]

    # Softmax
    exp_out = np.exp(output - np.max(output))
    probs = exp_out / exp_out.sum()

    top5_idx = np.argsort(probs)[-5:][::-1]
    results = []
    for idx in top5_idx:
        label_idx = idx + 1 if len(LABELS) == 1001 else idx
        results.append({
            "rank": len(results) + 1,
            "label": LABELS[label_idx] if label_idx < len(LABELS) else f"class_{idx}",
            "confidence": round(float(probs[idx]) * 100, 1)
        })
    return results, round(dt, 1)

def inference_loop():
    """后台持续推理"""
    global current_result
    while True:
        try:
            results, dt = classify()
            ts = time.strftime("%H:%M:%S")
            with lock:
                current_result = {
                    "predictions": results,
                    "time_ms": dt,
                    "frame_time": ts
                }
        except Exception as e:
            print(f"推理错误: {e}")
            time.sleep(1)

# 启动后台推理线程
t = threading.Thread(target=inference_loop, daemon=True)
t.start()

# ===================== Flask =====================
app = Flask(__name__)

HTML = """<!DOCTYPE html>
<html lang="zh">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>AI 实时图像分类</title>
<style>
*{margin:0;padding:0;box-sizing:border-box}
body{font-family:'Microsoft YaHei',sans-serif;
background:linear-gradient(135deg,#0d1117,#161b22);color:#c9d1d9;
min-height:100vh;display:flex;flex-direction:column;align-items:center;padding:15px}
h1{color:#58a6ff;font-size:20px;margin-bottom:10px}
.status{display:flex;gap:15px;margin-bottom:12px;font-size:13px;color:#8b949e}
.status .live{color:#3fb950;font-weight:bold}
.status .live::before{content:"";display:inline-block;width:8px;height:8px;
background:#3fb950;border-radius:50%;margin-right:5px;animation:blink 1s infinite}
@keyframes blink{50%{opacity:.3}}
.container{display:flex;gap:15px;flex-wrap:wrap;justify-content:center;max-width:900px}
.box{background:#161b22;border:1px solid #30363d;border-radius:12px;padding:12px}
#snapshot{max-width:400px;border-radius:8px;border:1px solid #30363d}
.results{min-width:280px}
.results h3{color:#8b949e;font-size:13px;margin-bottom:8px}
.row{display:flex;align-items:center;margin:5px 0}
.rank{width:22px;height:22px;border-radius:50%;background:#30363d;
color:#58a6ff;font-weight:bold;display:flex;align-items:center;
justify-content:center;font-size:11px;margin-right:8px;flex-shrink:0}
.label{flex:1;font-size:13px}
.conf{color:#3fb950;font-weight:bold;font-size:13px;margin-left:8px}
.bar-bg{width:100%;height:4px;background:#30363d;border-radius:2px;margin-top:2px}
.bar{height:100%;background:linear-gradient(90deg,#58a6ff,#3fb950);
border-radius:2px;transition:width .3s}
.time{color:#8b949e;font-size:11px;margin-top:10px;text-align:center}
#1label{font-size:28px;font-weight:bold;color:#58a6ff;text-align:center;
margin:8px 0;min-height:36px}
#1conf{font-size:18px;color:#3fb950;text-align:center;margin-bottom:8px}
</style>
</head>
<body>
<h1>AI 实时图像分类 — MobileNetV2</h1>
<div class="status">
  <span class="live">持续识别中</span>
  <span id="fps">--</span>
  <span id="clock">--</span>
</div>
<div class="container">
  <div class="box">
    <img id="snapshot" src="/photo" alt="Camera">
  </div>
  <div class="box results">
    <div id="1label">--</div>
    <div id="1conf">--</div>
    <h3>Top-5 预测</h3>
    <div id="results">等待推理...</div>
    <div class="time" id="inf_time"></div>
  </div>
</div>

<script>
let lastFrame = '';
async function poll() {
  try {
    const r = await fetch('/api/latest');
    const d = await r.json();
    if (d.frame_time && d.frame_time !== lastFrame) {
      lastFrame = d.frame_time;
      document.getElementById('snapshot').src = '/photo?' + Date.now();
      // 第一名大字显示
      if (d.predictions.length > 0) {
        document.getElementById('1label').textContent = d.predictions[0].label;
        document.getElementById('1conf').textContent = d.predictions[0].confidence + '%';
      }
      // Top-5 列表
      let html = '';
      for (const p of d.predictions) {
        html += '<div class="row">' +
          '<div class="rank">' + p.rank + '</div>' +
          '<div class="label">' + p.label + '</div>' +
          '<div class="conf">' + p.confidence + '%</div></div>' +
          '<div class="bar-bg"><div class="bar" style="width:' +
          Math.min(p.confidence, 100) + '%"></div></div>';
      }
      document.getElementById('results').innerHTML = html;
      document.getElementById('inf_time').textContent = '推理: ' + d.time_ms + 'ms';
    }
    document.getElementById('clock').textContent = new Date().toLocaleTimeString();
  } catch(e) {}
}
setInterval(poll, 800);
poll();
</script>
</body>
</html>"""

@app.route("/")
def index():
    return render_template_string(HTML)

@app.route("/api/latest")
def api_latest():
    with lock:
        return jsonify(current_result)

@app.route("/photo")
def photo():
    if os.path.exists("snapshot.jpg"):
        return send_file("snapshot.jpg", mimetype="image/jpeg")
    return "No image", 404

# ===================== 传感器 (DHT22 + BH1750) =====================
DHT_OK = False
BH_OK = False

# --- DHT22 (GPIO, 需要 sudo) ---
try:
    import adafruit_dht, board
    _dht = adafruit_dht.DHT22(board.D4, use_pulseio=False)
    _t = _dht.temperature
    DHT_OK = True
    print(f"[DHT22] OK  {_t}°C")
except Exception as e:
    print(f"[DHT22] SKIP: {e}")

# --- BH1750 (I2C) ---
try:
    import smbus2
    _bus = smbus2.SMBus(1)
    _BH_ADDR = 0x23
    def read_bh1750():
        d = _bus.read_i2c_block_data(_BH_ADDR, 0x20, 2)
        return round(int((d[0] << 8) | d[1]) / 1.2, 1)
    _ = read_bh1750()
    BH_OK = True
    print(f"[BH1750] OK")
except Exception as e:
    print(f"[BH1750] SKIP: {e}")

sensor_lock = threading.Lock()
sensor_data = {"temp": None, "humi": None, "lux": None, "ts": ""}

def sensor_loop():
    while True:
        ts = time.strftime("%H:%M:%S")
        t, h, lux = None, None, None
        if DHT_OK:
            try:
                t = _dht.temperature
                h = _dht.humidity
            except: pass
        if BH_OK:
            try: lux = read_bh1750()
            except: pass
        with sensor_lock:
            sensor_data["temp"] = round(t, 1) if t is not None else None
            sensor_data["humi"] = round(h, 1) if h is not None else None
            sensor_data["lux"] = lux
            sensor_data["ts"] = ts
        time.sleep(3)

threading.Thread(target=sensor_loop, daemon=True).start()

SENSOR_HTML = """<!DOCTYPE html>
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
.cam{max-width:600px;width:100%;text-align:center}
.cam img{width:100%;border-radius:8px;border:1px solid #30363d;background:#000}
.ft{margin-top:8px;font-size:12px;color:#8b949e}
</style></head><body>
<h1>📡 传感与测量 — 多传感器实时监测</h1>
<div class="st">采集中</div>
<div class="grid">
  <div class="c"><div class="v" id="t">--</div><div class="l">🌡️ 温度 °C</div></div>
  <div class="c"><div class="v" id="h">--</div><div class="l">💧 湿度 %</div></div>
  <div class="c"><div class="v" id="l">--</div><div class="l">☀️ 光照 lux</div></div>
</div>
<div class="cam"><img id="p" src="/photo" alt="cam"></div>
<div class="ft" id="s">连接中...</div>
<script>
async function poll(){
  try{
    const d=await(await fetch('/api/data')).json();
    document.getElementById('t').textContent=d.temp!=null?d.temp:'--';
    document.getElementById('h').textContent=d.humi!=null?d.humi:'--';
    document.getElementById('l').textContent=d.lux!=null?d.lux:'--';
    document.getElementById('p').src='/photo?'+Date.now();
    document.getElementById('s').textContent='✅ '+d.ts;
  }catch(e){document.getElementById('s').textContent='❌ '+e.message;}
}
setInterval(poll,2000);poll();
</script></body></html>"""

@app.route("/sensor")
def sensor_page():
    return render_template_string(SENSOR_HTML)

@app.route("/api/data")
def api_data():
    with sensor_lock: d = dict(sensor_data)
    return jsonify({"temp":d["temp"],"humi":d["humi"],"lux":d["lux"],"ts":d["ts"]})

if __name__ == "__main__":
    print("=" * 50)
    print("  AI + 传感器 综合监测")
    print(f"  AI: MobileNetV2 (ONNX)")
    print(f"  DHT22:  {'✓' if DHT_OK else '✗'}")
    print(f"  BH1750: {'✓' if BH_OK else '✗'}")
    print("  AI页面: http://0.0.0.0:5000")
    print("  传感器: http://0.0.0.0:5000/sensor")
    print("=" * 50)
    app.run(host="0.0.0.0", port=5000, debug=False)
