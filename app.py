from flask import Flask, render_template, jsonify, request, send_file, Response, stream_with_context
import sqlite3
from datetime import datetime
import csv
import io
from openpyxl import Workbook
import json
import time
import threading
import socket
import os


# ================= APP CONFIG =================
app = Flask(__name__)
DB_PATH = "energy.db"
UDP_PORT = 50003
webport = 5001
UDP_DEBUG = True

# ================= SAFE FLOAT CONVERTER =================
def to_float(v, default=0.0):
    try:
        return float(v)
    except (TypeError, ValueError):
        return default


def get_any(data, *keys):
    for k in keys:
        if k in data:
            return data.get(k)
    return None


def as_dashboard_row_upper(row):
    raw = dict(row) if row else {}
    return {
        "SLAVE_ID":     raw.get("slave_id"),
        "ENERGY":       raw.get("energy"),
        "POWER":        raw.get("power"),
        "POWER_FACTOR": raw.get("power_factor"),
        "FREQUENCY":    raw.get("frequency"),
        "VR": raw.get("vr"),
        "VY": raw.get("vy"),
        "VB": raw.get("vb"),
        "RY": raw.get("ry"),
        "YB": raw.get("yb"),
        "BR": raw.get("br"),
        "IR": raw.get("ir"),
        "IY": raw.get("iy"),
        "IB": raw.get("ib"),
        "SHIFT":     raw.get("shift"),
        "TIMESTAMP": raw.get("timestamp"),
        "DEVICE_IP": raw.get("device_ip"),
    }


CSV_EXPORT_COLUMNS = [
    "TIMESTAMP", "DEVICE_NAME", "SHIFT", "SLAVE_ID", "ENERGY", "POWER_FACTOR", "FREQUENCY",
    "VR", "VY", "VB", "RY", "YB", "BR",
    "IR", "IY", "IB"
]


def as_csv_row_upper(row, mapping=None):
    raw = dict(row) if row else {}
    mapping = mapping or {}
    
    # Try logic ID (slave_id) first then device_ip
    sid_val = get_any(raw, "SLAVE_ID", "slave_id")
    ip_val  = get_any(raw, "DEVICE_IP", "device_ip")

    sid_str = str(sid_val) if sid_val is not None else ""
    ip_str  = str(ip_val)  if ip_val is not None else ""

    name = mapping.get(sid_str) or mapping.get(ip_str) or "Unknown Device"

    return {
        "TIMESTAMP":    get_any(raw, "TIMESTAMP", "timestamp"),
        "DEVICE_NAME":  name,
        "SHIFT":        get_any(raw, "SHIFT", "shift"),
        "SLAVE_ID":     get_any(raw, "SLAVE_ID", "slave_id"),
        "ENERGY":       get_any(raw, "ENERGY", "energy"),
        "POWER_FACTOR": get_any(raw, "POWER_FACTOR", "power_factor"),
        "FREQUENCY":    get_any(raw, "FREQUENCY", "frequency"),
        "VR": get_any(raw, "VR", "vr"),
        "VY": get_any(raw, "VY", "vy"),
        "VB": get_any(raw, "VB", "vb"),
        "RY": get_any(raw, "RY", "ry"),
        "YB": get_any(raw, "YB", "yb"),
        "BR": get_any(raw, "BR", "br"),
        "IR": get_any(raw, "IR", "ir"),
        "IY": get_any(raw, "IY", "iy"),
        "IB": get_any(raw, "IB", "ib"),
    }


# ================= DB MANAGEMENT =================
def get_db_connection():
    conn = sqlite3.connect(DB_PATH, check_same_thread=False)
    conn.row_factory = sqlite3.Row
    return conn


# ================ SHIFT CALCULATION =================
def get_shift_from_time(dt):
    h = dt.hour
    if 6 <= h < 14:
        return "A"
    elif 14 <= h < 22:
        return "B"
    else:
        return "C"


