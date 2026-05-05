// Пример простого React компонента для мониторинга холодильника

import React, { useState, useEffect } from 'react';

function FridgeMonitor() {
  const [status, setStatus] = useState(null);
  const [history, setHistory] = useState([]);
  const [alerts, setAlerts] = useState([]);
  const [settings, setSettings] = useState({
    targetTempFridge: 4.0,
    targetTempFreezer: -18.0
  });

  // WebSocket подключение
  useEffect(() => {
    const ws = new WebSocket('ws://localhost:3001');

    ws.onopen = () => {
      console.log('WebSocket подключен');
    };

    ws.onmessage = (event) => {
      const message = JSON.parse(event.data);
      
      if (message.type === 'temperature_update') {
        setStatus(message.data);
      }
      
      if (message.type === 'alert') {
        setAlerts(prev => [message.data, ...prev]);
        // Показываем уведомление
        if (Notification.permission === 'granted') {
          new Notification(message.data.type.toUpperCase(), {
            body: message.data.message,
            icon: '/fridge-icon.png'
          });
        }
      }
    };

    ws.onclose = () => {
      console.log('WebSocket отключен');
    };

    return () => ws.close();
  }, []);

  // Загрузка начальных данных
  useEffect(() => {
    fetchStatus();
    fetchHistory();
    fetchAlerts();
    
    // Запрос разрешения на уведомления
    if ('Notification' in window && Notification.permission === 'default') {
      Notification.requestPermission();
    }
  }, []);

  const fetchStatus = async () => {
    const res = await fetch('/api/status');
    const data = await res.json();
    setStatus(data);
  };

  const fetchHistory = async () => {
    const res = await fetch('/api/history?hours=24');
    const data = await res.json();
    setHistory(data);
  };

  const fetchAlerts = async () => {
    const res = await fetch('/api/alerts?unread=true');
    const data = await res.json();
    setAlerts(data);
  };

  const updateSettings = async () => {
    await fetch('/api/settings', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(settings)
    });
    alert('Настройки отправлены!');
  };

  const toggleCompressor = async () => {
    const newState = !status?.compressorOn;
    await fetch('/api/compressor', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ enabled: newState })
    });
  };

  const markAlertRead = async (id) => {
    await fetch(`/api/alerts/${id}/read`, { method: 'PUT' });
    setAlerts(prev => prev.filter(a => a.id !== id));
  };

  if (!status) {
    return <div>Загрузка...</div>;
  }

  return (
    <div className="fridge-monitor">
      <h1>🧊 Умный Холодильник</h1>

      {/* Текущий статус */}
      <div className="status-cards">
        <div className={`card ${status.tempFridge > 8 ? 'warning' : ''}`}>
          <h2>Холодильник</h2>
          <div className="temp">{status.tempFridge.toFixed(1)}°C</div>
          <div className="target">Цель: {status.targetTempFridge}°C</div>
        </div>

        <div className={`card ${status.tempFreezer > -10 ? 'critical' : ''}`}>
          <h2>Морозилка</h2>
          <div className="temp">{status.tempFreezer.toFixed(1)}°C</div>
          <div className="target">Цель: {status.targetTempFreezer}°C</div>
        </div>

        <div className="card">
          <h2>Статус</h2>
          <div className="compressor">
            Компрессор: {status.compressorOn ? '✅ ВКЛ' : '❌ ВЫКЛ'}
          </div>
          <div className="wifi">WiFi: {status.wifiRSSI} dBm</div>
        </div>
      </div>

      {/* Управление */}
      <div className="controls">
        <h3>Настройки</h3>
        <div className="setting">
          <label>Температура холодильника:</label>
          <input
            type="number"
            step="0.5"
            value={settings.targetTempFridge}
            onChange={(e) => setSettings({...settings, targetTempFridge: parseFloat(e.target.value)})}
          />
        </div>
        <div className="setting">
          <label>Температура морозилки:</label>
          <input
            type="number"
            step="0.5"
            value={settings.targetTempFreezer}
            onChange={(e) => setSettings({...settings, targetTempFreezer: parseFloat(e.target.value)})}
          />
        </div>
        <button onClick={updateSettings}>Применить</button>
        <button onClick={toggleCompressor} className="secondary">
          {status.compressorOn ? 'Выключить компрессор' : 'Включить компрессор'}
        </button>
      </div>

      {/* Уведомления */}
      {alerts.length > 0 && (
        <div className="alerts">
          <h3>⚠️ Уведомления ({alerts.length})</h3>
          {alerts.map(alert => (
            <div key={alert.id} className={`alert ${alert.type}`}>
              <span>{alert.message}</span>
              <button onClick={() => markAlertRead(alert.id)}>✕</button>
            </div>
          ))}
        </div>
      )}

      {/* График истории (упрощённо) */}
      <div className="history">
        <h3>История температур (24ч)</h3>
        <div className="chart">
          {history.slice(0, 10).map((record, i) => (
            <div key={i} className="chart-point">
              <span>{new Date(record.timestamp).toLocaleTimeString()}</span>
              <span>{record.temp_fridge.toFixed(1)}°C</span>
            </div>
          ))}
        </div>
      </div>

      <style jsx>{`
        .fridge-monitor {
          max-width: 800px;
          margin: 0 auto;
          padding: 20px;
          font-family: Arial, sans-serif;
        }
        
        .status-cards {
          display: grid;
          grid-template-columns: repeat(auto-fit, minmax(200px, 1fr));
          gap: 20px;
          margin: 20px 0;
        }
        
        .card {
          background: #f5f5f5;
          padding: 20px;
          border-radius: 10px;
          text-align: center;
        }
        
        .card.warning {
          background: #fff3cd;
          border: 2px solid #ffc107;
        }
        
        .card.critical {
          background: #f8d7da;
          border: 2px solid #dc3545;
        }
        
        .temp {
          font-size: 2.5em;
          font-weight: bold;
          color: #2196F3;
        }
        
        .controls {
          background: #e3f2fd;
          padding: 20px;
          border-radius: 10px;
          margin: 20px 0;
        }
        
        .setting {
          margin: 10px 0;
        }
        
        .setting label {
          display: block;
          margin-bottom: 5px;
        }
        
        .setting input {
          width: 100px;
          padding: 5px;
          margin-right: 10px;
        }
        
        button {
          background: #2196F3;
          color: white;
          border: none;
          padding: 10px 20px;
          border-radius: 5px;
          cursor: pointer;
          margin: 5px;
        }
        
        button.secondary {
          background: #757575;
        }
        
        .alerts {
          background: #ffebee;
          padding: 15px;
          border-radius: 10px;
          margin: 20px 0;
        }
        
        .alert {
          display: flex;
          justify-content: space-between;
          align-items: center;
          padding: 10px;
          margin: 5px 0;
          background: white;
          border-radius: 5px;
        }
        
        .alert.critical {
          border-left: 4px solid #dc3545;
        }
        
        .alert.warning {
          border-left: 4px solid #ffc107;
        }
        
        .history {
          margin-top: 20px;
        }
        
        .chart-point {
          display: flex;
          justify-content: space-between;
          padding: 5px;
          border-bottom: 1px solid #eee;
        }
      `}</style>
    </div>
  );
}

export default FridgeMonitor;
