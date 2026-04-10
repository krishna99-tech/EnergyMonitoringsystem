import socket
import json
import time
import random

# Target IP where the app.py is running. Use "127.0.0.1" if iterating locally.
UDP_IP = "192.168.29.139" 
UDP_PORT = 50003

# Hardcoded list of ALL IP addresses you want to simulate
ALL_DEVICES = {
    "1": "Yellow Line",
    "2": "Blue Line",
    "3": "Green Line",
    "4": "Pink Line",
    "5": "White Line"
}

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

# Keep track of local state per device so things like 'cumulative energy' can rise continuously realistically
state = {}

def simulate_device(ip):
    if ip not in state:
        state[ip] = {
            "energy": random.uniform(100.0, 500.0)
        }
    
    # Increment cumulative energy
    state[ip]["energy"] += random.uniform(0.01, 0.05)
    
    # Indian standard 3-phase voltages: Line-to-Neutral approx 230v
    vr = random.uniform(225.0, 235.0)
    vy = random.uniform(225.0, 235.0)
    vb = random.uniform(225.0, 235.0)
    
    # Line-to-Line voltages approx Phase-Neutral * 1.732 (sqrt 3) -> ~ 398v
    ry = random.uniform(395.0, 410.0)
    yb = random.uniform(395.0, 410.0)
    br = random.uniform(395.0, 410.0)
    
    # Load Current (Amps) randomly fluctuating 
    ir = random.uniform(5.0, 50.0)
    iy = random.uniform(5.0, 50.0)
    ib = random.uniform(5.0, 50.0)
    
    # Estimated power formula for 3 separate single-phase resistive loads (for random calculation)
    total_power = ((ir * vr) + (iy * vy) + (ib * vb)) * 0.95 / 1000 
    
    payload = {
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
        "device_ip": ip # Allows script to override source IP identification in app.py logic
    }
    return payload

def main():
    print(f"Starting Multi-Device Sensor Simulator. Broadcasting to UDP {UDP_IP}:{UDP_PORT}")
    print("Press CTRL+C to quit.")
    
    while True:
        for ip, name in ALL_DEVICES.items():
            payload = simulate_device(ip)
            message = json.dumps(payload).encode('utf-8')
            
            try:
                sock.sendto(message, (UDP_IP, UDP_PORT))
                print(f"[Sim] Sent sensor update for {name} ({ip})")
            except Exception as e:
                print(f"Failed to send UDP packet: {e}")
                
        print("Waiting 5 seconds for the next cycle...\n")
        time.sleep(5)

if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        print("\nSimulator stopped.")