# -------------------------------------------------------
# insert_energy_row — handles ONE meter's flat dict.
# slave_id is passed explicitly (1, 2, or None for legacy).
# -------------------------------------------------------
def insert_energy_row(data, device_ip=None, slave_id=None):
    now = datetime.now()
    shift = get_shift_from_time(now)
    with get_db_connection() as conn:
        conn.execute("""
            INSERT INTO energy_data (
                slave_id,
                energy, power, power_factor, frequency,
                vr, vy, vb,
                ry, yb, br,
                ir, iy, ib,
                shift, timestamp, device_ip
            ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
        """, (
            slave_id,
            to_float(get_any(data, "energy", "Energy", "total_energy", "kWh")),
            to_float(data.get("power")),
            to_float(data.get("power_factor")),
            to_float(data.get("frequency")),
            to_float(data.get("vr")),
            to_float(data.get("vy")),
            to_float(data.get("vb")),
            to_float(get_any(data, "ry", "vry", "v_ry")),
            to_float(get_any(data, "yb", "vyb", "v_yb")),
            to_float(get_any(data, "br", "vbr", "v_br")),
            to_float(data.get("ir")),
            to_float(data.get("iy")),
            to_float(data.get("ib")),
            shift,
            now.strftime("%Y-%m-%d %H:%M:%S"),
            device_ip
        ))
        conn.commit()
    return shift


# -------------------------------------------------------
# parse_and_insert — detects payload format:
#
#   NESTED (new dual-slave master):
#     { "slave1": { "energy": ..., "vr": ... },
#       "slave2": { "energy": ..., "vr": ... } }
#
#   FLAT (old single-meter master / legacy):
#     { "energy": ..., "vr": ..., ... }
#
# Both formats are stored with a slave_id so existing
# API queries still work unchanged.
# -------------------------------------------------------
SLAVE_KEYS = {
    "slave1": 1,
    "slave2": 2,
    "slave3": 3,   # ready for future expansion
    "slave4": 4,
}

def parse_and_insert(payload, device_ip):
    inserted = []

    # Check if any top-level key matches a known slave label
    is_nested = any(k in payload for k in SLAVE_KEYS)

    if is_nested:
        for label, slave_id in SLAVE_KEYS.items():
            if label in payload:
                meter_data = payload[label]
                if isinstance(meter_data, dict):
                    shift = insert_energy_row(meter_data, device_ip, slave_id)
                    inserted.append((slave_id, shift))
                    if UDP_DEBUG:
                        print(f"[UDP] Inserted slave_id={slave_id} shift={shift}")
    else:
        # Legacy flat packet — treat as slave_id=None (or 1 if you prefer)
        shift = insert_energy_row(payload, device_ip, slave_id=None)
        inserted.append((None, shift))
        if UDP_DEBUG:
            print(f"[UDP] Inserted legacy flat row shift={shift}")

    return inserted


# ================= UDP LISTENER =================
def start_udp_listener():
    def _run():
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        sock.bind(("0.0.0.0", UDP_PORT))
        if UDP_DEBUG:
            print(f"[UDP] Listening on 0.0.0.0:{UDP_PORT}")
        while True:
            try:
                data, addr = sock.recvfrom(4096)
                raw_text = data.decode("utf-8", errors="replace").strip()
                if UDP_DEBUG:
                    print(f"[UDP] Packet from {addr[0]}:{addr[1]} | bytes={len(data)}")
                    print(f"[UDP] Raw: {raw_text}")

                payload = json.loads(raw_text)
                ip_address = payload.get("device_ip", str(addr[0]))

                inserted = parse_and_insert(payload, ip_address)

                if UDP_DEBUG:
                    # Print precisely what was inserted for energy to help debugging
                    print(f"[UDP] Rows inserted: {inserted} | Raw Energy: {payload.get('energy')} / {payload.get('slave1',{}).get('energy')}")

            except Exception as e:
                if UDP_DEBUG:
                    print(f"[UDP] Error: {e}")
                continue

    t = threading.Thread(target=_run, daemon=True)
    t.start()


# ================= DB INIT =================
def ensure_columns(conn, columns):
    existing = {row["name"] for row in conn.execute("PRAGMA table_info(energy_data)")}
    for name, col_def in columns.items():
        if name not in existing:
            conn.execute(f"ALTER TABLE energy_data ADD COLUMN {name} {col_def}")


def init_db():
    with get_db_connection() as conn:
        conn.execute("""
            CREATE TABLE IF NOT EXISTS energy_data (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                slave_id INTEGER,          -- 1, 2, 3 … or NULL for legacy rows
                energy REAL,
                power REAL,
                power_factor REAL,
                frequency REAL,
                vr REAL, vy REAL, vb REAL,
                ry REAL, yb REAL, br REAL,
                ir REAL, iy REAL, ib REAL,
                shift TEXT,
                timestamp DATETIME DEFAULT CURRENT_TIMESTAMP,
                device_ip TEXT
            )
        """)
        # Migrate existing DB that lacks newer columns
        ensure_columns(conn, {
            "slave_id":  "INTEGER",
            "ry":        "REAL",
            "yb":        "REAL",
            "br":        "REAL",
            "device_ip": "TEXT",
        })
        conn.commit()


