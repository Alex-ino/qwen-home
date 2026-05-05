/*
 * Умный холодильник на ESP8266 + Home Assistant
 * 
 * Оборудование:
 * - ESP8266 (NodeMCU/Wemos D1)
 * - 2x DS18B20 (холодильник + морозилка)
 * - Реле 5В (опционально, для управления компрессором)
 * 
 * Интеграция: MQTT Auto Discovery для Home Assistant
 */

#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <ArduinoJson.h>

// ==================== НАСТРОЙКИ ====================
const char* WIFI_SSID = "YOUR_WIFI_SSID";        // Измените на ваш SSID
const char* WIFI_PASS = "YOUR_WIFI_PASSWORD";    // Измените на ваш пароль

const char* MQTT_HOST = "192.168.1.XXX";         // IP MQTT брокера
const int MQTT_PORT = 1883;
const char* MQTT_USER = "";                       // Логин (если нужен)
const char* MQTT_PASS = "";                       // Пароль (если нужен)

// Пины
#define ONE_WIRE_MAIN 4      // GPIO4 (D2) - датчик холодильника
#define ONE_WIRE_FREEZER 5   // GPIO5 (D1) - датчик морозилки
#define RELAY_PIN 14         // GPIO14 (D5) - реле компрессора

// Топики MQTT
#define MQTT_BASE "home/fridge"
#define MQTT_TEMP_MAIN MQTT_BASE "/main/temp"
#define MQTT_TEMP_FREEZER MQTT_BASE "/freezer/temp"
#define MQTT_TARGET_MAIN MQTT_BASE "/main/target"
#define MQTT_TARGET_FREEZER MQTT_BASE "/freezer/target"
#define MQTT_COMP_STATE MQTT_BASE "/compressor/state"
#define MQTT_COMP_SET MQTT_BASE "/compressor/set"
#define MQTT_STATUS MQTT_BASE "/status"
#define MQTT_AVAIL MQTT_BASE "/avail"

// Идентификатор устройства для HA
#define DEVICE_ID "fridge_esp8266"
#define DEVICE_NAME "Холодильник"

// ==================== ГЛОБАЛЬНЫЕ ПЕРЕМЕННЫЕ ====================
WiFiClient espClient;
PubSubClient client(espClient);
OneWire oneWire_main(ONE_WIRE_MAIN);
OneWire oneWire_freezer(ONE_WIRE_FREEZER);
DallasTemperature sensors_main(&oneWire_main);
DallasTemperature sensors_freezer(&oneWire_freezer);

// Целевые температуры (можно менять через HA)
float target_temp_main_min = 2.0;    // Минимум для холодильника
float target_temp_main_max = 6.0;    // Максимум для холодильника
float target_temp_freezer_min = -20.0; // Минимум для морозилки
float target_temp_freezer_max = -15.0; // Максимум для морозилки

// Состояние компрессора
bool compressor_on = false;
unsigned long lastPublishTime = 0;
unsigned long lastReconnectTime = 0;
const unsigned long PUBLISH_INTERVAL = 5000;  // Публикация каждые 5 сек

// Last Will Testament
char will_topic[] = MQTT_STATUS;
char will_message[] = "offline";

// ==================== ФУНКЦИИ ====================

void setup_wifi() {
  Serial.println();
  Serial.print("Connecting to WiFi: ");
  Serial.println(WIFI_SSID);
  
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  
  Serial.println("");
  Serial.println("WiFi connected");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
}

void callback(char* topic, byte* payload, unsigned int length) {
  // Преобразуем payload в строку
  char message[length + 1];
  for (unsigned int i = 0; i < length; i++) {
    message[i] = (char)payload[i];
  }
  message[length] = '\0';
  
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("]: ");
  Serial.println(message);
  
  // Обработка команд от HA
  if (String(topic) == MQTT_TARGET_MAIN) {
    float new_target = atof(message);
    target_temp_main_max = new_target;
    target_temp_main_min = new_target - 2.0; // Гистерезис 2°C
    Serial.printf("New fridge target: %.1f°C (min: %.1f°C)\n", 
                  target_temp_main_max, target_temp_main_min);
    
    // Отправляем подтверждение
    client.publish(MQTT_TARGET_MAIN, message);
  }
  else if (String(topic) == MQTT_TARGET_FREEZER) {
    float new_target = atof(message);
    target_temp_freezer_max = new_target;
    target_temp_freezer_min = new_target - 3.0; // Гистерезис 3°C
    Serial.printf("New freezer target: %.1f°C (min: %.1f°C)\n", 
                  target_temp_freezer_max, target_temp_freezer_min);
    
    // Отправляем подтверждение
    client.publish(MQTT_TARGET_FREEZER, message);
  }
  else if (String(topic) == MQTT_COMP_SET) {
    if (String(message) == "ON" || String(message) == "on" || String(message) == "true") {
      compressor_on = true;
      digitalWrite(RELAY_PIN, HIGH);
      client.publish(MQTT_COMP_STATE, "ON");
    } else {
      compressor_on = false;
      digitalWrite(RELAY_PIN, LOW);
      client.publish(MQTT_COMP_STATE, "OFF");
    }
  }
}

