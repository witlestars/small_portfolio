#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
AI 期末项目 — 基于预训练 MobileNetV2 的实时图像分类
硬件: 树莓派5B + Pi Camera
推理: ONNX MobileNetV2 (ImageNet 1000类, 无需训练)
展示: Flask 网页 — 拍照 → 分类 → 显示 Top-5 结果

首次运行会自动下载模型和标签文件（需联网）
"""

import os
import sys
import time
import urllib.request
import numpy as np
from io import BytesIO
from flask import Flask, jsonify, render_template_string

# ===================== 自动下载模型 & 标签 =====================
LABELS_URL = ("https://storage.googleapis.com/download.tensorflow.org/"
              "data/ImageNetLabels.txt")

MODEL_PATH = "mobilenetv2.onnx"
LABELS_PATH = "imagenet_labels.txt"
def download_labels():
    """下载 ImageNet 标签"""
    if os.path.exists(LABELS_PATH):
        return
    print("正在下载 ImageNet 标签...")
    urllib.request.urlretrieve(LABELS_URL, LABELS_PATH)
    print("标签下载完成:", LABELS_PATH)

def load_labels():
    with open(LABELS_PATH, "r") as f:
        return [line.strip() for line in f.readlines()]

# 初始化
download_labels()
LABELS = load_labels()

# ===================== ONNX 推理 =====================
import onnxruntime as ort

session = ort.InferenceSession(MODEL_PATH, providers=['CPUExecutionProvider'])
input_name = session.get_inputs()[0].name
output_name = session.get_outputs()[0].name
# ONNX MobileNetV2 expects: [1, 3, 224, 224] (NCHW), normalized [0,1]
INPUT_H, INPUT_W = 224, 224

# ===================== Camera =====================
from picamera2 import Picamera2
picam = Picamera2()
picam.configure(picam.create_still_configuration(
    main={"size": (INPUT_W, INPUT_H)}))
picam.start()
time.sleep(0.5)

def classify():
    """拍照并返回 Top-5 分类结果"""
    from PIL import Image

    # 拍照
    picam.capture_file("snapshot.jpg")

    # 预处理: resize -> HWC to CHW -> normalize [0,1] -> expand batch dim
    img = Image.open("snapshot.jpg").resize((INPUT_W, INPUT_H))
    img_array = np.array(img, dtype=np.float32) / 255.0
    img_array = np.transpose(img_array, (2, 0, 1))  # HWC -> CHW
    img_array = np.expand_dims(img_array, axis=0)    # [1, 3, 224, 224]

    # 推理
    t0 = time.time()
    outputs = session.run([output_name], {input_name: img_array})
    dt = (time.time() - t0) * 1000
    output = outputs[0][0]

    # Top-5
    top5_idx = np.argsort(output)[-5:][::-1]
    results = []
    for idx in top5_idx:
        results.append({
            "rank": len(results) + 1,
            "label": LABELS[idx],
            "confidence": round(float(output[idx]) * 100, 1)
        })
    return results, round(dt, 1)

# ===================== Flask =====================
app = Flask(__name__)

HTML = """<!DOCTYPE html>
<html lang="zh">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>AI 图像分类</title>
<style>
  * { margin: 0; padding: 0; box-sizing: border-box; }
  body { font-family: 'Microsoft YaHei', sans-serif;
         background: linear-gradient(135deg, #0d1117, #161b22);
         color: #c9d1d9; min-height: 100vh; display: flex;
         flex-direction: column; align-items: center; padding: 20px; }
  h1 { color: #58a6ff; font-size: 22px; margin-bottom: 15px; }
  .container { display: flex; gap: 20px; flex-wrap: wrap;
               justify-content: center; max-width: 900px; }
  .box { background: #161b22; border: 1px solid #30363d;
         border-radius: 12px; padding: 15px; }
  #snapshot { max-width: 400px; border-radius: 8px;
              border: 1px solid #30363d; }
  .results { min-width: 280px; }
  .results h3 { color: #8b949e; font-size: 14px; margin-bottom: 10px; }
  .row { display: flex; align-items: center; margin: 6px 0; }
  .rank { width: 24px; height: 24px; border-radius: 50%;
          background: #30363d; color: #58a6ff; font-weight: bold;
          display: flex; align-items: center; justify-content: center;
          font-size: 12px; margin-right: 10px; }
  .label { flex: 1; font-size: 14px; }
  .conf { color: #3fb950; font-weight: bold; font-size: 14px; }
  .bar-bg { width: 100%; height: 4px; background: #30363d;
            border-radius: 2px; margin-top: 3px; }
  .bar { height: 100%; background: linear-gradient(90deg, #58a6ff, #3fb950);
         border-radius: 2px; transition: width 0.3s; }
  .time { color: #8b949e; font-size: 12px; margin-top: 15px;
          text-align: center; }
  button { background: #238636; color: #fff; border: none;
           padding: 10px 30px; border-radius: 8px; font-size: 15px;
           cursor: pointer; margin: 15px 0; }
  button:hover { background: #2ea043; }
  .loading { color: #d2991d; }
</style>
</head>
<body>
<h1>🤖 AI 图像分类 — MobileNetV2 (ImageNet 1000类)</h1>
<button onclick="classify()">📸 拍照并分类</button>
<div class="loading" id="status"></div>
<div class="container">
  <div class="box">
    <img id="snapshot" src="/photo" alt="Camera snapshot">
  </div>
  <div class="box results">
    <h3>📊 Top-5 预测结果</h3>
    <div id="results">点击「拍照并分类」开始</div>
    <div class="time" id="inf_time"></div>
  </div>
</div>

<script>
async function classify() {
  document.getElementById('status').textContent = '推理中...';
  const r = await fetch('/api/classify');
  const d = await r.json();
  document.getElementById('status').textContent = '';
  document.getElementById('snapshot').src = '/photo?' + Date.now();
  document.getElementById('inf_time').textContent =
    '推理耗时: ' + d.time_ms + ' ms';
  let html = '';
  for (const p of d.predictions) {
    html +=
      '<div class="row">' +
      '<div class="rank">' + p.rank + '</div>' +
      '<div class="label">' + p.label + '</div>' +
      '<div class="conf">' + p.confidence + '%</div>' +
      '</div>' +
      '<div class="bar-bg"><div class="bar" style="width:' +
      p.confidence + '%"></div></div>';
  }
  document.getElementById('results').innerHTML = html;
}
classify();
</script>
</body>
</html>"""


@app.route("/")
def index():
    return render_template_string(HTML)


@app.route("/api/classify")
def api_classify():
    results, dt = classify()
    return jsonify({"predictions": results, "time_ms": dt})


@app.route("/photo")
def photo():
    path = "snapshot.jpg"
    if os.path.exists(path):
        from flask import send_file
        return send_file(path, mimetype="image/jpeg")
    return "No image", 404


if __name__ == "__main__":
    print("=" * 50)
    print("  AI 图像分类服务启动")
    print("  模型: MobileNetV2 (ImageNet 1000类, ONNX)")
    print("  访问: http://0.0.0.0:5000")
    print("=" * 50)
    app.run(host="0.0.0.0", port=5000, debug=False)
