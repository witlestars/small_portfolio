#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
传感与测量技术 — Flask 实时仪表盘
运行: python3 app.py
访问: http://<树莓派IP>:5000
"""

from flask import Flask, jsonify, render_template_string, send_file
import csv, os, glob, json

app = Flask(__name__)
JSON_PATH = os.path.expanduser("~/传感与测量_多传感器/sensor_data.json")
EVENT_LOG = os.path.expanduser("~/event_log.txt")

HTML = """<!DOCTYPE html>
<html lang="zh">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>传感与测量 - 实时监测仪表盘</title>
<script src="https://cdn.jsdelivr.net/npm/chart.js@4"></script>
<style>
*{box-sizing:border-box}body{font-family:'Microsoft YaHei',sans-serif;
margin:0;padding:10px;background:#0d1117;color:#c9d1d9}
h1{text-align:center;color:#58a6ff;font-size:20px;margin:5px 0 10px}
.grid{display:grid;grid-template-columns:1fr 1fr;gap:10px}
.card{background:#161b22;border-radius:8px;padding:10px;border:1px solid #30363d}
.card h3{margin:0 0 8px 0;color:#8b949e;font-size:13px}
canvas{width:100%!important;height:200px!important}
.live{display:grid;grid-template-columns:repeat(5,1fr);gap:8px;text-align:center;margin-bottom:10px}
.live>div{background:#161b22;border:1px solid #30363d;border-radius:6px;padding:8px}
.live .val{font-size:28px;font-weight:bold;color:#58a6ff}
.live .lbl{font-size:11px;color:#8b949e}
.badge{display:inline-block;padding:3px 10px;border-radius:10px;font-size:13px;font-weight:bold}
.badge-on{background:#da3633;color:#fff}
.badge-off{background:#30363d;color:#8b949e}
#events{max-height:200px;overflow-y:auto;font-size:12px;font-family:Consolas,monospace}
#events .line{padding:3px 6px;border-bottom:1px solid #21262d}
#events .motion{color:#f85149}
#events .info{color:#8b949e}
#photo{width:100%;border-radius:6px;border:1px solid #30363d}
</style>
</head>
<body>
<h1>传感与测量 - 多传感器实时监测</h1>

<div class="live">
  <div><div class="val" id="vt">--</div><div class="lbl">温度 °C</div></div>
  <div><div class="val" id="vh">--</div><div class="lbl">湿度 %</div></div>
  <div><div class="val" id="vl">--</div><div class="lbl">光照 lux</div></div>
  <div><div class="val" id="vd">--</div><div class="lbl">距离 cm</div></div>
  <div><div class="val" id="vp"><span class="badge badge-off" id="pb">无人</span></div><div class="lbl">红外检测</div></div>
</div>

<div class="grid">
  <div class="card"><h3>温湿度</h3><canvas id="c1"></canvas></div>
  <div class="card"><h3>光照度</h3><canvas id="c2"></canvas></div>
  <div class="card"><h3>实时画面 + 事件日志</h3>
    <img id="photo" src="/photo"><hr style="border-color:#30363d">
    <div id="events">等待事件...</div></div>
  <div class="card"><h3>事件抓拍</h3><img id="motion" src="/photo" style="width:100%;border-radius:6px;border:1px solid #30363d"></div>
</div>

<script>
const M=60
function mk(id,ds){return new Chart(document.getElementById(id),{
type:'line',data:{labels:[],datasets:ds},options:{responsive:!0,
maintainAspectRatio:!1,scales:{x:{display:!1}},
plugins:{legend:{labels:{color:'#8b949e',font:{size:10}}}},
animation:{duration:0}}})}
const c1=mk('c1',[{label:'Temp C',data:[],borderColor:'#f85149',tension:.1,pointRadius:0},
{label:'Humi %',data:[],borderColor:'#58a6ff',tension:.1,pointRadius:0}])
const c2=mk('c2',[{label:'Lux',data:[],borderColor:'#d2a8ff',tension:.1,pointRadius:0}])

function ap(ch,ds,v){ch.data.datasets[ds].data.push(v??null)}
function tr(ch){let d=ch.data.datasets[0].data;while(d.length>M){ch.data.labels.shift();ch.data.datasets.forEach(x=>x.data.shift())}}

async function up(){
  let d=await(await fetch('/api/latest')).json()
  document.getElementById('vt').textContent=d.temp??'--'
  document.getElementById('vh').textContent=d.humi??'--'
  document.getElementById('vl').textContent=d.lux??'--'
  document.getElementById('vd').textContent=d.dist??'--'
  let b=document.getElementById('pb')
  if(d.pir){b.className='badge badge-on';b.textContent='有人'}
  else{b.className='badge badge-off';b.textContent='无人'}
  let l=new Date().toLocaleTimeString()
  ap(c1,0,d.temp);ap(c1,1,d.humi);ap(c2,0,d.lux)
  c1.data.labels.push(l);c2.data.labels.push(l)
  tr(c1);tr(c2);c1.update();c2.update()
  document.getElementById('photo').src='/photo?'+Date.now()
  // 事件日志
  let ev=await(await fetch('/api/events')).json()
  let h=''
  for(let e of ev.slice(-15).reverse())
    h+='<div class="line '+ (e.includes('MOTION')?'motion':'info') +'">'+e+'</div>'
  document.getElementById('events').innerHTML=h
  // 最新抓拍
  let mp=await(await fetch('/api/motion_photo')).json()
  if(mp.src) document.getElementById('motion').src=mp.src+'?'+Date.now()
}
setInterval(up,2000);up()
</script>
</body>
</html>"""

@app.route("/")
def index():
    return render_template_string(HTML)

@app.route("/api/latest")
def api_latest():
    if not os.path.exists(JSON_PATH):
        return jsonify({})
    try:
        with open(JSON_PATH) as f:
            d = json.load(f)
        return jsonify({
            "ts":   d.get("timestamp", ""),
            "temp": d.get("temp"),
            "humi": d.get("humi"),
            "lux":  d.get("lux"),
            "dist": d.get("distance"),
            "pir":  d.get("pir", 0),
            "led":  d.get("led", 0),
        })
    except Exception:
        return jsonify({})

@app.route("/api/events")
def api_events():
    if not os.path.exists(EVENT_LOG):
        return jsonify([])
    with open(EVENT_LOG) as f:
        return jsonify([l.strip() for l in f.readlines()])

@app.route("/api/motion_photo")
def api_motion_photo():
    photos = sorted(glob.glob("motion_*.jpg"), reverse=True)
    if photos:
        return jsonify({"src": "/photo/" + photos[0]})
    if os.path.exists("captured_0.jpg"):
        return jsonify({"src": "/photo/captured_0.jpg"})
    return jsonify({"src": None})

@app.route("/photo")
def photo():
    photos = sorted(glob.glob("motion_*.jpg"), reverse=True)
    if not photos:
        photos = sorted(glob.glob("captured_*.jpg"), reverse=True)
    if not photos:
        photos = ["captured.jpg"]
    path = photos[0]
    if os.path.exists(path):
        return send_file(path, mimetype="image/jpeg")
    return "No photo", 404

@app.route("/photo/<name>")
def photo_named(name):
    if os.path.exists(name):
        return send_file(name, mimetype="image/jpeg")
    return "Not found", 404

if __name__ == "__main__":
    print("=" * 50)
    print("Flask 仪表盘: http://0.0.0.0:5000")
    print("=" * 50)
    app.run(host="0.0.0.0", port=5000, debug=False)