void reconnect_mqtt() {
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    
    String clientId = "fridge-" + String(WiFi.macAddress());
    
    if (client.connect(clientId.c_str(), MQTT_USER, MQTT_PASS, 
                       will_topic, 1, true, will_message)) {
      Serial.println("connected!");
      
      // Публикуем статус online
      client.publish(MQTT_STATUS, "online");
      client.publish(MQTT_AVAIL, "online");
      
      // Подписываемся на топики
      client.subscribe(MQTT_TARGET_MAIN);
      client.subscribe(MQTT_TARGET_FREEZER);
      client.subscribe(MQTT_COMP_SET);
      
      // Отправляем текущее состояние реле
      client.publish(MQTT_COMP_STATE, compressor_on ? "ON" : "OFF");
      
      // Отправляем целевые температуры
      char buf[10];
      dtostrf(target_temp_main_max, 1, 1, buf);
      client.publish(MQTT_TARGET_MAIN, buf);
      
      dtostrf(target_temp_freezer_max, 1, 1, buf);
      client.publish(MQTT_TARGET_FREEZER, buf);
      
      // MQTT Auto Discovery для Home Assistant
      send_auto_discovery();
      
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      delay(5000);
    }
  }
}

void send_auto_discovery() {
  // JSON для сенсоров и устройств
  StaticJsonDocument<512> doc;
  
  // Устройство
  doc["device"]["identifiers"][0] = DEVICE_ID;
  doc["device"]["name"] = DEVICE_NAME;
  doc["device"]["manufacturer"] = "DIY";
  doc["device"]["model"] = "ESP8266+DS18B20";
  doc["device"]["sw_version"] = "1.0";
  
  // Датчик температуры холодильника
  doc["name"] = "Fridge Temperature";
  doc["unique_id"] = "fridge_temp_main";
  doc["state_topic"] = MQTT_TEMP_MAIN;
  doc["unit_of_measurement"] = "°C";
  doc["device_class"] = "temperature";
  doc["availability_topic"] = MQTT_AVAIL;
  
  char buffer[512];
  serializeJson(doc, buffer);
  client.publish("homeassistant/sensor/" DEVICE_ID "/temp_main/config", buffer, true);
  
  // Датчик температуры морозилки
  doc["name"] = "Freezer Temperature";
  doc["unique_id"] = "fridge_temp_freezer";
  doc["state_topic"] = MQTT_TEMP_FREEZER;
  
  serializeJson(doc, buffer);
  client.publish("homeassistant/sensor/" DEVICE_ID "/temp_freezer/config", buffer, true);
  
  // Number entity для целевой температуры холодильника
  doc.clear();
  doc["device"]["identifiers"][0] = DEVICE_ID;
  doc["device"]["name"] = DEVICE_NAME;
  doc["name"] = "Fridge Target Temp";
  doc["unique_id"] = "fridge_target_main";
  doc["command_topic"] = String(MQTT_TARGET_MAIN) + "/set";
  doc["state_topic"] = MQTT_TARGET_MAIN;
  doc["min"] = -10;
  doc["max"] = 10;
  doc["step"] = 0.5;
  doc["unit_of_measurement"] = "°C";
  doc["mode"] = "box";
  doc["availability_topic"] = MQTT_AVAIL;
  
  serializeJson(doc, buffer);
  client.publish("homeassistant/number/" DEVICE_ID "/target_main/config", buffer, true);
  
  // Number entity для целевой температуры морозилки
  doc["name"] = "Freezer Target Temp";
  doc["unique_id"] = "fridge_target_freezer";
  doc["command_topic"] = String(MQTT_TARGET_FREEZER) + "/set";
  doc["state_topic"] = MQTT_TARGET_FREEZER;
  doc["min"] = -30;
  doc["max"] = -10;
  
  serializeJson(doc, buffer);
  client.publish("homeassistant/number/" DEVICE_ID "/target_freezer/config", buffer, true);
  
  // Switch для компрессора
  doc.clear();
  doc["device"]["identifiers"][0] = DEVICE_ID;
  doc["device"]["name"] = DEVICE_NAME;
  doc["name"] = "Fridge Compressor";
  doc["unique_id"] = "fridge_compressor";
  doc["command_topic"] = MQTT_COMP_SET;
  doc["state_topic"] = MQTT_COMP_STATE;
  doc["availability_topic"] = MQTT_AVAIL;
  
  serializeJson(doc, buffer);
  client.publish("homeassistant/switch/" DEVICE_ID "/compressor/config", buffer, true);
  
  Serial.println("Auto discovery sent");
}

