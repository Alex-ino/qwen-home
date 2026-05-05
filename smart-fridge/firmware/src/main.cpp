/*
 * Smart Fridge Firmware
 * ESP32 прошивка для управления холодильником
 * 
 * Функции:
 * - Чтение температуры с двух датчиков DS18B20
 * - Управление реле компрессора и света
 * - Отправка телеметрии по MQTT
 * - Получение команд от сервера
 * - Локальное управление кнопками
 * - Отображение на OLED дисплее
 */

#include <WiFi.h>
#include <PubSubClient.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <Adafruit_SSD1306.h>
#include <ArduinoJson.h>
#include <Preferences.h>

// ==================== КОНФИГУРАЦИЯ ====================

const char* WIFI_SSID = "YOUR_WIFI_SSID";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";
const char* MQTT_SERVER = "192.168.1.100";
const int MQTT_PORT = 1883;
const char* MQTT_USER = "fridge_user";
const char* MQTT_PASS = "fridge_password";
const char* DEVICE_ID = "smart-fridge-001";

// Пинов
#define ONE_WIRE_BUS_FRIDGE 4
#define ONE_WIRE_BUS_FREEZER 16
#define RELAY_COMPRESSOR 17
#define RELAY_LIGHT 18
#define BUTTON_MODE 5
#define BUTTON_PLUS 19
#define BUTTON_MINUS 20
#define LED_STATUS 23

// I2C для OLED
#define OLED_SDA 21
#define OLED_SCL 22
#define OLED_RESET -1

// Топики MQTT
char topic_temperature[64];
char topic_status[64];
char topic_command[64];
char topic_config[64];

// Глобальные переменные
WiFiClient espClient;
PubSubClient client(espClient);
OneWire oneWireFridge(ONE_WIRE_BUS_FRIDGE);
OneWire oneWireFreezer(ONE_WIRE_BUS_FREEZER);
DallasTemperature sensorsFridge(&oneWireFridge);
DallasTemperature sensorsFreezer(&oneWireFreezer);
Adafruit_SSD1306 display(128, 64, &Wire, OLED_RESET);
Preferences preferences;

// Настройки
float targetTempFridge = 4.0;    // Целевая температура холодильник (°C)
float targetTempFreezer = -18.0; // Целевая температура морозилка (°C)
float hysteresis = 1.5;          // Гистерезис (°C)
bool compressorOn = false;
bool lightOn = false;
unsigned long lastPublish = 0;
unsigned long lastDisplayUpdate = 0;
int displayMode = 0;
float currentTempFridge = 0.0;
float currentTempFreezer = 0.0;

// ==================== ФУНКЦИИ ====================

void setupWiFi() {
  Serial.print("Подключение к WiFi");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi подключен!");
    Serial.print("IP адрес: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\nОшибка подключения WiFi!");
  }
}

void callback(char* topic, byte* payload, unsigned int length) {
  char message[length + 1];
  memcpy(message, payload, length);
  message[length] = '\0';
  
  Serial.print("Получено сообщение [");
  Serial.print(topic);
  Serial.print("]: ");
  Serial.println(message);
  
  // Парсинг JSON команды
  StaticJsonDocument<256> doc;
  DeserializationError error = deserializeJson(doc, message);
  
  if (error) {
    Serial.println("Ошибка парсинга JSON");
    return;
  }
  
  if (strcmp(topic, topic_config) == 0) {
    // Обработка команды настройки
    if (doc.containsKey("targetTempFridge")) {
      targetTempFridge = doc["targetTempFridge"];
      preferences.putFloat("targetFridge", targetTempFridge);
    }
    if (doc.containsKey("targetTempFreezer")) {
      targetTempFreezer = doc["targetTempFreezer"];
      preferences.putFloat("targetFreezer", targetTempFreezer);
    }
    if (doc.containsKey("hysteresis")) {
      hysteresis = doc["hysteresis"];
      preferences.putFloat("hysteresis", hysteresis);
    }
    if (doc.containsKey("compressor")) {
      compressorOn = doc["compressor"];
      digitalWrite(RELAY_COMPRESSOR, compressorOn ? LOW : HIGH);
    }
    
    updateDisplay();
    publishStatus();
  }
}

