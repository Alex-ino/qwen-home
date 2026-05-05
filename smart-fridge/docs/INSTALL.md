# Инструкция по установке и запуску

## 1. Установка MQTT брокера (Mosquitto)

### Ubuntu/Debian:
```bash
sudo apt update
sudo apt install mosquitto mosquitto-clients
sudo systemctl enable mosquitto
sudo systemctl start mosquitto
```

### Настройка Mosquitto:
```bash
sudo nano /etc/mosquitto/mosquitto.conf
```

Добавьте:
```
listener 1883
allow_anonymous false
password_file /etc/mosquitto/passwd
```

Создайте пользователя:
```bash
sudo mosquitto_passwd -c /etc/mosquitto/passwd fridge_user
```

Перезапустите:
```bash
sudo systemctl restart mosquitto
```

## 2. Установка и запуск Backend

```bash
cd backend
npm install
cp .env.example .env
npm start
```

## 3. Установка прошивки на ESP32

### Вариант A: PlatformIO (рекомендуется)
```bash
# Установите PlatformIO CLI
pip install platformio

# Перейдите в папку firmware
cd firmware

# Скомпилируйте и загрузите
pio run --target upload
```

### Вариант B: Arduino IDE
1. Откройте `firmware/src/main.cpp` в Arduino IDE
2. Установите библиотеки через Library Manager:
   - PubSubClient by Nick O'Leary
   - DallasTemperature by Miles Burton
   - OneWire by Paul Stoffregen
   - Adafruit SSD1306
   - ArduinoJson by Benoit Blanchon
3. Настройте WiFi и MQTT параметры в начале файла
4. Загрузите на ESP32

## 4. Проверка работы

### Проверка MQTT:
```bash
mosquitto_sub -h localhost -t 'fridge/#' -v -u fridge_user -P fridge_password
```

### Проверка API:
```bash
curl http://localhost:3000/api/status
curl http://localhost:3000/api/history?hours=24
```

### Тестирование отправки команды:
```bash
mosquitto_pub -h localhost -t 'fridge/smart-fridge-001/config' \
  -m '{"targetTempFridge": 5.0}' \
  -u fridge_user -P fridge_password
```

## 5. Мобильное приложение (пример подключения)

### WebSocket подключение:
```javascript
const ws = new WebSocket('ws://localhost:3001');

ws.onmessage = (event) => {
  const data = JSON.parse(event.data);
  if (data.type === 'temperature_update') {
    console.log('Температура:', data.data);
  }
  if (data.type === 'alert') {
    alert(data.data.message);
  }
};
```

### REST API запрос:
```javascript
// Изменение настроек
fetch('http://localhost:3000/api/settings', {
  method: 'POST',
  headers: { 'Content-Type': 'application/json' },
  body: JSON.stringify({
    targetTempFridge: 4.5,
    targetTempFreezer: -20
  })
});
```

## Структура проекта

```
smart-fridge/
├── hardware/           # Схема подключения
│   └── wiring.md
├── firmware/           # Прошивка ESP32
│   ├── src/
│   │   └── main.cpp
│   └── platformio.ini
├── backend/            # Серверная часть
│   ├── src/
│   │   └── index.js
│   ├── package.json
│   └── .env.example
├── frontend/           # Фронтенд (опционально)
├── docs/               # Документация
└── README.md
```

## API Endpoints

| Метод | Endpoint | Описание |
|-------|----------|----------|
| GET | `/api/status` | Текущий статус холодильника |
| GET | `/api/history?hours=24` | История температур |
| POST | `/api/settings` | Обновить настройки |
| POST | `/api/compressor` | Вкл/выкл компрессор |
| GET | `/api/alerts?unread=true` | Получить уведомления |
| PUT | `/api/alerts/:id/read` | Пометить как прочитанное |
| GET | `/api/stats?days=7` | Статистика за период |

## Формат сообщений MQTT

### Статус (device → server):
```json
{
  "deviceId": "smart-fridge-001",
  "timestamp": 1234567890,
  "tempFridge": 4.2,
  "tempFreezer": -18.5,
  "targetTempFridge": 4.0,
  "targetTempFreezer": -18.0,
  "compressorOn": true,
  "lightOn": false,
  "wifiRSSI": -65,
  "freeHeap": 150000
}
```

### Команда (server → device):
```json
{
  "targetTempFridge": 5.0,
  "targetTempFreezer": -20.0,
  "hysteresis": 1.5,
  "compressor": false
}
```

## Устранение проблем

### ESP32 не подключается к WiFi:
- Проверьте SSID и пароль в конфигурации
- Убедитесь что сеть 2.4GHz (ESP32 не поддерживает 5GHz)

### Нет связи по MQTT:
- Проверьте что брокер запущен: `systemctl status mosquitto`
- Проверьте лог: `journalctl -u mosquitto`
- Убедитесь что пользователь существует

### Ошибки базы данных:
- Удалите файл `fridge.db` и перезапустите сервер
- Проверьте права доступа к папке
