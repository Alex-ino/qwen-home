/**
 * Smart Fridge Backend
 * Сервер для управления умным холодильником
 * 
 * Функции:
 * - MQTT брокер для связи с устройством
 * - WebSocket для реального времени
 * - REST API для мобильного приложения
 * - SQLite база данных для истории
 * - Система уведомлений
 */

const express = require('express');
const mqtt = require('mqtt');
const { WebSocketServer } = require('ws');
const http = require('http');
const cors = require('cors');
const sqlite3 = require('sqlite3').verbose();
const cron = require('node-cron');
require('dotenv').config();

// Конфигурация
const CONFIG = {
  MQTT_BROKER: process.env.MQTT_BROKER || 'mqtt://localhost:1883',
  MQTT_USER: process.env.MQTT_USER || 'fridge_user',
  MQTT_PASS: process.env.MQTT_PASS || 'fridge_password',
  PORT: process.env.PORT || 3000,
  WS_PORT: process.env.WS_PORT || 3001,
  DB_PATH: process.env.DB_PATH || './fridge.db'
};

// Инициализация Express
const app = express();
app.use(cors());
app.use(express.json());

// HTTP сервер
const server = http.createServer(app);

// WebSocket сервер
const wss = new WebSocketServer({ port: CONFIG.WS_PORT });
console.log(`WebSocket сервер запущен на порту ${CONFIG.WS_PORT}`);

// Хранилище клиентов WebSocket
const wsClients = new Set();

// Обработка WebSocket подключений
wss.on('connection', (ws) => {
  console.log('Новое WebSocket подключение');
  wsClients.add(ws);
  
  ws.on('close', () => {
    console.log('WebSocket подключение закрыто');
    wsClients.delete(ws);
  });
  
  ws.on('error', (error) => {
    console.error('WebSocket ошибка:', error);
    wsClients.delete(ws);
  });
});

// Функция отправки данных всем клиентам
function broadcastToClients(data) {
  const message = JSON.stringify(data);
  wsClients.forEach(client => {
    if (client.readyState === 1) { // OPEN
      client.send(message);
    }
  });
}

// Инициализация базы данных
const db = new sqlite3.Database(CONFIG.DB_PATH, (err) => {
  if (err) {
    console.error('Ошибка подключения к БД:', err);
  } else {
    console.log('Подключено к SQLite базе данных');
    initializeDatabase();
  }
});

function initializeDatabase() {
  // Таблица температур
  db.run(`
    CREATE TABLE IF NOT EXISTS temperature_history (
      id INTEGER PRIMARY KEY AUTOINCREMENT,
      device_id TEXT,
      temp_fridge REAL,
      temp_freezer REAL,
      target_fridge REAL,
      target_freezer REAL,
      compressor_on INTEGER,
      timestamp DATETIME DEFAULT CURRENT_TIMESTAMP
    )
  `);
  
  // Таблица настроек
  db.run(`
    CREATE TABLE IF NOT EXISTS settings (
      key TEXT PRIMARY KEY,
      value TEXT
    )
  `);
  
  // Таблица уведомлений
  db.run(`
    CREATE TABLE IF NOT EXISTS alerts (
      id INTEGER PRIMARY KEY AUTOINCREMENT,
      type TEXT,
      message TEXT,
      is_read INTEGER DEFAULT 0,
      created_at DATETIME DEFAULT CURRENT_TIMESTAMP
    )
  `);
  
  console.log('База данных инициализирована');
}

// Подключение к MQTT брокеру
const mqttClient = mqtt.connect(CONFIG.MQTT_BROKER, {
  username: CONFIG.MQTT_USER,
  password: CONFIG.MQTT_PASS,
  clientId: 'fridge-backend-' + Math.random().toString(16).substr(2, 8)
});

mqttClient.on('connect', () => {
  console.log('Подключено к MQTT брокеру');
  
  // Подписка на топики
  mqttClient.subscribe('fridge/+/status', (err) => {
    if (!err) {
      console.log('Подписан на топики статуса');
    }
  });
});

mqttClient.on('message', (topic, message) => {
  try {
    const data = JSON.parse(message.toString());
    console.log(`MQTT сообщение [${topic}]:`, data);
    
    // Сохранение в базу данных
    if (topic.includes('/status')) {
      saveTemperatureData(data);
      
      // Отправка через WebSocket
      broadcastToClients({
        type: 'temperature_update',
        data: data
      });
      
      // Проверка на критические значения
      checkAlerts(data);
    }
  } catch (error) {
    console.error('Ошибка обработки MQTT сообщения:', error);
  }
});

// Сохранение данных о температуре
function saveTemperatureData(data) {
  const stmt = db.prepare(`
    INSERT INTO temperature_history 
    (device_id, temp_fridge, temp_freezer, target_fridge, target_freezer, compressor_on)
    VALUES (?, ?, ?, ?, ?, ?)
  `);
  
  stmt.run(
    data.deviceId,
    data.tempFridge,
    data.tempFreezer,
    data.targetTempFridge,
    data.targetTempFreezer,
    data.compressorOn ? 1 : 0
  );
  stmt.finalize();
}

