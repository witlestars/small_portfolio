# -*- coding: utf-8 -*-
"""
IIOT — 车间智能通风与照明大屏 (PC端)
依赖: pip install flask paho-mqtt
用法: python iiot_dashboard.py
     浏览器打开 http://localhost:5000
"""
import json, time, threading
from flask import Flask, jsonify, render_template_string
import paho.mqtt.client as mqtt

app = Flask(__name__)

# ===================== 配置 =====================
MQTT_BROKER = "localhost"
MQTT_PORT = 1883
TOPIC_DATA = "sensor/data"
TOPIC_CMD  = "cmd/relay"

# ===================== 数据存储 =====================
lock = threading.Lock()
latest = {
    "pressure": None, "lux": None, "distance": None,
    "person": False, "fan": False, "status": "disconnected", "ts": ""
}
history = []
MAX_HISTORY = 60

# ===================== MQTT =====================
def on_connect(client, userdata, flags, rc):
    print(f"[MQTT] Connected (rc={rc})")
    client.subscribe(TOPIC_DATA)
    client.subscribe("sensor/event")

def on_message(client, userdata, msg):
    try:
        payload = json.loads(msg.payload.decode())
        with lock:
            latest["ts"] = time.strftime("%H:%M:%S")
            if msg.topic == TOPIC_DATA:
                for k in ["pressure", "lux", "distance", "person", "fan", "status"]:
                    if k in payload:
                        latest[k] = payload[k]
                history.append(dict(latest))
                if len(history) > MAX_HISTORY:
                    history.pop(0)
            elif msg.topic == "sensor/event":
                latest["event"] = payload.get("event", "")
    except Exception as e:
        print(f"[MQTT] parse error: {e}")

mqtt_client = mqtt.Client()
mqtt_client.on_connect = on_connect
mqtt_client.on_message = on_message
try:
    mqtt_client.connect(MQTT_BROKER, MQTT_PORT, 60)
    mqtt_client.loop_start()
    print(f"[MQTT] Broker: {MQTT_BROKER}:{MQTT_PORT}")
except Exception as e:
    print(f"[MQTT] Broker connect fail: {e}")

def mqtt_publish_cmd(cmd):
    try:
        mqtt_client.publish(TOPIC_CMD, cmd)
        return True
    except:
        return False

