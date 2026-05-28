/*
 * ============================================================
 *  IIOT 课程设计 — 基于MQTT的车间环境与设备状态监测系统
 *  硬件: ESP32-S3 + DHT22 + BH1750 + ACS712 + HC-SR04
 *       + OLED SSD1306 + 2路继电器 + 蜂鸣器 + 130直流电机
 *  通信: WiFi + MQTT 双向 (Mosquitto Broker)
 *  仪表盘: PC端 Node-RED (http://localhost:1880/ui)
 * ============================================================
 */

#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <DHT.h>
#include <BH1750.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ===================== 配置区 (按实际情况修改) =====================
const char* WIFI_SSID   = "你的WiFi名";
const char* WIFI_PASS   = "你的WiFi密码";
const char* MQTT_BROKER = "192.168.1.100";  // 运行Mosquitto的PC IP地址
const int   MQTT_PORT   = 1883;
const char* MQTT_CLIENT = "ESP32_Monitor";
// ==================================================================

// 引脚定义
#define DHTPIN       4    // DHT22 DATA
#define ACS712_PIN   34   // ACS712 VOUT (ADC1_CH6)
#define TRIG_PIN     5    // HC-SR04 Trig
#define ECHO_PIN     18   // HC-SR04 Echo
#define RELAY_PIN    26   // 继电器 IN
#define BUZZER_PIN   27   // 蜂鸣器 +
#define MOTOR_INA    32   // L9110S INA (PWM)
#define MOTOR_INB    33   // L9110S INB

// 常量
#define DHTTYPE           DHT22
#define OLED_ADDR         0x3C
#define CURRENT_THRESHOLD 0.50   // 电流阈值 (A)，超过则报警
#define ALERT_COUNT_MAX   3      // 连续超阈值次数才断电
#define SAMPLE_MS         1000   // 采样间隔 (ms)
#define OLED_W            128
#define OLED_H            64

// 对象
DHT dht(DHTPIN, DHTTYPE);
BH1750 lightMeter(0x23);
Adafruit_SSD1306 display(OLED_W, OLED_H, &Wire, -1);
WiFiClient espClient;
PubSubClient mqtt(espClient);

// 状态
unsigned long lastSample = 0;
int alertCount = 0;
bool relayOn = true;
String systemStatus = "normal";

// ===================== WiFi =====================
void connectWiFi() {
  Serial.print("WiFi connecting");
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  for (int i = 0; i < 30 && WiFi.status() != WL_CONNECTED; i++) {
    delay(500); Serial.print(".");
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi OK, IP=" + WiFi.localIP().toString());
  } else {
    Serial.println("\nWiFi FAIL");
  }
}

// ===================== MQTT =====================
void mqttCallback(char* topic, byte* payload, unsigned int len) {
  String msg = "";
  for (unsigned int i = 0; i < len; i++) msg += (char)payload[i];
  Serial.println("MQTT RX [" + String(topic) + "]: " + msg);

  if (String(topic) == "cmd/relay") {
    if (msg == "OFF") {
      digitalWrite(RELAY_PIN, LOW); relayOn = false;
      Serial.println("-> Relay OFF (motor stop)");
    } else if (msg == "ON") {
      digitalWrite(RELAY_PIN, HIGH); relayOn = true;
      Serial.println("-> Relay ON (motor run)");
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
    } else {
      Serial.print("fail("); Serial.print(mqtt.state()); Serial.print(") ");
      delay(2000);
    }
  }
}

// ===================== 传感器读取 =====================
float readACS712() {
  // ACS712-5A: 185mV/A, 零点2.5V@5V供电
  // ESP32 ADC: 12bit, 0-4095, 0-3.3V
  float sum = 0;
  for (int i = 0; i < 10; i++) {
    sum += analogRead(ACS712_PIN);
    delayMicroseconds(100);
  }
  float adc = sum / 10.0;
  float voltage = adc * 3.3 / 4095.0;
  float current = (voltage - 2.5) / 0.185;
  return max(0.0f, current);
}