// Проверка на критические значения
function checkAlerts(data) {
  const alerts = [];
  
  // Слишком высокая температура в холодильнике
  if (data.tempFridge > 8) {
    alerts.push({
      type: 'warning',
      message: `Внимание! Высокая температура в холодильнике: ${data.tempFridge}°C`
    });
  }
  
  // Слишком высокая температура в морозилке
  if (data.tempFreezer > -10) {
    alerts.push({
      type: 'critical',
      message: `Критично! Высокая температура в морозилке: ${data.tempFreezer}°C`
    });
  }
  
  // Слишком низкая температура в холодильнике
  if (data.tempFridge < -2) {
    alerts.push({
      type: 'warning',
      message: `Внимание! Низкая температура в холодильнике: ${data.tempFridge}°C`
    });
  }
  
  // Сохранение уведомлений
  alerts.forEach(alert => {
    db.run(
      'INSERT INTO alerts (type, message) VALUES (?, ?)',
      [alert.type, alert.message]
    );
    
    // Отправка уведомления клиентам
    broadcastToClients({
      type: 'alert',
      data: alert
    });
    
    console.log(`ALERT [${alert.type}]: ${alert.message}`);
  });
}

// ==================== REST API ====================

// Получить текущий статус
app.get('/api/status', (req, res) => {
  db.get(
    'SELECT * FROM temperature_history ORDER BY timestamp DESC LIMIT 1',
    (err, row) => {
      if (err) {
        res.status(500).json({ error: err.message });
      } else {
        res.json(row || {});
      }
    }
  );
});

// Получить историю температур
app.get('/api/history', (req, res) => {
  const hours = req.query.hours || 24;
  db.all(
    'SELECT * FROM temperature_history WHERE timestamp >= datetime("now", ?) ORDER BY timestamp DESC',
    [`-${hours} hours`],
    (err, rows) => {
      if (err) {
        res.status(500).json({ error: err.message });
      } else {
        res.json(rows);
      }
    }
  );
});

// Обновить настройки
app.post('/api/settings', (req, res) => {
  const { targetTempFridge, targetTempFreezer, hysteresis } = req.body;
  
  // Публикация команды в MQTT
  const command = {};
  if (targetTempFridge !== undefined) command.targetTempFridge = targetTempFridge;
  if (targetTempFreezer !== undefined) command.targetTempFreezer = targetTempFreezer;
  if (hysteresis !== undefined) command.hysteresis = hysteresis;
  
  mqttClient.publish('fridge/smart-fridge-001/config', JSON.stringify(command));
  
  res.json({ success: true, message: 'Настройки отправлены' });
});

// Управление компрессором вручную
app.post('/api/compressor', (req, res) => {
  const { enabled } = req.body;
  
  mqttClient.publish(
    'fridge/smart-fridge-001/config',
    JSON.stringify({ compressor: enabled })
  );
  
  res.json({ success: true, message: `Компрессор ${enabled ? 'включен' : 'выключен'}` });
});

// Получить уведомления
app.get('/api/alerts', (req, res) => {
  const unread = req.query.unread === 'true';
  const query = unread 
    ? 'SELECT * FROM alerts WHERE is_read = 0 ORDER BY created_at DESC'
    : 'SELECT * FROM alerts ORDER BY created_at DESC';
  
  db.all(query, (err, rows) => {
    if (err) {
      res.status(500).json({ error: err.message });
    } else {
      res.json(rows);
    }
  });
});

// Пометить уведомления как прочитанные
app.put('/api/alerts/:id/read', (req, res) => {
  db.run(
    'UPDATE alerts SET is_read = 1 WHERE id = ?',
    [req.params.id],
    (err) => {
      if (err) {
        res.status(500).json({ error: err.message });
      } else {
        res.json({ success: true });
      }
    }
  );
});

// Статистика за период
app.get('/api/stats', (req, res) => {
  const days = req.query.days || 7;
  
  db.all(`
    SELECT 
      DATE(timestamp) as date,
      AVG(temp_fridge) as avg_fridge,
      AVG(temp_freezer) as avg_freezer,
      MAX(temp_fridge) as max_fridge,
      MIN(temp_fridge) as min_fridge,
      SUM(compressor_on) as compressor_minutes
    FROM temperature_history 
    WHERE timestamp >= datetime("now", ?)
    GROUP BY DATE(timestamp)
    ORDER BY date DESC
  `, [`-${days} days`], (err, rows) => {
    if (err) {
      res.status(500).json({ error: err.message });
    } else {
      res.json(rows);
    }
  });
});

// Запуск сервера
server.listen(CONFIG.PORT, () => {
  console.log(`REST API сервер запущен на порту ${CONFIG.PORT}`);
});

// Ежедневная очистка старых записей
cron.schedule('0 2 * * *', () => {
  console.log('Очистка старых записей...');
  db.run('DELETE FROM temperature_history WHERE timestamp < datetime("now", "-30 days")');
  db.run('DELETE FROM alerts WHERE created_at < datetime("now", "-7 days") AND is_read = 1');
});

console.log('\n=== Smart Fridge Backend запущен ===');
console.log(`REST API: http://localhost:${CONFIG.PORT}`);
console.log(`WebSocket: ws://localhost:${CONFIG.WS_PORT}`);
