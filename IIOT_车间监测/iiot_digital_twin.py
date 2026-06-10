# -*- coding: utf-8 -*-
"""
IIOT 数字孪生 — 车间虚拟镜像 (Three.js 3D)
ESP32-S3 → MQTT → Python → 浏览器 3D 车间实时同步
依赖: pip install flask paho-mqtt
用法: python iiot_digital_twin.py → http://localhost:5000
"""
import json, time, threading
from flask import Flask, jsonify, render_template_string
import paho.mqtt.client as mqtt

app = Flask(__name__)

MQTT_BROKER = "localhost"
MQTT_PORT = 1883

lock = threading.Lock()
twin = {
    "pressure": 1013.0, "lux": 0, "distance": 400,
    "person": False, "fan": False, "status": "disconnected", "ts": ""
}

def on_connect(client, userdata, flags, rc):
    client.subscribe("sensor/data")

def on_message(client, userdata, msg):
    try:
        payload = json.loads(msg.payload.decode())
        with lock:
            twin["ts"] = time.strftime("%H:%M:%S")
            for k in ["pressure", "lux", "distance", "person", "fan", "status"]:
                if k in payload: twin[k] = payload[k]
    except: pass

mqtt_client = mqtt.Client()
mqtt_client.on_connect = on_connect
mqtt_client.on_message = on_message
try:
    mqtt_client.connect(MQTT_BROKER, MQTT_PORT, 60)
    mqtt_client.loop_start()
except Exception as e:
    print(f"[MQTT] {e}")

def send_cmd(cmd):
    try: mqtt_client.publish("cmd/relay", cmd); return True
    except: return False