float readHCSR04() {
  digitalWrite(TRIG_PIN, LOW); delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH); delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);
  long duration = pulseIn(ECHO_PIN, HIGH, 30000);
  if (duration == 0) return 400;  // 超时返回最大值
  return duration * 0.034 / 2.0;  // cm
}

// ===================== OLED =====================
void updateOLED(float t, float h, float c) {
  display.clearDisplay();
  display.setTextColor(WHITE);
  display.setTextSize(2);
  display.setCursor(0, 0);
  display.printf("%.1fC %.0f%%", t, h);
  display.setCursor(0, 20);
  display.printf("%.2fA", c);
  display.setCursor(0, 44);
  display.setTextSize(1);
  display.print(systemStatus == "alert" ? "!! ALERT !!" :
                (systemStatus == "warning" ? "WARNING" : "NORMAL"));
  display.display();
}

// ===================== 报警 =====================
void triggerAlert() {
  digitalWrite(RELAY_PIN, LOW);
  digitalWrite(BUZZER_PIN, HIGH);
  relayOn = false;
  systemStatus = "alert";
  Serial.println("\n!!! CURRENT SPIKE - RELAY CUT !!!\n");
  delay(2000);
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

  digitalWrite(RELAY_PIN, HIGH);   // 继电器不触发 = COM-NC通 = 电机有电
  digitalWrite(BUZZER_PIN, LOW);
  analogWrite(MOTOR_INA, 180);     // PWM ~70%, 电机启动
  digitalWrite(MOTOR_INB, LOW);

  Wire.begin(21, 22);
  dht.begin();
  lightMeter.begin(BH1750::CONTINUOUS_HIGH_RES_MODE);

  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.println("OLED init failed");
  }
  display.clearDisplay(); display.display();

  connectWiFi();
  connectMQTT();

  Serial.println("\n========== IIOT Monitor Ready ==========");
  Serial.println("MQTT Topics:");
  Serial.println("  Publish: sensor/data  (JSON)");
  Serial.println("  Subscribe: cmd/relay  (ON/OFF)");
  Serial.println("=========================================\n");
}

// ===================== LOOP =====================
void loop() {
  if (WiFi.status() != WL_CONNECTED) connectWiFi();
  if (!mqtt.connected()) connectMQTT();
  mqtt.loop();

  if (millis() - lastSample >= SAMPLE_MS) {
    lastSample = millis();

    // 采集
    float temp = dht.readTemperature();
    float humi = dht.readHumidity();
    float lux  = lightMeter.readLightLevel();
    float current = readACS712();
    float distance = readHCSR04();

    if (isnan(temp)) temp = 0;
    if (isnan(humi)) humi = 0;
    if (lux < 0) lux = 0;

    // 报警判断
    if (current > CURRENT_THRESHOLD) {
      alertCount++;
      systemStatus = (alertCount >= ALERT_COUNT_MAX) ? "alert" : "warning";
    } else {
      alertCount = 0;
      systemStatus = "normal";
    }
    if (alertCount >= ALERT_COUNT_MAX && relayOn) {
      triggerAlert();
    }

    // JSON + MQTT发布
    StaticJsonDocument<256> doc;
    doc["temp"]     = round(temp * 10) / 10.0;
    doc["humi"]     = round(humi * 10) / 10.0;
    doc["lux"]      = (int)lux;
    doc["current"]  = round(current * 1000) / 1000.0;
    doc["distance"] = (int)distance;
    doc["status"]   = systemStatus;
    String json;
    serializeJson(doc, json);
    mqtt.publish("sensor/data", json.c_str(), true);  // retained

    // OLED
    updateOLED(temp, humi, current);

    // 串口调试
    Serial.printf("T:%.1f H:%.1f L:%d I:%.3f D:%d S:%s\n",
                  temp, humi, (int)lux, current, (int)distance,
                  systemStatus.c_str());
  }
}
