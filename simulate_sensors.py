import socket
import json
import time
import random

# Target IP where app.py is running. Uses 127.0.0.1 for local and network IP
UDP_TARGETS = ["127.0.0.1", "192.168.29.139"]
UDP_PORT = 50004  # Synced with app.py and main.ino (port 50004)

# Devices to simulate
ALL_DEVICES = {
    "2": "Blue Line",
    "3": "Red Line",
    "4": "Green Line"
}

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
state = {}

def simulate_device(ip):
    if ip not in state:
        state[ip] = {
            "energy": random.uniform(100.0, 500.0)
        }
    
    # Increment cumulative energy
    state[ip]["energy"] += random.uniform(0.01, 0.05)
    
    vr = random.uniform(225.0, 235.0)
    vy = random.uniform(225.0, 235.0)
    vb = random.uniform(225.0, 235.0)
    
    ry = random.uniform(395.0, 410.0)
    yb = random.uniform(395.0, 410.0)
    br = random.uniform(395.0, 410.0)
    
    ir = random.uniform(5.0, 50.0)
    iy = random.uniform(5.0, 50.0)
    ib = random.uniform(5.0, 50.0)
    
    total_power = ((ir * vr) + (iy * vy) + (ib * vb)) * 0.95 / 1000 
    
    payload = {
        "slave_id": int(ip) if ip.isdigit() else 1,
        "energy": round(state[ip]["energy"], 2),
        "power": round(total_power, 2),
        "power_factor": round(random.uniform(0.9, 0.99), 3),
        "frequency": round(random.uniform(49.8, 50.2), 2),
        "vr": round(vr, 1),
        "vy": round(vy, 1),
        "vb": round(vb, 1),
        "ry": round(ry, 1),
        "yb": round(yb, 1),
        "br": round(br, 1),
        "ir": round(ir, 2),
        "iy": round(iy, 2),
        "ib": round(ib, 2),
        "device_ip": ip
    }
    return payload

def send_packet(target_ip, port, message):
    try:
        sock.sendto(message, (target_ip, port))
    except Exception:
        pass

def main():
    print(f"Starting Multi-Device Sensor Simulator on UDP port {UDP_PORT}")
    print("Press CTRL+C to quit.\n")
    
    while True:
        for ip, name in ALL_DEVICES.items():
            payload = simulate_device(ip)
            message = json.dumps(payload).encode('utf-8')
            for target in UDP_TARGETS:
                send_packet(target, UDP_PORT, message)
            print(f"[Sim] Sent sensor telemetry for {name} (Device {ip}) - Energy: {payload['energy']} kWh")
                
        print("Waiting 5 seconds for the next cycle...\n")
        time.sleep(5)

if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        print("\nSimulator stopped.")