init_db()


# ================= ROUTES =================
@app.route("/")
def index():
    return render_template("devices.html")

@app.route("/dashboard")
def dashboard():
    return render_template("dashboard.html")

@app.route("/gauges")
def gauges():
    return render_template("gauges.html")

@app.route("/trends")
def trends():
    return render_template("trends.html")

@app.route("/reports")
def reports():
    return render_template("reports.html")

@app.route("/alerts")
def alerts():
    return render_template("alerts.html")

@app.route("/settings")
def settings():
    return render_template("settings.html")


DEVICE_MAPPING_FILE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "devices.json")

@app.route("/api/devices")
def api_devices():
    mapping = {}
    devices = []
    try:
        import os
        if os.path.exists(DEVICE_MAPPING_FILE):
            with open(DEVICE_MAPPING_FILE, "r") as f:
                mapping = json.load(f)
    except:
        mapping = {}
    now = datetime.now()
    with get_db_connection() as conn:
        for ident, name in mapping.items():
            # Try to convert ident to int if it looks like one for better DB matching
            search_param = ident
            try:
                search_param = int(ident)
            except:
                pass

            # Check for latest data matching this ID (either as device_ip or slave_id)
            query = "SELECT timestamp FROM energy_data WHERE (device_ip = ? OR slave_id = ?) ORDER BY timestamp DESC LIMIT 1"
            row = conn.execute(query, (ident, search_param)).fetchone()
            
            is_active = False
            last_seen = "Never"
            if row:
                last_seen_str = row["timestamp"]
                last_seen = last_seen_str
                try:
                    ls_dt = datetime.strptime(last_seen_str, "%Y-%m-%d %H:%M:%S")
                    diff = (now - ls_dt).total_seconds()
                    # More lenient: Active if seen in last 10 mins or if timestamp is in slight future (drift)
                    if -300 < diff < 600: 
                        is_active = True
                except:
                    pass
            
            devices.append({
                "device_ip": ident, 
                "name": name, 
                "active": is_active,
                "last_seen": last_seen,
                "diff_sec": (now - ls_dt).total_seconds() if row and 'ls_dt' in locals() else None
            })
    return jsonify(devices)


# -------------------------------------------------------
# All API endpoints accept an optional ?slave=1 or ?slave=2
# query param so the frontend can filter per meter.
# If omitted, the latest row across all slaves is returned.
# -------------------------------------------------------

def _slave_where(params, slave_id_str, prefix=" WHERE "):
    """Append slave_id filter if provided. Returns (extra_sql, updated_params)."""
    if slave_id_str:
        try:
            sid = int(slave_id_str)
            return prefix + "slave_id = ?", params + [sid]
        except ValueError:
            pass
    return "", params


@app.route("/api/data")
def api_data():
    device_ip = request.args.get("ip")
    slave_id  = request.args.get("slave")

    query = "SELECT * FROM energy_data"
    params = []
    where = []

    if device_ip:
        where.append("(device_ip = ? OR slave_id = ?)")
        params.extend([device_ip, device_ip])
    if slave_id:
        try:
            where.append("slave_id = ?")
            params.append(int(slave_id))
        except ValueError:
            pass

    if where:
        query += " WHERE " + " AND ".join(where)
    query += " ORDER BY timestamp DESC LIMIT 1"

    with get_db_connection() as conn:
        row = conn.execute(query, params).fetchone()
    return jsonify(as_dashboard_row_upper(row)) if row else jsonify({})