float read_temperature(DallasTemperature& sensors) {
  sensors.requestTemperatures();
  float temp = sensors.getTempCByIndex(0);
  
  // Проверка на ошибку чтения
  if (temp == -127.0 || temp == -196.6) {
    Serial.println("Error reading temperature sensor!");
    return -999.0; // Специальное значение ошибки
  }
  
  return temp;
}

void control_compressor(float temp_main, float temp_freezer) {
  // Автоматическое управление только если не включено вручную
  // Простая логика: включаем если температура выше максимума
  
  bool need_cooling = false;
  
  if (temp_main > target_temp_main_max) {
    need_cooling = true;
    Serial.println("Fridge too warm!");
  }
  
  if (temp_freezer > target_temp_freezer_max) {
    need_cooling = true;
    Serial.println("Freezer too warm!");
  }
  
  // Выключаем если температура ниже минимума (гистерезис)
  bool too_cold = false;
  if (temp_main < target_temp_main_min && temp_freezer < target_temp_freezer_min) {
    too_cold = true;
    Serial.println("Both zones cold enough");
  }
  
  if (need_cooling && !compressor_on) {
    compressor_on = true;
    digitalWrite(RELAY_PIN, HIGH);
    client.publish(MQTT_COMP_STATE, "ON");
    Serial.println("Compressor ON");
  }
  else if (too_cold && compressor_on) {
    compressor_on = false;
    digitalWrite(RELAY_PIN, LOW);
    client.publish(MQTT_COMP_STATE, "OFF");
    Serial.println("Compressor OFF");
  }
}

void publish_temperatures(float temp_main, float temp_freezer) {
  char buf[10];
  
  // Температура холодильника
  if (temp_main > -900) {
    dtostrf(temp_main, 1, 2, buf);
    client.publish(MQTT_TEMP_MAIN, buf);
    Serial.printf("Published main temp: %s°C\n", buf);
  }
  
  // Температура морозилки
  if (temp_freezer > -900) {
    dtostrf(temp_freezer, 1, 2, buf);
    client.publish(MQTT_TEMP_FREEZER, buf);
    Serial.printf("Published freezer temp: %s°C\n", buf);
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println();
  Serial.println("=== Fridge Controller Starting ===");
  
  // Инициализация реле
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW);
  
  // Инициализация датчиков
  sensors_main.begin();
  sensors_freezer.begin();
  
  Serial.printf("Found %d sensor(s) on main bus\n", sensors_main.getDeviceCount());
  Serial.printf("Found %d sensor(s) on freezer bus\n", sensors_freezer.getDeviceCount());
  
  // Подключение к WiFi
  setup_wifi();
  
  // Настройка MQTT
  client.setServer(MQTT_HOST, MQTT_PORT);
  client.setCallback(callback);
  client.setBufferSize(1024);
  
  Serial.println("=== Setup Complete ===");
}

void loop() {
  // Поддержание соединения WiFi
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi lost, reconnecting...");
    setup_wifi();
  }
  
  // Поддержание соединения MQTT
  if (!client.connected()) {
    reconnect_mqtt();
  }
  
  client.loop();
  
  // Чтение температур и публикация по таймеру
  unsigned long currentMillis = millis();
  
  if (currentMillis - lastPublishTime >= PUBLISH_INTERVAL) {
    lastPublishTime = currentMillis;
    
    // Чтение температур
    float temp_main = read_temperature(sensors_main);
    float temp_freezer = read_temperature(sensors_freezer);
    
    Serial.printf("Temperatures - Main: %.2f°C, Freezer: %.2f°C\n", temp_main, temp_freezer);
    
    // Публикация
    if (client.connected()) {
      publish_temperatures(temp_main, temp_freezer);
      
      // Управление компрессором (автоматический режим)
      control_compressor(temp_main, temp_freezer);
    }
  }
  
  delay(100);
}
