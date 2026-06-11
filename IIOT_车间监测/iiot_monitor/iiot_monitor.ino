/*
 * ============================================================
 *  IIOT 课程设计 — 车间智能通风与照明系统
 *  硬件: ESP32-S3 + BMP280(气压) + BH1750(光照) + HC-SR04(人员探测)
 *       + OLED SSD1306 + LED + 130电机(排风扇, TB6612直驱)
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
const char* WIFI_SSID   = "Pura 70 Pro+";
const char* WIFI_PASS   = "0866031168";
const char* MQTT_BROKER = "192.168.43.8";  // PC IP
const int   MQTT_PORT   = 1883;
const char* MQTT_CLIENT = "ESP32_Workshop";
// =================================================

// 引脚定义 (DNESP32S3M 小系统板)
#define TRIG_PIN     4    // HC-SR04 Trig
#define ECHO_PIN     5    // HC-SR04 Echo
#define LED_PIN      6    // LED 指示灯
#define MOTOR_PWM    7    // TB6612 PWMA (PWM调速)
#define MOTOR_STBY   15   // TB6612 STBY (HIGH=运行)

// I2C 地址
#define OLED_ADDR    0x3C
#define BMP280_ADDR  0x76

// 阈值
#define PRESSURE_DELTA_THRESH  3.0   // 气压骤变阈值 (hPa)
#define LIGHT_THRESHOLD        50    // 光照阈值 (lux)
#define DISTANCE_THRESHOLD     4     // 人员探测距离 (cm)

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
bool fanRunning = false;
float basePressure = 1013.0;
unsigned long pressureCalibratedAt = 0;
String systemStatus = "normal";
bool personPresent = false;
unsigned long personLastSeen = 0;

// ===================== 电机控制 =====================
void motorOn() {
  digitalWrite(MOTOR_STBY, HIGH);
  analogWrite(MOTOR_PWM, 180);  // PWM ~70%
  fanRunning = true;
}

void motorOff() {
  digitalWrite(MOTOR_STBY, LOW);
  analogWrite(MOTOR_PWM, 0);
  fanRunning = false;
}

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
    if (msg == "ON")  motorOn();
    if (msg == "OFF") motorOff();
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
                (systemStatus == "pressure" ? "PRESSURE" :
                (systemStatus == "person" ? "PERSON" : "NORMAL")));
  display.display();
}

// ===================== 告警 =====================
void triggerAlert(const char* reason) {
  systemStatus = "alert";
  Serial.printf("\n!!! ALERT: %s !!!\n", reason);
  for (int i = 0; i < 6; i++) {  // LED 快闪3次
    digitalWrite(LED_PIN, !digitalRead(LED_PIN));
    delay(250);
  }
  digitalWrite(LED_PIN, LOW);
}

// ===================== SETUP =====================
void setup() {
  Serial.begin(115200);

  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  pinMode(LED_PIN, OUTPUT);
  pinMode(MOTOR_PWM, OUTPUT);
  pinMode(MOTOR_STBY, OUTPUT);

  digitalWrite(LED_PIN, LOW);
  motorOff();

  Wire.begin(38, 37);  // DNESP32S3M: SDA=38, SCL=37

  // BMP280
  if (bmp.begin(BMP280_ADDR)) {
    bmp.setSampling(Adafruit_BMP280::MODE_NORMAL,
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
    float pressure = bmp.readPressure() / 100.0;
    float lux = lightMeter.readLightLevel();
    float distance = readHCSR04();

    if (isnan(pressure)) pressure = basePressure;
    if (isnan(lux) || lux < 0) lux = 0;

    // --- 人员探测 ---
    bool personNow = (distance > 0 && distance < DISTANCE_THRESHOLD);
    if (personNow) personLastSeen = millis();
    personPresent = (millis() - personLastSeen < 3000);

    // --- 气压异常检测 ---
    float delta = abs(pressure - basePressure);
    if (delta > PRESSURE_DELTA_THRESH && !fanRunning) {
      systemStatus = "pressure";
      motorOn();
      Serial.printf("[PRESSURE] delta=%.1f hPa → fan ON\n", delta);
    }

    // --- 人员靠近开风扇 ---
    if (personPresent && !fanRunning && systemStatus == "normal") {
      systemStatus = "person";
      motorOn();
      Serial.println("[PERSON] 有人靠近 → fan ON");
    }

    // --- 关风扇: 气压正常 且 人已离开 ---
    bool pressureNormal = (delta <= PRESSURE_DELTA_THRESH);
    if (fanRunning && pressureNormal && !personPresent) {
      systemStatus = "normal";
      motorOff();
      Serial.println("[AUTO] 恢复正常 → fan OFF");
    }

    // --- 照明联动 ---
    if (personPresent && lux < LIGHT_THRESHOLD && systemStatus == "normal") {
      digitalWrite(LED_PIN, HIGH);
      Serial.println("[LIGHT] 有人+光线不足→LED亮");
    } else if (!(personPresent && lux < LIGHT_THRESHOLD)) {
      digitalWrite(LED_PIN, LOW);
    }

    // 慢速更新气压基准
    if (millis() - pressureCalibratedAt > 60000 && systemStatus == "normal") {
      basePressure = basePressure * 0.95 + pressure * 0.05;
      pressureCalibratedAt = millis();
    }

    // JSON + MQTT
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

    updateOLED(pressure, lux, distance, personPresent);
    Serial.printf("P:%.1f L:%d D:%d Person:%d Fan:%d %s\n",
                  pressure, (int)lux, (int)distance,
                  personPresent, fanRunning, systemStatus.c_str());
  }
}
