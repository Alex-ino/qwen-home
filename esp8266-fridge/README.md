# Умный холодильник на ESP8266 + Home Assistant

Система мониторинга и управления холодильником с интеграцией в Home Assistant.

## 📦 Оборудование

| Компонент | Количество | Назначение |
|-----------|------------|------------|
| **ESP8266** (NodeMCU/Wemos D1) | 1 | Микроконтроллер с Wi-Fi |
| **DS18B20** | 2 | Датчики температуры (холодильник + морозилка) |
| **Резистор 4.7кОм** | 2 | Подтяжка для Dallas-протокола |
| **Реле 5В 1-канальное** | 1 | Управление компрессором (опционально) |
| **Провода** | - | Соединение компонентов |

## 🔌 Схема подключения

```
ESP8266 (NodeMCU/Wemos D1):
├── GPIO4 (D2) ──┬── DS18B20 #1 (холодильник)
│                └── через резистор 4.7кОм к VCC
├── GPIO5 (D1) ──┬── DS18B20 #2 (морозилка)
│                └── через резистор 4.7кОм к VCC
├── GPIO14 (D5) ── Реле (компрессор)
├── 3.3V ──────── VCC датчиков
└── GND ───────── GND всех компонентов
```

## 🚀 Возможности

✅ **Автоматическая интеграция с Home Assistant** через MQTT Auto Discovery  
✅ **Два независимых датчика** температуры  
✅ **Удалённая настройка** пороговых значений  
✅ **Уведомления** при критических температурах  
✅ **История температур** в HA  
✅ **Ручное управление** компрессором  
✅ **Отказоустойчивость** при потере Wi-Fi  

---

## 📁 Структура проекта

```
esp8266-fridge/
├── firmware/
│   └── fridge.ino          # Прошивка для Arduino IDE
├── docs/
│   └── ha-config.yaml      # Конфигурация Home Assistant
└── README.md               # Этот файл
```

---

## 🛠️ Установка

### 1. Подготовка железа

1. Подключите датчики DS18B20 к ESP8266 по схеме выше
2. Припаяйте подтягивающие резисторы 4.7кОм между VCC и DATA каждого датчика
3. Подключите реле к GPIO14 если нужно управление компрессором

### 2. Настройка прошивки