HTML = r"""<!DOCTYPE html>
<html lang="zh"><head><meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>IIOT 数字孪生 — 车间虚拟镜像</title>
<style>
*{margin:0;padding:0;box-sizing:border-box}
body{overflow:hidden;font-family:'Microsoft YaHei',sans-serif}
#info{position:absolute;top:12px;left:50%;transform:translateX(-50%);z-index:10;
background:rgba(17,24,39,0.92);border:1px solid #1e3a5f;border-radius:8px;
padding:8px 20px;display:flex;gap:25px;color:#8899aa;font-size:13px}
#info span{color:#4fc3f7;font-weight:bold}
#panel{position:absolute;right:15px;top:50%;transform:translateY(-50%);z-index:10;
background:rgba(17,24,39,0.92);border:1px solid #1e3a5f;border-radius:10px;
padding:15px;display:flex;flex-direction:column;gap:10px;min-width:130px}
#panel button{padding:10px 16px;border:none;border-radius:6px;font-size:14px;
font-weight:bold;cursor:pointer;margin:3px 0}
#panel .btn-on{background:#00e676;color:#000}
#panel .btn-off{background:#ff5252;color:#fff}
#status{position:absolute;bottom:15px;left:50%;transform:translateX(-50%);z-index:10;
background:rgba(17,24,39,0.92);border-radius:8px;padding:6px 16px;font-size:13px}
#status.normal{border:1px solid #00e676;color:#00e676}
#status.pressure{border:1px solid #ffab00;color:#ffab00}
#status.alert{border:1px solid #ff5252;color:#ff5252}
#tip{position:absolute;bottom:50px;left:50%;transform:translateX(-50%);
color:#556;font-size:12px}
</style></head><body>
<div id="info">
  气压:<span id="v_pres">--</span>hPa
  光照:<span id="v_lux">--</span>lux
  距离:<span id="v_dist">--</span>cm
  人员:<span id="v_person">--</span>
  排风扇:<span id="v_fan">--</span>
</div>
<div id="panel">
  <button class="btn-on" onclick="ctl('ON')">▶ 启动排风扇</button>
  <button class="btn-off" onclick="ctl('OFF')">⬛ 停止</button>
</div>
<div id="status" class="normal">等待数据...</div>
<div id="tip">🖱️ 拖拽旋转 | 滚轮缩放 | 右键平移</div>

<script type="importmap">
{"imports":{"three":"https://unpkg.com/three@0.160/build/three.module.js",
"OrbitControls":"https://unpkg.com/three@0.160/examples/jsm/controls/OrbitControls.js"}}
</script>
<script type="module">
import * as THREE from 'three';
import {OrbitControls} from 'OrbitControls';

// ========== 场景 ==========
const scene = new THREE.Scene();
scene.background = new THREE.Color(0x0a0e17);
scene.fog = new THREE.Fog(0x0a0e17, 8, 30);

const cam = new THREE.PerspectiveCamera(50, innerWidth/innerHeight, 0.5, 50);
cam.position.set(5, 3.5, 7);
cam.lookAt(0, 1, 0);

const renderer = new THREE.WebGLRenderer({antialias:true});
renderer.setSize(innerWidth, innerHeight);
renderer.shadowMap.enabled = true;
renderer.shadowMap.type = THREE.PCFSoftShadowMap;
renderer.toneMapping = THREE.ACESFilmicToneMapping;
document.body.appendChild(renderer.domElement);

new OrbitControls(cam, renderer.domElement).enableDamping = true;

// 灯光
const amb = new THREE.AmbientLight(0x334466, 2.5);
scene.add(amb);
const sun = new THREE.DirectionalLight(0x8899cc, 3);
sun.position.set(6, 10, 4);
sun.castShadow = true;
sun.shadow.mapSize.set(1024, 1024);
sun.shadow.camera.near = 0.5; sun.shadow.camera.far = 40;
sun.shadow.camera.left = -10; sun.shadow.camera.right = 10;
sun.shadow.camera.top = 10; sun.shadow.camera.bottom = -10;
scene.add(sun);

// 地面
const ground = new THREE.Mesh(
  new THREE.PlaneGeometry(12, 12),
  new THREE.MeshStandardMaterial({color:0x1a1a2e, roughness:0.8})
);
ground.rotation.x = -Math.PI/2; ground.receiveShadow = true;
scene.add(ground);

// 网格线
const grid = new THREE.GridHelper(12, 24, 0x1e3a5f, 0x111827);
scene.add(grid);

// ========== 车间建筑 ==========
const wallMat = new THREE.MeshStandardMaterial({color:0x1e2a3a, roughness:0.6, metalness:0.2});
const roofMat = new THREE.MeshStandardMaterial({color:0x263040, roughness:0.5, metalness:0.3});

// 四面墙 (half-open, 能看到里面)
function addWall(w, h, d, x, y, z, ry) {
  const g = new THREE.BoxGeometry(w, h, d);
  const m = new THREE.Mesh(g, wallMat);
  m.position.set(x, y, z);
  m.rotation.y = ry;
  m.castShadow = true; m.receiveShadow = true;
  scene.add(m);
}
addWall(5, 3, 0.2, 0, 1.5, -2.5, 0);    // 后墙
addWall(5, 3, 0.2, 0, 1.5, 2.5, 0);     // 前墙
addWall(5, 3, 0.2, -2.5, 1.5, 0, Math.PI/2); // 左墙
addWall(5, 3, 0.2, 2.5, 1.5, 0, Math.PI/2);  // 右墙

// 屋顶框架
const roofFrame = new THREE.Mesh(
  new THREE.BoxGeometry(5.2, 0.08, 5.2),
  roofMat
);
roofFrame.position.y = 3; roofFrame.castShadow = true;
scene.add(roofFrame);

// 地面标识线 (十字)
const lineMat = new THREE.MeshStandardMaterial({color:0x334455, roughness:0.5, emissive:0x111822});
const lx = new THREE.Mesh(new THREE.BoxGeometry(4, 0.02, 0.3), lineMat);
lx.position.y = 0.01; scene.add(lx);
const lz = new THREE.Mesh(new THREE.BoxGeometry(0.3, 0.02, 4), lineMat);
lz.position.y = 0.01; scene.add(lz);

// ========== 排风扇 (天花板) ==========
const fanGroup = new THREE.Group();
fanGroup.position.set(0, 2.85, 0);
scene.add(fanGroup);

// 电机外壳
const motorHousing = new THREE.Mesh(
  new THREE.CylinderGeometry(0.15, 0.15, 0.25, 16),
  new THREE.MeshStandardMaterial({color:0x556677, roughness:0.3, metalness:0.8})
);
motorHousing.position.y = 0.15; motorHousing.castShadow = true;
fanGroup.add(motorHousing);

// 叶片组
const bladeGroup = new THREE.Group();
fanGroup.add(bladeGroup);
const bladeMat = new THREE.MeshStandardMaterial({color:0x8899aa, roughness:0.4, metalness:0.6});
for (let i = 0; i < 4; i++) {
  const blade = new THREE.Mesh(new THREE.BoxGeometry(0.06, 0.8, 0.18), bladeMat);
  blade.position.y = 0.4;
  blade.rotation.y = (i * Math.PI) / 2;
  blade.castShadow = true;
  bladeGroup.add(blade);
}

// 风扇旋转标签
let fanSpeed = 0;
const fanOn = new THREE.Mesh(
  new THREE.CylinderGeometry(0.05, 0.05, 0.02, 8),
  new THREE.MeshStandardMaterial({color:0x00e676, emissive:0x004422, emissiveIntensity:2})
);
fanOn.position.set(0, 0.1, 0);
fanGroup.add(fanOn);

// ========== 照明灯 (天花板) ==========
const lightBulb = new THREE.Mesh(
  new THREE.SphereGeometry(0.12, 16, 16),
  new THREE.MeshStandardMaterial({color:0xffcc88, roughness:0.1, emissive:0x332200, emissiveIntensity:1})
);
lightBulb.position.set(1.2, 2.7, 0.8);
scene.add(lightBulb);

const lightFixture = new THREE.Mesh(
  new THREE.CylinderGeometry(0.08, 0.2, 0.3, 8),
  new THREE.MeshStandardMaterial({color:0x445566, roughness:0.3, metalness:0.7})
);
lightFixture.position.set(1.2, 2.55, 0.8);
lightFixture.castShadow = true;
scene.add(lightFixture);

// ========== 气压面板 (墙上) ==========
const gaugeGroup = new THREE.Group();
gaugeGroup.position.set(2.4, 1.5, 0);
gaugeGroup.rotation.y = -Math.PI / 2;
scene.add(gaugeGroup);

const gaugeBack = new THREE.Mesh(
  new THREE.BoxGeometry(0.08, 0.6, 0.4),
  new THREE.MeshStandardMaterial({color:0x111827, roughness:0.3})
);
gaugeGroup.add(gaugeBack);

const gaugeScreen = new THREE.Mesh(
  new THREE.PlaneGeometry(0.35, 0.5),
  new THREE.MeshStandardMaterial({color:0x003344, roughness:0.1, emissive:0x002233, emissiveIntensity:0.8})
);
gaugeScreen.position.z = 0.045;
gaugeGroup.add(gaugeScreen);

// ========== 人员模型 (出现/消失) ==========
const personGroup = new THREE.Group();
personGroup.position.set(0, 0, 1.5);
personGroup.visible = false;
scene.add(personGroup);

// 身体
const body = new THREE.Mesh(
  new THREE.CylinderGeometry(0.2, 0.25, 1, 8),
  new THREE.MeshStandardMaterial({color:0x4488cc, roughness:0.5})
);
body.position.y = 0.7; body.castShadow = true;
personGroup.add(body);

// 头
const head = new THREE.Mesh(
  new THREE.SphereGeometry(0.18, 16, 16),
  new THREE.MeshStandardMaterial({color:0xffcc99, roughness:0.4})
);
head.position.y = 1.35; head.castShadow = true;
personGroup.add(head);

// 粒子光环 (表示检测范围)
const ringGeo = new THREE.TorusGeometry(0.8, 0.03, 8, 32);
const ringMat = new THREE.MeshStandardMaterial({color:0x00e676, emissive:0x004422, emissiveIntensity:1.5, roughness:0.2});
const ring = new THREE.Mesh(ringGeo, ringMat);
ring.rotation.x = Math.PI / 2;
ring.position.y = 0.05;
personGroup.add(ring);

// ========== 粒子效果 ==========
const particlesGeo = new THREE.BufferGeometry();
const particleCount = 60;
const posArr = new Float32Array(particleCount * 3);
for (let i = 0; i < particleCount * 3; i += 3) {
  posArr[i] = (Math.random() - 0.5) * 5;
  posArr[i + 1] = Math.random() * 3;
  posArr[i + 2] = (Math.random() - 0.5) * 5;
}
particlesGeo.setAttribute('position', new THREE.BufferAttribute(posArr, 3));
const particles = new THREE.Points(
  particlesGeo,
  new THREE.PointsMaterial({color:0x334466, size:0.04, blending:THREE.AdditiveBlending, depthWrite:false})
);
scene.add(particles);

// ========== 状态文字标签 ==========
const canvas = document.createElement('canvas');
canvas.width = 256; canvas.height = 64;
const ctx = canvas.getContext('2d');
const tex = new THREE.CanvasTexture(canvas);
const labelPlane = new THREE.Mesh(
  new THREE.PlaneGeometry(2, 0.5),
  new THREE.MeshBasicMaterial({map: tex, transparent: true, depthTest: false})
);
labelPlane.position.set(0, 0.15, -2.35);
scene.add(labelPlane);

function updateLabel(text, color) {
  ctx.clearRect(0, 0, 256, 64);
  ctx.fillStyle = color; ctx.font = 'bold 28px Microsoft YaHei';
  ctx.textAlign = 'center'; ctx.fillText(text, 128, 40);
  tex.needsUpdate = true;
}
updateLabel('车间正常', '#00e676');

// ========== 动画循环 ==========
function animate() {
  requestAnimationFrame(animate);
  fanGroup.rotation.y += fanSpeed;
  particles.rotation.y += 0.0003;
  ring.rotation.z += 0.02;
  renderer.render(scene, cam);
}
animate();

// ========== 数据同步 ==========
async function poll() {
  try {
    const d = await (await fetch('/api/data')).json();

    // 顶部信息栏
    document.getElementById('v_pres').textContent = d.pressure?.toFixed(1) ?? '--';
    document.getElementById('v_lux').textContent = d.lux ?? '--';
    document.getElementById('v_dist').textContent = d.distance ?? '--';
    document.getElementById('v_person').textContent = d.person ? '有人' : '无人';
    document.getElementById('v_fan').textContent = d.fan ? '运行中' : '停止';

    // 排风扇
    fanSpeed = d.fan ? 0.08 : 0;
    fanOn.material.emissive.set(d.fan ? 0x004422 : 0x000000);
    fanOn.material.emissiveIntensity = d.fan ? 2 : 0.5;

    // 灯光
    const lv = (d.lux || 0) / 500;
    lightBulb.material.emissive.set(new THREE.Color().setHSL(0.12, 0.8, lv));
    lightBulb.material.emissiveIntensity = 0.5 + lv * 3;
    lightBulb.material.color.set(new THREE.Color().setHSL(0.12, 0.6, 0.3 + lv * 0.7));

    // 气压面板
    const pd = d.pressure ? (d.pressure - 1000) / 50 : 0;
    gaugeScreen.material.emissive.set(new THREE.Color().setHSL(0.55 - pd * 0.5, 0.8, 0.15 + Math.abs(pd) * 0.3));
    gaugeScreen.material.emissiveIntensity = 0.5 + Math.abs(pd) * 2;

    // 人员
    personGroup.visible = d.person;
    ring.material.color.set(d.person ? 0x00e676 : 0x334455);
    ring.material.emissive.set(d.person ? 0x004422 : 0x000000);

    // 状态标签
    const stEl = document.getElementById('status');
    if (d.status === 'pressure') {
      stEl.className = 'pressure'; stEl.textContent = '⚠️ 气压异常 — 排风扇已自动启动';
      updateLabel('气压异常', '#ffab00');
    } else if (d.status === 'alert') {
      stEl.className = 'alert'; stEl.textContent = '!! ALERT !!';
      updateLabel('!! ALERT !!', '#ff5252');
    } else {
      stEl.className = 'normal'; stEl.textContent = '✅ ' + d.ts + ' — 系统正常';
      updateLabel('车间正常', '#00e676');
    }
  } catch (e) {}
}
setInterval(poll, 1500); poll();

window.ctl = async function(cmd) {
  await fetch('/api/cmd/' + cmd);
};

window.addEventListener('resize', () => {
  cam.aspect = innerWidth / innerHeight;
  cam.updateProjectionMatrix();
  renderer.setSize(innerWidth, innerHeight);
});
</script></body></html>"""

@app.route("/")
def index():
    return render_template_string(HTML)

@app.route("/api/data")
def api_data():
    with lock:
        return jsonify(twin)

@app.route("/api/cmd/<cmd>")
def api_cmd(cmd):
    return jsonify({"ok": send_cmd(cmd.upper())})

if __name__ == "__main__":
    print("\n" + "=" * 55)
    print("  IIOT 数字孪生 — 车间虚拟镜像")
    print("  ESP32 MQTT → Three.js 3D 实时同步")
    print(f"  http://localhost:5000")
    print("=" * 55 + "\n")
    app.run(host="0.0.0.0", port=5000, debug=False)