@app.route("/api/raw")
def api_raw():
    shift     = request.args.get("shift", "ALL")
    start     = request.args.get("start_date")
    end       = request.args.get("end_date")
    device_ip = request.args.get("ip")
    slave_id  = request.args.get("slave")
    try:
        limit = int(request.args.get("limit", 500))
    except (TypeError, ValueError):
        limit = 500
    limit = max(1, min(limit, 10000))

    columns = [
        "slave_id", "energy", "power", "power_factor", "frequency",
        "vr", "vy", "vb", "ry", "yb", "br",
        "ir", "iy", "ib", "shift", "timestamp",
    ]

    query = f"SELECT {', '.join(columns)} FROM energy_data"
    params = []
    where = []

    if start and end:
        where.append("DATE(timestamp) BETWEEN ? AND ?")
        params.extend([start, end])
    if shift != "ALL":
        where.append("shift = ?")
        params.append(shift)
    if device_ip:
        where.append("(device_ip = ? OR slave_id = ?)")
        params.extend([device_ip, device_ip])
    if slave_id:
        try:
            where.append("slave_id = ?")
            params.append(int(slave_id))
        except ValueError:
            pass

    if where:
        query += " WHERE " + " AND ".join(where)
    query += " ORDER BY timestamp DESC LIMIT ?"
    params.append(limit)

    with get_db_connection() as conn:
        rows = conn.execute(query, params).fetchall()

    return jsonify([as_dashboard_row_upper(r) for r in rows])


@app.route("/api/latest")
def api_latest():
    device_ip = request.args.get("ip")
    slave_id  = request.args.get("slave")

    query = "SELECT * FROM energy_data"
    params = []
    where = []

    if device_ip:
        where.append("(device_ip = ? OR slave_id = ?)")
        params.extend([device_ip, device_ip])
    if slave_id:
        try:
            where.append("slave_id = ?")
            params.append(int(slave_id))
        except ValueError:
            pass

    if where:
        query += " WHERE " + " AND ".join(where)
    query += " ORDER BY timestamp DESC LIMIT 1"

    with get_db_connection() as conn:
        row = conn.execute(query, params).fetchone()
    return jsonify(as_dashboard_row_upper(row)) if row else jsonify({})


# -------------------------------------------------------
# /api/latest/all — convenience endpoint: returns the
# latest row for EACH slave in one call.
# Frontend can use this to update all meter panels at once.
# -------------------------------------------------------
@app.route("/api/latest/all")
def api_latest_all():
    device_ip = request.args.get("ip")
    results = {}

    for label, sid in SLAVE_KEYS.items():
        query = "SELECT * FROM energy_data WHERE (slave_id = ? OR device_ip = ?)"
        params = [sid, sid]
        if device_ip:
            query += " AND (device_ip = ? OR slave_id = ?)"
            params.extend([device_ip, device_ip])
        query += " ORDER BY timestamp DESC LIMIT 1"

        with get_db_connection() as conn:
            row = conn.execute(query, params).fetchone()
        if row:
            results[label] = as_dashboard_row_upper(row)

    return jsonify(results)


@app.route("/api/stream")
def api_stream():
    shift_filter = request.args.get("shift", "ALL")
    start        = request.args.get("start_date")
    end          = request.args.get("end_date")
    device_ip    = request.args.get("ip")
    slave_id     = request.args.get("slave")

    start_dt = end_dt = None
    try:
        if start:
            start_dt = datetime.strptime(start, "%Y-%m-%d")
        if end:
            end_dt = datetime.strptime(end, "%Y-%m-%d").replace(hour=23, minute=59, second=59)
    except ValueError:
        pass

    def event_stream():
        last_ts = None
        while True:
            query = "SELECT * FROM energy_data"
            params = []
            where = []

            if device_ip:
                where.append("(device_ip = ? OR slave_id = ?)")
                params.extend([device_ip, device_ip])
            if slave_id:
                try:
                    where.append("slave_id = ?")
                    params.append(int(slave_id))
                except ValueError:
                    pass

            if where:
                query += " WHERE " + " AND ".join(where)
            query += " ORDER BY timestamp DESC LIMIT 1"

            with get_db_connection() as conn:
                row = conn.execute(query, params).fetchone()

            if row:
                data = dict(row)
                ts = data.get("timestamp")
                if ts and ts != last_ts:
                    last_ts = ts
                    if shift_filter != "ALL" and data.get("shift") != shift_filter:
                        time.sleep(1)
                        continue
                    if start_dt or end_dt:
                        try:
                            row_dt = datetime.strptime(ts, "%Y-%m-%d %H:%M:%S")
                            if start_dt and row_dt < start_dt:
                                time.sleep(1)
                                continue
                            if end_dt and row_dt > end_dt:
                                time.sleep(1)
                                continue
                        except ValueError:
                            pass
                    yield f"data: {json.dumps(as_dashboard_row_upper(data))}\n\n"
            time.sleep(1)

    return Response(stream_with_context(event_stream()), mimetype="text/event-stream")