1. Установите [Arduino IDE](https://www.arduino.cc/en/software)
2. Добавьте плату ESP8266:
   - Файл → Настройки → Дополнительные URL: `http://arduino.esp8266.com/stable/package_esp8266com_index.json`
   - Инструменты → Платы → Менеджер плат → установите "ESP8266 by ESP8266 Community"
3. Установите библиотеки (Инструменты → Управление библиотеками):
   - `OneWire` by Paul Stoffregen
   - `DallasTemperature` by Miles Burton
   - `PubSubClient` by Nick O'Leary
4. Откройте `firmware/fridge.ino`
5. Отредактируйте настройки в начале файла:
   ```cpp
   const char* WIFI_SSID = "YOUR_WIFI_SSID";
   const char* WIFI_PASS = "YOUR_WIFI_PASSWORD";
   const char* MQTT_HOST = "192.168.1.XXX";  // IP вашего MQTT брокера
   ```
6. Загрузите прошивку на ESP8266

### 3. Настройка MQTT брокера

В Home Assistant (если используется Mosquitto addon):

1. Установите аддон "Mosquitto broker"
2. Включите аутентификацию или оставьте анонимный доступ для локальной сети
3. Запишите данные для подключения

### 4. Интеграция с Home Assistant

Прошивка использует **MQTT Auto Discovery**, поэтому устройства появятся автоматически!

Если нужно вручную добавить в `configuration.yaml`:

```yaml
mqtt:
  sensor:
    - name: "Fridge Temperature"
      unique_id: fridge_temp_main
      state_topic: "home/fridge/main/temp"
      unit_of_measurement: "°C"
      device_class: temperature
      device:
        identifiers: ["fridge_esp8266"]
        name: "Холодильник"
        manufacturer: "DIY"
        model: "ESP8266+DS18B20"
    
    - name: "Freezer Temperature"
      unique_id: fridge_temp_freezer
      state_topic: "home/fridge/freezer/temp"
      unit_of_measurement: "°C"
      device_class: temperature
      device:
        identifiers: ["fridge_esp8266"]
        name: "Холодильник"

  number:
    - name: "Fridge Target Temp"
      unique_id: fridge_target_main
      command_topic: "home/fridge/main/target/set"
      state_topic: "home/fridge/main/target"
      min: -10
      max: 10
      step: 0.5
      unit_of_measurement: "°C"
      mode: box
    
    - name: "Freezer Target Temp"
      unique_id: fridge_target_freezer
      command_topic: "home/fridge/freezer/target/set"
      state_topic: "home/fridge/freezer/target"
      min: -30
      max: -10
      step: 0.5
      unit_of_measurement: "°C"
      mode: box

  switch:
    - name: "Fridge Compressor"
      unique_id: fridge_compressor
      command_topic: "home/fridge/compressor/set"
      state_topic: "home/fridge/compressor/state"
      device:
        identifiers: ["fridge_esp8266"]
```

Перезапустите Home Assistant после добавления конфигурации.

---

## 📱 Использование в Home Assistant

### Ловелас карточка

Добавьте в Dashboard:

```yaml
type: entities
title: Холодильник
entities:
  - entity: sensor.fridge_temperature
    name: Холодильная камера
  - entity: sensor.freezer_temperature
    name: Морозильная камера
  - entity: number.fridge_target_temp
    name: Целевая темп. (холод.)
  - entity: number.freezer_target_temp
    name: Целевая темп. (мороз.)
  - entity: switch.fridge_compressor
    name: Компрессор
```

### Автоматизация уведомлений

```yaml
alias: Холодильник - Предупреждение о температуре
trigger:
  - platform: numeric_state
    entity_id: sensor.fridge_temperature
    above: 8
  - platform: numeric_state
    entity_id: sensor.freezer_temperature
    above: -15
action:
  - service: notify.mobile_app_your_phone
    data:
      title: "⚠️ Холодильник"
      message: >
        {% if trigger.entity_id == 'sensor.fridge_temperature' %}
          Температура в холодильнике: {{ states('sensor.fridge_temperature') }}°C
        {% else %}
          Температура в морозилке: {{ states('sensor.freezer_temperature') }}°C
        {% endif %}
mode: single
```

---

## ⚙️ Топики MQTT

| Топик | Направление | Описание |
|-------|-------------|----------|
| `home/fridge/main/temp` | ESP → HA | Температура холодильника |
| `home/fridge/freezer/temp` | ESP → HA | Температура морозилки |
| `home/fridge/main/target` | ESP ↔ HA | Целевая температура (холод.) |
| `home/fridge/freezer/target` | ESP ↔ HA | Целевая температура (мороз.) |
| `home/fridge/compressor/state` | ESP → HA | Состояние компрессора |
| `home/fridge/compressor/set` | HA → ESP | Управление компрессором |
| `home/fridge/status` | ESP → HA | Статус устройства (online/offline) |

---

## 🔧 Настройка порогов

По умолчанию в прошивке:
- Холодильник: 2°C ... 6°C (гистерезис 2°C)
- Морозилка: -20°C ... -15°C (гистерезис 3°C)

Изменяйте через интерфейс Home Assistant (entity `number.*`).

---

## 🐛 Troubleshooting

**Датчик не определяется:**
- Проверьте подключение резистора 4.7кОм
- Убедитесь в правильности распиновки
- Проверьте контакты пайки

**Нет связи с MQTT:**
- Проверьте правильный ли IP брокера
- Убедитесь что ESP8266 в той же сети
- Проверьте логин/пароль если есть аутентификация

**Температура неверная:**
- Калибруйте датчики (смещение в прошивке)
- Убедитесь что датчик плотно прилегает к поверхности

---

## 📊 Мониторинг истории

Home Assistant автоматически сохраняет историю температур. Для просмотра:
1. Откройте Entity в Developer Tools
2. Перейдите на вкладку "History"
3. Или используйте карту `history-graph` в Lovelace

```yaml
type: history-graph
entities:
  - entity: sensor.fridge_temperature
  - entity: sensor.freezer_temperature
hours_to_show: 24
```
