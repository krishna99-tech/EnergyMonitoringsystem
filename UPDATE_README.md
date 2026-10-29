# ELFORGE Energy Monitoring System — System Documentation & Update Guide

This document details the architecture, calculation methodology, API endpoints, and newly completed features for the **ELFORGE Energy Monitoring System**.

---

## 1. Handover Requirements Status (Completed)

The three pending requirements requested at handover have been verified and integrated into the application:

| Requirement | Implementation Details | Status |
| :--- | :--- | :--- |
| **1. Shift-Wise Cumulative Energy Data** | • Real-time shift classification into **Shift A** (06:30–14:30), **Shift B** (14:30–22:30), and **Shift C** (22:30–06:30).<br>• Backend calculation of shift consumption using delta accumulation per meter.<br>• Dedicated KPI cards on the dashboard and reporting pages displaying Shift A, Shift B, Shift C, and Total kWh.<br>• Table summary footer (`<tfoot>`) showing individual shift totals. | **Completed** |
| **2. Working Graph** | • Embedded responsive **Chart.js** graphs on the main **Dashboard** (`/dashboard`):<br>&nbsp;&nbsp;1. **Shift Distribution Bar Chart**: Visual comparison of Shift A, B, and C consumption.<br>&nbsp;&nbsp;2. **Consumption Timeline Chart**: Real-time interval energy trend curve.<br>• Trends page (`/trends`): Fixed initial load bug with auto-recovery to latest recorded dates. | **Completed** |
| **3. Shift-Wise & Total Energy Dashboard** | • **Dashboard (`/dashboard`)**: Full shift-wise and total energy analytics combined with live telemetry.<br>• **Reports Hub (`/reports`)**: Complete analytical dashboard with daily shift stacked bars, shift share ratio doughnut chart, breakdown table, and Excel/CSV exports.<br>• **Devices Overview (`/`)**: Floating shift summary modal with side-by-side shift metrics. | **Completed** |

---

## 2. Energy Calculation Methodology

The system reads the **Cumulative Energy (kWh)** register from the **MFM384 Energy Meter**. This counter is a lifetime odometer value that does not reset daily.

### The Algorithm:
1. **Sort Chronologically**: Readings for the selected timeframe and node are sorted from oldest to newest:
   $$\text{Delta} = \text{Reading}_{current} - \text{Reading}_{previous}$$
2. **Filter & Accumulate**:
   - If $0 < \text{Delta} < 10000$: Power was consumed during this interval. The difference is added to the running total and attributed to the current reading's shift.
   - If $\text{Delta} \le 0$ or $\text{Delta} \ge 10000$: The reading is treated as a meter reset, communication glitch, or rollover and is ignored to prevent corrupted totals.
3. **Shift Windows**:

| Shift | Start Time | End Time |
| :--- | :--- | :--- |
| **Shift A** (Morning) | 06:30 (06:30 AM) | 14:29:59 (02:29 PM) |
| **Shift B** (Evening) | 14:30 (02:30 PM) | 22:29:59 (10:29 PM) |
| **Shift C** (Night) | 22:30 (10:30 PM) | 06:29:59 (06:29 AM) |

---

## 3. Application Pages & Interfaces