# ================= TREND CHART API =================
@app.route("/api/raw/export")
def api_raw_export():
    shift     = request.args.get("shift", "ALL")
    start     = request.args.get("start_date")
    end       = request.args.get("end_date")
    device_ip = request.args.get("ip")
    slave_id  = request.args.get("slave")
    try:
        limit = int(request.args.get("limit", 10000))
    except (TypeError, ValueError):
        limit = 10000
    limit = max(1, min(limit, 10000))

    columns = [
        "slave_id", "energy", "power_factor", "frequency",
        "vr", "vy", "vb", "ry", "yb", "br",
        "ir", "iy", "ib", "shift", "timestamp", "device_ip"
    ]
    query = f"SELECT {', '.join(columns)} FROM energy_data"
    params = []
    where = []

    if start and end:
        where.append("DATE(timestamp) BETWEEN ? AND ?")
        params.extend([start, end])
    if shift != "ALL":
        where.append("shift = ?")
        params.append(shift)
    if device_ip:
        where.append("(device_ip = ? OR slave_id = ?)")
        params.extend([device_ip, device_ip])
    if slave_id:
        try:
            where.append("slave_id = ?")
            params.append(int(slave_id))
        except ValueError:
            pass

    if where:
        query += " WHERE " + " AND ".join(where)
    query += " ORDER BY timestamp DESC LIMIT ?"
    params.append(limit)

    with get_db_connection() as conn:
        rows = conn.execute(query, params).fetchall()

    if not rows:
        return jsonify({"error": "No data"}), 404

    # Load mapping for name lookup
    mapping = {}
    try:
        if os.path.exists(DEVICE_MAPPING_FILE):
            with open(DEVICE_MAPPING_FILE, "r") as f:
                mapping = json.load(f)
    except:
        pass

    output = io.StringIO()
    writer = csv.DictWriter(output, fieldnames=CSV_EXPORT_COLUMNS, extrasaction='ignore')
    writer.writeheader()
    writer.writerows([as_csv_row_upper(r, mapping) for r in rows])

    return send_file(
        io.BytesIO(output.getvalue().encode("utf-8")),
        mimetype="text/csv",
        as_attachment=True,
        download_name="energy_data.csv"
    )


@app.route("/api/trend/<string:trend_type>", methods=["POST"])
def api_trend(trend_type):
    req       = request.get_json(silent=True) or {}
    shift     = req.get("shift", "ALL")
    start     = req.get("start_date")
    end       = req.get("end_date")
    device_ip = req.get("ip")
    slave_id  = req.get("slave")

    if not start or not end:
        return jsonify({"ok": False, "error": "Missing date range"}), 400

    trend_type = trend_type.lower().strip()
    if trend_type == "voltage":
        columns = ["timestamp", "vr", "vy", "vb"]
    elif trend_type == "current":
        columns = ["timestamp", "ir", "iy", "ib"]
    elif trend_type == "power":
        columns = ["timestamp", "power"]
    else:
        return jsonify({"ok": False, "error": "Invalid trend type"}), 400

    query = f"""
        SELECT {', '.join(columns)}
        FROM energy_data
        WHERE DATE(timestamp) BETWEEN ? AND ?
    """
    params = [start, end]

    if shift != "ALL":
        query += " AND shift = ?"
        params.append(shift)
    if device_ip:
        query += " AND (device_ip = ? OR slave_id = ?)"
        params.extend([device_ip, device_ip])
    if slave_id:
        try:
            query += " AND slave_id = ?"
            params.append(int(slave_id))
        except ValueError:
            pass

    query += " ORDER BY timestamp ASC"

    with get_db_connection() as conn:
        rows = conn.execute(query, params).fetchall()

    return jsonify({"ok": True, "data": [dict(r) for r in rows]})


