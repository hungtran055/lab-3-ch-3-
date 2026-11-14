/*
 * LAB 3 - De 3: Mach bao chay gia dinh (ESP32 + FreeRTOS + MQTT)
 * Wokwi: MQ2 (mo phong bang bien tro/analog) + NTC + 2 LED (Quat, Bom)
 */
#include <WiFi.h>
#include <PubSubClient.h>
#include <math.h>

// ---------- Chan ket noi ----------
#define PIN_MQ2   34   // ADC1_CH6 - AO cua MQ2
#define PIN_NTC   35   // ADC1_CH7 - cam bien nhiet NTC
#define PIN_FAN   26   // Quat (LED/relay)
#define PIN_PUMP  27   // May bom (LED/relay)

// ---------- WiFi / MQTT ----------
const char* WIFI_SSID = "Wokwi-GUEST";
const char* WIFI_PASS = "";
const char* MQTT_HOST = "broker.hivemq.com";
const int   MQTT_PORT = 1883;
const char* T_DATA = "lab3/firealarm/data";      // ESP32 -> Web (JSON)
const char* T_FAN  = "lab3/firealarm/cmd/fan";   // Web -> ESP32 (ON/OFF)
const char* T_PUMP = "lab3/firealarm/cmd/pump";

// ---------- Nguong canh bao ----------
const int   GAS_THRESHOLD  = 2000;  // gia tri ADC 0..4095
const float TEMP_THRESHOLD = 50.0;  // do C

// ---------- Kieu du lieu & Queue ----------
typedef struct { int gas; float temp; } SensorData;

QueueHandle_t dataQueue;   // Task doc -> Task truyen (day du)
QueueHandle_t alertQueue;  // Task doc -> Task canh bao
volatile bool manualFan = false, manualPump = false; // lenh tu Web

WiFiClient espClient;
PubSubClient mqtt(espClient);

// ---------- Callback nhan lenh tu Web ----------
void onMessage(char* topic, byte* payload, unsigned int len) {
  String msg; for (unsigned i = 0; i < len; i++) msg += (char)payload[i];
  bool on = (msg == "ON");
  if (String(topic) == T_FAN)  manualFan  = on;
  if (String(topic) == T_PUMP) manualPump = on;
}

// ---------- TASK 1: truyen data (Core 0) ----------
void taskTransmit(void* pv) {
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) { vTaskDelay(pdMS_TO_TICKS(500)); }
  Serial.println("WiFi OK");
  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setCallback(onMessage);

  SensorData d;
  for (;;) {
    if (!mqtt.connected()) {
      String id = "esp32-lab3-" + String((uint32_t)esp_random(), HEX);
      if (mqtt.connect(id.c_str())) {
        mqtt.subscribe(T_FAN); mqtt.subscribe(T_PUMP);
      } else { vTaskDelay(pdMS_TO_TICKS(2000)); continue; }
    }
    mqtt.loop();
    if (xQueueReceive(dataQueue, &d, pdMS_TO_TICKS(100)) == pdTRUE) {
      char buf[80];
      snprintf(buf, sizeof(buf), "{\"gas\":%d,\"temp\":%.1f}", d.gas, d.temp);
      mqtt.publish(T_DATA, buf);
    }
  }
}

// ---------- TASK 2: doc data (Core 1) ----------
float readTemp() {                       // NTC 10k, Beta 3950 (mo hinh Wokwi)
  int adc = analogRead(PIN_NTC);
  return 1.0 / (log(1.0 / (4095.0 / adc - 1)) / 3950.0 + 1.0 / 298.15) - 273.15;
}
void taskRead(void* pv) {
  for (;;) {
    SensorData d;
    d.gas  = analogRead(PIN_MQ2);
    d.temp = readTemp();
    Serial.printf("Gas: %d | Temp: %.1f C\n", d.gas, d.temp);
    xQueueSend(dataQueue,  &d, 0);       // gui day du
    xQueueSend(alertQueue, &d, 0);       // gui cho canh bao
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

// ---------- TASK 3: canh bao (Core 1) ----------
void taskAlert(void* pv) {
  SensorData d;
  for (;;) {
    if (xQueueReceive(alertQueue, &d, portMAX_DELAY) == pdTRUE) {
      bool fan  = (d.gas  > GAS_THRESHOLD)  || manualFan;   // gas vuot -> bat quat
      bool pump = (d.temp > TEMP_THRESHOLD) || manualPump;  // nhiet vuot -> bat bom
      digitalWrite(PIN_FAN,  fan);
      digitalWrite(PIN_PUMP, pump);
    }
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(PIN_FAN, OUTPUT); pinMode(PIN_PUMP, OUTPUT);
  dataQueue  = xQueueCreate(10, sizeof(SensorData));
  alertQueue = xQueueCreate(10, sizeof(SensorData));
  xTaskCreatePinnedToCore(taskTransmit, "Truyen", 8192, NULL, 1, NULL, 0);
  xTaskCreatePinnedToCore(taskRead,     "Doc",    4096, NULL, 2, NULL, 1);
  xTaskCreatePinnedToCore(taskAlert,    "CanhBao",4096, NULL, 3, NULL, 1);
}
void loop() { vTaskDelete(NULL); }
