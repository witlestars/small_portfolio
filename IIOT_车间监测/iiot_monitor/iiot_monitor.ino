/*
 * ============================================================
 *  IIOT 课程设计 — 车间智能通风与照明系统
 *  硬件: ESP32-S3 + BMP280(气压) + BH1750(光照) + HC-SR04(人员探测)
 *       + OLED SSD1306 + 继电器(排风扇通断) + 蜂鸣器 + 130电机
 *  通信: WiFi + MQTT 双向 (Mosquitto Broker)
 *  仪表盘: PC端 Python Flask + ECharts (iiot_dashboard.py)
 * ============================================================
 */

#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <BH1750.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_BMP280.h>

// ===================== 配置区 =====================
const char* WIFI_SSID   = "你的WiFi名";
const char* WIFI_PASS   = "你的WiFi密码";
const char* MQTT_BROKER = "192.168.1.100";  // PC IP
const int   MQTT_PORT   = 1883;
const char* MQTT_CLIENT = "ESP32_Workshop";
// =================================================

// 引脚定义
#define TRIG_PIN     5    // HC-SR04 Trig
#define ECHO_PIN     18   // HC-SR04 Echo
#define RELAY_PIN    26   // 继电器 IN (LOW=触发/断电, HIGH=释放/通电)
#define BUZZER_PIN   27   // 蜂鸣器 +
#define MOTOR_INA    32   // L9110S INA (PWM)
#define MOTOR_INB    33   // L9110S INB

// I2C 地址
#define OLED_ADDR    0x3C
#define BMP280_ADDR  0x76

// 阈值
#define PRESSURE_DELTA_THRESH  3.0   // 气压骤变阈值 (hPa)
#define LIGHT_THRESHOLD        50    // 光照阈值 (lux)
#define DISTANCE_THRESHOLD     50    // 人员探测距离 (cm)
#define PRESSURE_WINDOW_MS     15000 // 气压滑动窗口 (ms)

// 常量
#define SAMPLE_MS      1000
#define OLED_W         128
#define OLED_H         64

// 对象
Adafruit_BMP280 bmp;
BH1750 lightMeter(0x23);
Adafruit_SSD1306 display(OLED_W, OLED_H, &Wire, -1);
WiFiClient espClient;
PubSubClient mqtt(espClient);

// 状态
unsigned long lastSample = 0;
bool relayOn = true;       // true = 电机正常运行
bool fanRunning = false;   // 排风扇是否在转
float basePressure = 1013.0; // 基准气压
unsigned long pressureCalibratedAt = 0;
String systemStatus = "normal";
bool personPresent = false;
unsigned long personLastSeen = 0;

// 气压滑动窗口 (简单版: 记录最近一次异常)
float lastPressure = 0;
unsigned long lastPressureTime = 0;

// ===================== WiFi =====================
void connectWiFi() {
  Serial.print("WiFi...");
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  for (int i = 0; i < 30 && WiFi.status() != WL_CONNECTED; i++) {
    delay(500); Serial.print(".");
  }
  if (WiFi.status() == WL_CONNECTED)
    Serial.println(" OK " + WiFi.localIP().toString());
  else
    Serial.println(" FAIL");
}

// ===================== MQTT =====================
void mqttCallback(char* topic, byte* payload, unsigned int len) {
  String msg = "";
  for (unsigned int i = 0; i < len; i++) msg += (char)payload[i];
  Serial.println("MQTT RX [" + String(topic) + "]: " + msg);

  if (String(topic) == "cmd/relay" || String(topic) == "cmd/fan") {
    if (msg == "ON") {
      relayOn = true;
      digitalWrite(RELAY_PIN, HIGH);  // 释放继电器 = 电机通电
      analogWrite(MOTOR_INA, 180);    // PWM 70%
      digitalWrite(MOTOR_INB, LOW);
      fanRunning = true;
    } else if (msg == "OFF") {
      relayOn = false;
      digitalWrite(RELAY_PIN, LOW);   // 触发继电器 = 断电
      analogWrite(MOTOR_INA, 0);
      digitalWrite(MOTOR_INB, LOW);
      fanRunning = false;
    }
  }
}