# ===================== Web 页面 =====================
HTML = r"""<!DOCTYPE html>
<html lang="zh"><head><meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>IIOT 车间智能通风与照明</title>
<script src="https://cdn.jsdelivr.net/npm/echarts@5.6.0/dist/echarts.min.js"></script>
<style>
*{margin:0;padding:0;box-sizing:border-box}
body{font-family:'Microsoft YaHei',sans-serif;background:#0a0e17;color:#e0e6ed;overflow-x:hidden}
.header{background:linear-gradient(180deg,#111827,#0a0e17);padding:16px 30px;border-bottom:2px solid #1e3a5f;display:flex;align-items:center;justify-content:space-between}
.header h1{font-size:22px;color:#4fc3f7;letter-spacing:2px}
.header .status-dot{width:10px;height:10px;border-radius:50%;display:inline-block;margin-right:8px}
.online{background:#00e676;box-shadow:0 0 8px #00e676}
.offline{background:#ff5252;box-shadow:0 0 8px #ff5252}
.main{padding:15px;display:grid;grid-template-columns:1fr 1fr 1fr;grid-template-rows:130px 1fr;gap:12px;max-width:1400px;margin:0 auto;height:calc(100vh - 75px)}
.gauges{grid-column:1/4;display:grid;grid-template-columns:repeat(4,1fr);gap:12px}
.gauge-card{background:#111827;border:1px solid #1e3a5f;border-radius:10px;padding:14px;text-align:center;display:flex;flex-direction:column;justify-content:center}
.gauge-card .value{font-size:36px;font-weight:bold;color:#4fc3f7}
.gauge-card .label{font-size:12px;color:#8899aa;margin-top:4px}
.gauge-card.alert{border-color:#ff5252}.gauge-card.alert .value{color:#ff5252}
.gauge-card.warn{border-color:#ffab00}.gauge-card.warn .value{color:#ffab00}
.gauge-card.ok{border-color:#00e676}
.chart-panel{background:#111827;border:1px solid #1e3a5f;border-radius:10px;padding:10px}
.ctrl-panel{background:#111827;border:1px solid #1e3a5f;border-radius:10px;padding:15px;display:flex;flex-direction:column;gap:15px}
.ctrl-panel h3{color:#4fc3f7;font-size:16px;margin-bottom:5px}
.ctrl-btn{padding:12px;border:none;border-radius:8px;font-size:16px;cursor:pointer;font-weight:bold;transition:all .2s}
.ctrl-btn.on{background:#00e676;color:#000}
.ctrl-btn.off{background:#ff5252;color:#fff}
.ctrl-btn:hover{transform:scale(1.03)}
.ctrl-btn:active{transform:scale(0.97)}
.ctrl-info{font-size:13px;color:#8899aa;line-height:1.6}
.alert-box{background:#1a0a0a;border:1px solid #ff5252;border-radius:8px;padding:10px;color:#ff5252;font-size:13px;min-height:40px}
.alert-box.normal{background:#0a1a0a;border-color:#00e676;color:#00e676}
.fan-viz{width:120px;height:120px;border-radius:50%;border:4px solid #1e3a5f;margin:10px auto;display:flex;align-items:center;justify-content:center;transition:all .5s;font-size:40px}
.fan-viz.running{border-color:#00e676;animation:spin 2s linear infinite;box-shadow:0 0 25px rgba(0,230,118,.3)}
.fan-viz.stopped{border-color:#ff5252}
@keyframes spin{to{transform:rotate(360deg)}}
.person-ind{display:flex;align-items:center;justify-content:center;gap:6px;font-size:14px;color:#8899aa;margin-top:5px}
.person-dot{width:10px;height:10px;border-radius:50%}
.person-dot.here{background:#00e676;box-shadow:0 0 6px #00e676}
.person-dot.gone{background:#484f58}
</style></head><body>
<div class="header">
  <h1>   IIOT 车间智能通风与照明</h1>
  <div><span class="status-dot online" id="conn_dot"></span><span id="conn_text" style="color:#8899aa">等待数据...</span></div>
</div>
<div class="main">
  <div class="gauges">
    <div class="gauge-card" id="g_pres"><div class="value" id="v_pres">--</div><div class="label">气压 hPa</div></div>
    <div class="gauge-card" id="g_lux"><div class="value" id="v_lux">--</div><div class="label">光照 lux</div></div>
    <div class="gauge-card" id="g_dist"><div class="value" id="v_dist">--</div><div class="label">探测距离 cm</div></div>
    <div class="gauge-card" id="g_person"><div class="value" style="font-size:28px" id="v_person">--</div><div class="label">人员状态</div></div>
  </div>
  <div class="chart-panel" id="chart_main" style="grid-column:1/3"></div>
  <div class="ctrl-panel">
    <h3>🌀 排风扇</h3>
    <div class="fan-viz stopped" id="fan_viz">🌀</div>
    <div class="person-ind"><span class="person-dot gone" id="person_dot"></span><span id="person_text">无人</span></div>
    <button class="ctrl-btn on" onclick="sendCmd('ON')">▶ 启动排风扇</button>
    <button class="ctrl-btn off" onclick="sendCmd('OFF')">⬛ 停止</button>
    <div class="alert-box" id="alert_box">⏳ 等待数据...</div>
    <div class="ctrl-info">
      <div>MQTT: sensor/data</div>
      <div>控制: cmd/relay</div>
      <div id="ts_disp" style="margin-top:8px;color:#4fc3f7">--</div>
    </div>
  </div>
</div>
<script>
var chart = echarts.init(document.getElementById('chart_main'));
var option = {
  backgroundColor:'transparent',
  tooltip:{trigger:'axis'},
  legend:{data:['气压hPa','光照lux'],textStyle:{color:'#8899aa'},top:0},
  grid:{left:50,right:50,top:30,bottom:30},
  xAxis:{type:'category',data:[],axisLabel:{color:'#556'},axisLine:{lineStyle:{color:'#1e3a5f'}}},
  yAxis:[
    {type:'value',name:'气压(hPa)',nameTextStyle:{color:'#8899aa'},axisLabel:{color:'#556'},splitLine:{lineStyle:{color:'#1a1f2e'}}},
    {type:'value',name:'光照(lux)',nameTextStyle:{color:'#8899aa'},axisLabel:{color:'#556'},splitLine:{show:false}}
  ],
  series:[
    {name:'气压hPa',type:'line',smooth:true,data:[],lineStyle:{color:'#4fc3f7',width:2},symbol:'none'},
    {name:'光照lux',type:'line',smooth:true,yAxisIndex:1,data:[],lineStyle:{color:'#ffab00',width:2},symbol:'none'}
  ]
};
chart.setOption(option);

var times=[],prs=[],lux=[];

async function poll(){
  try{
    var d = await (await fetch('/api/data')).json();
    document.getElementById('v_pres').textContent = d.pressure!=null ? d.pressure.toFixed(1) : '--';
    document.getElementById('v_lux').textContent = d.lux!=null ? d.lux : '--';
    document.getElementById('v_dist').textContent = d.distance!=null ? d.distance+' cm' : '--';
    document.getElementById('v_person').textContent = d.person ? '有人' : '无人';
    document.getElementById('ts_disp').textContent = '  '+d.ts;

    // 气压卡片状态
    var gp = document.getElementById('g_pres');
    gp.className = 'gauge-card';
    if(d.status=='pressure'||d.status=='alert') gp.className += ' warn';

    // 人员卡片
    document.getElementById('g_person').className = 'gauge-card' + (d.person?' ok':'');

    // 人员指示灯
    var pd = document.getElementById('person_dot');
    pd.className = 'person-dot ' + (d.person?'here':'gone');
    document.getElementById('person_text').textContent = d.person?'有人':'无人';

    // 排风扇
    var fv = document.getElementById('fan_viz');
    fv.className = 'fan-viz ' + (d.fan?'running':'stopped');

    // 状态框
    var ab = document.getElementById('alert_box');
    ab.className = 'alert-box';
    if(d.status=='alert'){ ab.className='alert-box'; ab.innerHTML='⚠️ '+d.event+''; }
    else if(d.status=='pressure'){ ab.className='alert-box'; ab.innerHTML='⚠️ 气压异常 → 排风扇自动启动'; }
    else if(d.status=='normal'){ ab.className='alert-box normal'; ab.innerHTML='✅ 系统正常'; }
    else{ ab.innerHTML='⏳ '+d.status; }

    // 连接
    document.getElementById('conn_dot').className = 'status-dot '+(d.ts?'online':'offline');
    document.getElementById('conn_text').textContent = d.ts?'在线 | '+d.ts:'离线';

    // 图表
    if(d.ts && times[times.length-1]!==d.ts){
      times.push(d.ts); if(times.length>60) times.shift();
      prs.push(d.pressure||0); if(prs.length>60) prs.shift();
      lux.push(d.lux||0); if(lux.length>60) lux.shift();
      chart.setOption({xAxis:{data:times},series:[{data:prs},{data:lux}]});
    }
  }catch(e){ document.getElementById('conn_dot').className='status-dot offline'; }
}
setInterval(poll,1500);poll();

function sendCmd(cmd){
  fetch('/api/cmd/'+cmd).then(r=>r.json()).then(d=>{if(!d.ok)alert('发送失败');});
}
</script></body></html>"""

@app.route("/")
def index():
    return render_template_string(HTML)

@app.route("/api/data")
def api_data():
    with lock:
        return jsonify(latest)

@app.route("/api/cmd/<cmd>")
def api_cmd(cmd):
    ok = mqtt_publish_cmd(cmd.upper())
    return jsonify({"ok": ok, "cmd": cmd.upper()})

if __name__ == "__main__":
    print("\n" + "=" * 50)
    print("  IIOT 车间智能通风与照明大屏")
    print(f"  MQTT Broker: {MQTT_BROKER}:{MQTT_PORT}")
    print(f"  Web: http://localhost:5000")
    print("=" * 50 + "\n")
    app.run(host="0.0.0.0", port=5000, debug=False)
