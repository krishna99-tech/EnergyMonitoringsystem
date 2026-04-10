import socket
import random
import json
import time

# ================= SERVER CONFIG =================
SERVER_IP = "192.168.29.139"   # Same as ESP32 code
UDP_PORT = 50003

# Create UDP socket
sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

print("🚀 Python UDP Random Data Sender Started")

while True:
    # Generate random electrical data
    data = {
        "energy": round(random.uniform(1000, 5000), 2),
        "power_factor": round(random.uniform(0.8, 1.0), 3),
        "frequency": round(random.uniform(49.5, 50.5), 2),
        "vr": round(random.uniform(220, 240), 2),
        "vy": round(random.uniform(220, 240), 2),
        "vb": round(random.uniform(220, 240), 2),
        "ry": round(random.uniform(380, 415), 2),
        "yb": round(random.uniform(380, 415), 2),
        "br": round(random.uniform(380, 415), 2),
        "ir": round(random.uniform(0, 100), 2),
        "iy": round(random.uniform(0, 100), 2),
        "ib": round(random.uniform(0, 100), 2)
    }

    # Convert to JSON string
    json_data = json.dumps(data)

    # Send UDP packet
    sock.sendto(json_data.encode(), (SERVER_IP, UDP_PORT))

    print("📤 Sent:", json_data)

    # Wait 5 seconds (same as ESP32)
    time.sleep(5)