void connectMQTT() {
  mqtt.setServer(MQTT_BROKER, MQTT_PORT);
  mqtt.setCallback(mqttCallback);
  while (!mqtt.connected()) {
    Serial.print("MQTT...");
    if (mqtt.connect(MQTT_CLIENT)) {
      Serial.println("OK");
      mqtt.subscribe("cmd/relay");
      mqtt.subscribe("cmd/fan");
    } else {
      Serial.print("fail("); Serial.print(mqtt.state()); Serial.print(") ");
      delay(2000);
    }
  }
}

// ===================== 传感器 =====================
float readHCSR04() {
  digitalWrite(TRIG_PIN, LOW); delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH); delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);
  long duration = pulseIn(ECHO_PIN, HIGH, 30000);
  if (duration == 0) return 400;
  return duration * 0.034 / 2.0;
}

// ===================== OLED =====================
void updateOLED(float p, float l, float d, bool person) {
  display.clearDisplay();
  display.setTextColor(WHITE);

  display.setTextSize(1);
  display.setCursor(0, 0);
  display.printf("P:%.1fhPa L:%d", p, (int)l);

  display.setCursor(0, 12);
  display.printf("D:%.0fcm %s", d, person ? "有人" : "无人");

  display.setCursor(0, 24);
  display.printf("Fan:%s", fanRunning ? "ON " : "OFF");

  display.setTextSize(2);
  display.setCursor(0, 40);
  display.print(systemStatus == "alert" ? "!!ALERT!!" :
                (systemStatus == "pressure" ? "PRESSURE" : "NORMAL"));
  display.display();
}

// ===================== 告警 =====================
void triggerAlert(const char* reason) {
  digitalWrite(BUZZER_PIN, HIGH);
  systemStatus = "alert";
  Serial.printf("\n!!! ALERT: %s !!!\n", reason);
  delay(1500);
  digitalWrite(BUZZER_PIN, LOW);
}