### 1. Machine Selector (`/`)
* Grid of monitored machines (Yellow Line, Blue Line, Red Line, etc.) mapped via [devices.json](file:///c:/Users/vakav/virtualpy/elforge_updating/devices.json).
* Online/offline indicator based on packet heartbeat.
* Floating shift calculation panel with Shift A, B, C, and Total kWh.

### 2. Live Dashboard (`/dashboard`)
* **Live Connection & Heartbeat Badge**: Real-time status badge (Online with WiFi RSSI, Modbus Timeout Warning, or Offline).
* **Shift Consumption KPI Cards**: Shift A, Shift B, Shift C, and Total Consumed kWh.
* **Current Shift Badge**: Live indicator showing active shift window based on server time.
* **Shift Distribution Chart**: Bar chart comparing Shift A, B, and C consumption.
* **Energy Timeline Chart**: Step-by-step energy consumption trend.
* **Electrical Parameters**: Phase voltages ($V_{RN}, V_{YN}, V_{BN}$), line voltages ($V_{RY}, V_{YB}, V_{BR}$), phase currents ($I_R, I_Y, I_B$), frequency, and power factor.
* **Live / Manual Table**: Tabular log with a summary `<tfoot>` showing total period consumption.

### 3. Analytics & Reports Dashboard (`/reports`)
* Dedicated shift analytics hub.
* Filters by Machine Node, Shift, and Date Range with quick presets (*Full History*, *Today*, *Last 7 Days*, *Last 30 Days*).
* **Daily Shift Stacked Bar Chart**: Day-by-day comparison of Shift A, B, and C.
* **Shift Share Doughnut Chart**: Percentage distribution of consumed energy.
* **Detailed Breakdown Table**: Date-wise records with shift totals and % shares.
* **One-Click Exports**: Direct download of formatted CSV and Excel spreadsheets.

### 4. Trend Charts (`/trends`)
* 12 time-series line graphs tracking individual electrical parameters ($V, I, f, PF, \text{kWh}$).
* Automatic fallback to latest available data if no packets were received in the last 24 hours.

### 5. Dial Gauges (`/gauges`)
* Analog dial gauges for phase voltages, currents, frequency, and power factor with threshold alarms.

---

## 4. API Endpoints Reference

### Shift & Energy Analytics
* **`GET / POST /api/energy/shifts`**:
  * **Parameters**: `ip`, `slave`, `shift` (`ALL`, `A`, `B`, `C`), `start_date`, `end_date`.
  * **Returns**:
    ```json
    {
      "ok": true,
      "device_ip": "1",
      "device_name": "Yellow Line",
      "current_shift": "B",
      "total_energy": 36.0,
      "shift_a": 20.0,
      "shift_b": 16.0,
      "shift_c": 0.0,
      "daily_breakdown": [
        { "date": "2026-04-10", "A": 20.0, "B": 16.0, "C": 0.0, "total": 36.0 }
      ],
      "timeline": [
        { "timestamp": "2026-04-10 17:35:10", "shift": "B", "diff": 0.02, "power": 18.5, "energy": 1016.0 }
      ]
    }
    ```
* **`GET / POST /api/report/summary`**:
  * Returns `{ total_energy, shift_a, shift_b, shift_c, count }` for any date range and machine filter.

### Telemetry & Real-Time Streams
* **`GET /api/device/status/<device_id>`**: Real-time heartbeat health check returning `{ status: "ONLINE"|"MODBUS_TIMEOUT"|"OFFLINE", rssi, modbus_ok, uptime_sec, seconds_ago }`.
* **`GET /api/devices`**: List of all configured devices with last seen timestamps, WiFi RSSI, Modbus status, and active status.
* **`GET /api/latest?ip=<id>`**: Latest instantaneous telemetry reading for a node.
* **`GET /api/latest/all`**: Latest readings for all connected slave meters.
* **`GET /api/raw`**: Paginated raw logs with optional `shift`, `start_date`, and `end_date` filters.
* **`GET /api/stream?ip=<id>`**: Server-Sent Events (SSE) stream pushing updates to connected clients.

### Export Services
* **`POST /api/export/csv`**: Generates sectioned CSV files with device headers, data rows, per-slave summaries, and global shift summaries.
* **`POST /api/export/excel`**: Generates native `.xlsx` workbooks containing full analytical records.

---

## 5. Running the Application

### 1. Start Web Server
```powershell
python app.py
```
* Web Dashboard: `http://localhost:5002`
* UDP Receiver: Listening on `0.0.0.0:50004`

### 2. Start Sensor Simulator (Testing)
To simulate live meter traffic over UDP:
```powershell
python simulate_sensors.py
```

---

_ELFORGE Energy Monitoring System — Maintained by Advanced Energy Engineering._