void reconnectMQTT() {
  while (!client.connected()) {
    Serial.print("Подключение к MQTT...");
    
    if (client.connect(DEVICE_ID, MQTT_USER, MQTT_PASS)) {
      Serial.println("подключено!");
      
      // Подписка на топики
      client.subscribe(topic_command);
      client.subscribe(topic_config);
      
      Serial.println("Топики подписаны");
    } else {
      Serial.print("ошибка, rc=");
      Serial.print(client.state());
      Serial.println(" повтор через 5с");
      delay(5000);
    }
  }
}

void publishStatus() {
  StaticJsonDocument<512> doc;
  
  doc["deviceId"] = DEVICE_ID;
  doc["timestamp"] = millis();
  doc["tempFridge"] = currentTempFridge;
  doc["tempFreezer"] = currentTempFreezer;
  doc["targetTempFridge"] = targetTempFridge;
  doc["targetTempFreezer"] = targetTempFreezer;
  doc["compressorOn"] = compressorOn;
  doc["lightOn"] = lightOn;
  doc["wifiRSSI"] = WiFi.RSSI();
  doc["freeHeap"] = ESP.getFreeHeap();
  
  char buffer[512];
  serializeJson(doc, buffer);
  
  if (client.publish(topic_status, buffer)) {
    Serial.println("Статус отправлен");
  } else {
    Serial.println("Ошибка отправки статуса");
  }
}

void publishConfig() {
  StaticJsonDocument<256> doc;
  
  doc["targetTempFridge"] = targetTempFridge;
  doc["targetTempFreezer"] = targetTempFreezer;
  doc["hysteresis"] = hysteresis;
  
  char buffer[256];
  serializeJson(doc, buffer);
  
  client.publish(topic_config, buffer);
}

void controlCompressor(float currentTemp, float targetTemp) {
  bool shouldTurnOn = currentTemp > (targetTemp + hysteresis);
  bool shouldTurnOff = currentTemp < (targetTemp - hysteresis);
  
  if (shouldTurnOn && !compressorOn) {
    compressorOn = true;
    digitalWrite(RELAY_COMPRESSOR, LOW); // Реле активное низким уровнем
    Serial.println("Компрессор ВКЛЮЧЕН");
  } else if (shouldTurnOff && compressorOn) {
    compressorOn = false;
    digitalWrite(RELAY_COMPRESSOR, HIGH);
    Serial.println("Компрессор ВЫКЛЮЧЕН");
  }
}

void handleButtons() {
  static unsigned long lastPressTime = 0;
  const unsigned long debounceDelay = 300;
  
  if (millis() - lastPressTime > debounceDelay) {
    if (digitalRead(BUTTON_MODE) == LOW) {
      lastPressTime = millis();
      // Переключение режима отображения
      displayMode = (displayMode + 1) % 3;
      updateDisplay();
    }
    
    if (digitalRead(BUTTON_PLUS) == LOW) {
      lastPressTime = millis();
      // Увеличение целевой температуры
      targetTempFridge += 0.5;
      if (targetTempFridge > 10) targetTempFridge = 10;
      preferences.putFloat("targetFridge", targetTempFridge);
      updateDisplay();
      publishConfig();
    }
    
    if (digitalRead(BUTTON_MINUS) == LOW) {
      lastPressTime = millis();
      // Уменьшение целевой температуры
      targetTempFridge -= 0.5;
      if (targetTempFridge < 0) targetTempFridge = 0;
      preferences.putFloat("targetFridge", targetTempFridge);
      updateDisplay();
      publishConfig();
    }
  }
}

void updateDisplay() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  
  switch(displayMode) {
    case 0: // Основной экран
      display.setCursor(0, 0);
      display.println("Холодильник");
      display.setTextSize(2);
      display.print(currentTempFridge, 1);
      display.print("C");
      display.setTextSize(1);
      display.println();
      display.print("Цель: ");
      display.print(targetTempFridge, 1);
      display.print("C");
      break;
      
    case 1: // Морозилка
      display.setCursor(0, 0);
      display.println("Морозилка");
      display.setTextSize(2);
      display.print(currentTempFreezer, 1);
      display.print("C");
      break;
      
    case 2: // Статус
      display.setCursor(0, 0);
      display.println("Статус:");
      display.print("Komp: ");
      display.println(compressorOn ? "ON" : "OFF");
      display.print("WiFi: ");
      display.println(WiFi.RSSI());
      break;
  }
  
  display.display();
}