// ===================== SETUP =====================
void setup() {
  Serial.begin(115200);

  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  pinMode(RELAY_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(MOTOR_INA, OUTPUT);
  pinMode(MOTOR_INB, OUTPUT);

  // 初始态: 继电器未触发(COM-NC通) = 电机有电但不转
  digitalWrite(RELAY_PIN, HIGH);
  digitalWrite(BUZZER_PIN, LOW);
  analogWrite(MOTOR_INA, 0);
  digitalWrite(MOTOR_INB, LOW);

  Wire.begin(21, 22);

  // BMP280
  if (bmp.begin(BMP280_ADDR)) {
    bmp.setSampling(Adafruit_BMP280::MODE_FORCED,
                    Adafruit_BMP280::SAMPLING_X1,
                    Adafruit_BMP280::SAMPLING_X1,
                    Adafruit_BMP280::FILTER_OFF);
    basePressure = bmp.readPressure() / 100.0;
    pressureCalibratedAt = millis();
    Serial.printf("[BMP280] OK, base=%.1f hPa\n", basePressure);
  } else {
    Serial.println("[BMP280] FAIL");
  }

  // BH1750
  if (lightMeter.begin(BH1750::CONTINUOUS_HIGH_RES_MODE)) {
    Serial.println("[BH1750] OK");
  } else {
    Serial.println("[BH1750] FAIL");
  }

  // OLED
  if (display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    display.clearDisplay(); display.display();
    Serial.println("[OLED] OK");
  } else {
    Serial.println("[OLED] FAIL");
  }

  connectWiFi();
  connectMQTT();

  Serial.println("\n===== IIOT 车间智能通风与照明 =====");
  Serial.println("MQTT: sensor/data  ←  cmd/relay, cmd/fan");
  Serial.println("====================================\n");
}

// ===================== LOOP =====================
void loop() {
  if (WiFi.status() != WL_CONNECTED) connectWiFi();
  if (!mqtt.connected()) connectMQTT();
  mqtt.loop();

  if (millis() - lastSample >= SAMPLE_MS) {
    lastSample = millis();

    // 采集
    float pressure = bmp.readPressure() / 100.0;  // Pa → hPa
    float lux = lightMeter.readLightLevel();
    float distance = readHCSR04();

    if (isnan(pressure)) pressure = basePressure;
    if (lux < 0) lux = 0;

    // --- 人员探测 ---
    bool personNow = (distance > 0 && distance < DISTANCE_THRESHOLD);
    if (personNow) {
      personLastSeen = millis();
    }
    // 保持"有人"状态至少 3 秒
    if (millis() - personLastSeen < 3000) {
      personPresent = true;
    } else {
      personPresent = false;
    }

    // --- 气压异常检测 ---
    float delta = abs(pressure - basePressure);
    if (delta > PRESSURE_DELTA_THRESH && !fanRunning) {
      // 气压骤变 → 自动开排风扇
      systemStatus = "pressure";
      relayOn = true;
      digitalWrite(RELAY_PIN, HIGH);
      analogWrite(MOTOR_INA, 180);
      digitalWrite(MOTOR_INB, LOW);
      fanRunning = true;
      logEvent("气压异常 " + String(delta) + "hPa → 自动启动排风扇");
      Serial.printf("[PRESSURE] delta=%.1f hPa → fan ON\n", delta);
    } else if (delta <= PRESSURE_DELTA_THRESH && systemStatus == "pressure") {
      // 气压恢复 → 自动关
      systemStatus = "normal";
      analogWrite(MOTOR_INA, 0);
      digitalWrite(MOTOR_INB, LOW);
      fanRunning = false;
      logEvent("气压恢复正常 → 排风扇关闭");
    }

    // --- 照明联动 (HC-SR04 + BH1750) ---
    if (personPresent && lux < LIGHT_THRESHOLD) {
      // 有人 + 光线不足 → 应开灯 (这里用蜂鸣器模拟开灯提示)
      Serial.println("[LIGHT] 有人+光线不足→需开灯");
      if (systemStatus == "normal") {
        // 短鸣一声提示
        digitalWrite(BUZZER_PIN, HIGH);
        delay(200);
        digitalWrite(BUZZER_PIN, LOW);
      }
    }

    // 每 60 秒重新校准气压基准
    if (millis() - pressureCalibratedAt > 60000 && systemStatus == "normal") {
      // 慢速更新基准值（滑动平均）
      basePressure = basePressure * 0.95 + pressure * 0.05;
      pressureCalibratedAt = millis();
    }

    // JSON + MQTT 发布
    StaticJsonDocument<256> doc;
    doc["pressure"] = round(pressure * 10) / 10.0;
    doc["lux"]      = (int)lux;
    doc["distance"] = (int)distance;
    doc["person"]   = personPresent;
    doc["fan"]      = fanRunning;
    doc["status"]   = systemStatus;
    String json;
    serializeJson(doc, json);
    mqtt.publish("sensor/data", json.c_str(), true);

    // OLED
    updateOLED(pressure, lux, distance, personPresent);

    // 串口
    Serial.printf("P:%.1fhPa L:%d D:%dcm Person:%d Fan:%d Status:%s\n",
                  pressure, (int)lux, (int)distance,
                  personPresent, fanRunning, systemStatus.c_str());
  }
}

// ===================== 事件日志 =====================
void logEvent(String msg) {
  StaticJsonDocument<128> ev;
  ev["event"] = msg;
  String json;
  serializeJson(ev, json);
  mqtt.publish("sensor/event", json.c_str(), false);
}