# ================= EXPORT HELPERS =================
def fetch_filtered(req_data):
    req_data  = req_data or {}
    shift     = req_data.get("shift", "ALL")
    start     = req_data.get("start_date")
    end       = req_data.get("end_date")
    device_ip = req_data.get("ip")
    slave_id  = req_data.get("slave")

    columns = [
        "slave_id", "energy", "power_factor", "frequency",
        "vr", "vy", "vb", "ry", "yb", "br",
        "ir", "iy", "ib", "shift", "timestamp",
    ]

    if not start or not end:
        return []

    query = f"SELECT {', '.join(columns)} FROM energy_data WHERE DATE(timestamp) BETWEEN ? AND ?"
    params = [start, end]

    if shift != "ALL":
        query += " AND shift = ?"
        params.append(shift)
    if device_ip:
        query += " AND (device_ip = ? OR slave_id = ?)"
        params.extend([device_ip, device_ip])
    if slave_id:
        try:
            query += " AND slave_id = ?"
            params.append(int(slave_id))
        except ValueError:
            pass

    query += " ORDER BY timestamp DESC"

    with get_db_connection() as conn:
        rows = conn.execute(query, params).fetchall()

    return [{col: row[col] for col in columns} for row in rows]


@app.route("/api/export/preview", methods=["POST"])
def export_preview():
    data = fetch_filtered(request.get_json(silent=True))
    return jsonify({"data": data[:50], "total_records": len(data)})


@app.route("/api/export/csv", methods=["POST"])
def export_csv():
    req = request.get_json(silent=True) or {}
    if isinstance(req.get("data"), list) and req["data"]:
        data = req["data"]
    else:
        data = fetch_filtered(req)
    if not data:
        return jsonify({"error": "No data"}), 404

    # Load mapping for name lookup
    mapping = {}
    try:
        if os.path.exists(DEVICE_MAPPING_FILE):
            with open(DEVICE_MAPPING_FILE, "r") as f:
                mapping = json.load(f)
    except:
        pass

    csv_rows = [as_csv_row_upper(r, mapping) for r in data]

    # Shift-wise energy consumption summary
    sorted_data = sorted(data, key=lambda x: str(get_any(x, "TIMESTAMP", "timestamp")))
    total_cons = 0.0
    shift_cons = {"A": 0.0, "B": 0.0, "C": 0.0}

    if len(sorted_data) >= 2:
        for i in range(1, len(sorted_data)):
            prev = sorted_data[i - 1]
            curr = sorted_data[i]
            p_val = to_float(get_any(prev, "ENERGY", "energy"))
            c_val = to_float(get_any(curr, "ENERGY", "energy"))
            diff = c_val - p_val
            if diff > 0:
                total_cons += diff
                s = get_any(curr, "SHIFT", "shift")
                if s in shift_cons:
                    shift_cons[s] += diff

    csv_rows.append({})
    csv_rows.append({"TIMESTAMP": "SUMMARY",    "SHIFT": "TOTAL", "ENERGY": f"{round(total_cons, 2)} kWh"})
    csv_rows.append({"TIMESTAMP": "SHIFT WISE", "SHIFT": "A",     "ENERGY": f"{round(shift_cons['A'], 2)} kWh"})
    csv_rows.append({"TIMESTAMP": "",           "SHIFT": "B",     "ENERGY": f"{round(shift_cons['B'], 2)} kWh"})
    csv_rows.append({"TIMESTAMP": "",           "SHIFT": "C",     "ENERGY": f"{round(shift_cons['C'], 2)} kWh"})

    output = io.StringIO()
    writer = csv.DictWriter(output, fieldnames=CSV_EXPORT_COLUMNS, extrasaction='ignore')
    writer.writeheader()
    writer.writerows(csv_rows)

    return send_file(
        io.BytesIO(output.getvalue().encode("utf-8")),
        mimetype="text/csv",
        as_attachment=True,
        download_name="energy_report.csv"
    )


@app.route("/api/export/excel", methods=["POST"])
def export_excel():
    data = fetch_filtered(request.get_json(silent=True))
    if not data:
        return jsonify({"error": "No data"}), 404

    wb = Workbook()
    ws = wb.active
    ws.title = "Energy Data"
    ws.append([k.upper() for k in data[0].keys()])
    for row in data:
        ws.append(list(row.values()))

    output = io.BytesIO()
    wb.save(output)
    output.seek(0)

    return send_file(
        output,
        mimetype="application/vnd.openxmlformats-officedocument.spreadsheetml.sheet",
        as_attachment=True,
        download_name="energy_report.xlsx"
    )


# ================= MAIN =================
if __name__ == "__main__":
    start_udp_listener()
    app.run(
        host="0.0.0.0",
        port=webport,
        debug=True,
        use_reloader=False,
        threaded=True
    )