void loadSettings() {
  preferences.begin("fridge", false);
  targetTempFridge = preferences.getFloat("targetFridge", 4.0);
  targetTempFreezer = preferences.getFloat("targetFreezer", -18.0);
  hysteresis = preferences.getFloat("hysteresis", 1.5);
  preferences.end();
  
  Serial.printf("Загружены настройки: Х=%f, М=%f, H=%f\n", 
                targetTempFridge, targetTempFreezer, hysteresis);
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("\n=== Smart Fridge Firmware ===");
  
  // Инициализация пинов
  pinMode(RELAY_COMPRESSOR, OUTPUT);
  pinMode(RELAY_LIGHT, OUTPUT);
  pinMode(BUTTON_MODE, INPUT_PULLUP);
  pinMode(BUTTON_PLUS, INPUT_PULLUP);
  pinMode(BUTTON_MINUS, INPUT_PULLUP);
  pinMode(LED_STATUS, OUTPUT);
  
  // Реле выключены (высокий уровень)
  digitalWrite(RELAY_COMPRESSOR, HIGH);
  digitalWrite(RELAY_LIGHT, HIGH);
  
  // Загрузка настроек
  loadSettings();
  
  // OLED дисплей
  Wire.begin(OLED_SDA, OLED_SCL);
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("Ошибка инициализации OLED");
  } else {
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println("Smart Fridge");
    display.println("Загрузка...");
    display.display();
  }
  
  // Датчики температуры
  sensorsFridge.begin();
  sensorsFreezer.begin();
  sensorsFridge.requestTemperatures();
  sensorsFreezer.requestTemperatures();
  
  // WiFi
  setupWiFi();
  
  // MQTT
  snprintf(topic_temperature, sizeof(topic_temperature), "fridge/%s/temperature", DEVICE_ID);
  snprintf(topic_status, sizeof(topic_status), "fridge/%s/status", DEVICE_ID);
  snprintf(topic_command, sizeof(topic_command), "fridge/%s/command", DEVICE_ID);
  snprintf(topic_config, sizeof(topic_config), "fridge/%s/config", DEVICE_ID);
  
  client.setServer(MQTT_SERVER, MQTT_PORT);
  client.setCallback(callback);
  
  Serial.println("Инициализация завершена");
}

void loop() {
  // Поддержание соединения WiFi
  if (WiFi.status() != WL_CONNECTED) {
    setupWiFi();
  }
  
  // Поддержание соединения MQTT
  if (!client.connected()) {
    reconnectMQTT();
  }
  client.loop();
  
  // Чтение температуры (каждые 2 секунды)
  static unsigned long lastTempRead = 0;
  if (millis() - lastTempRead > 2000) {
    lastTempRead = millis();
    
    sensorsFridge.requestTemperatures();
    sensorsFreezer.requestTemperatures();
    delay(100);
    
    currentTempFridge = sensorsFridge.getTempCByIndex(0);
    currentTempFreezer = sensorsFreezer.getTempCByIndex(0);
    
    Serial.printf("Температура: Х=%fC, М=%fC\n", currentTempFridge, currentTempFreezer);
    
    // Управление компрессором
    controlCompressor(currentTempFridge, targetTempFridge);
  }
  
  // Публикация статуса (каждые 5 секунд)
  if (millis() - lastPublish > 5000) {
    lastPublish = millis();
    publishStatus();
    
    // Мигание светодиодом
    digitalWrite(LED_STATUS, !digitalRead(LED_STATUS));
  }
  
  // Обновление дисплея (каждую секунду)
  if (millis() - lastDisplayUpdate > 1000) {
    lastDisplayUpdate = millis();
    updateDisplay();
  }
  
  // Обработка кнопок
  handleButtons();
  
  delay(50);
}
