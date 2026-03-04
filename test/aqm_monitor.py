from pymodbus.client import ModbusTcpClient
import matplotlib.pyplot as plt
import matplotlib.animation as animation
from datetime import datetime
import collections
import os

# ==========================================
# CONNECTION CONFIGURATION
# ==========================================
ESP32_IP = '192.168.0.147'  
MODBUS_PORT = 502
START_REG = 0
COUNT_REG = 25
CSV_FILENAME = "aqm_data_log.csv"

# ==========================================
# DATA STORAGE FOR GRAPHS (Last 60 points)
# ==========================================
MAX_POINTS = 60
time_x = collections.deque(maxlen=MAX_POINTS)

# Gases & Resistances
data_so2 = collections.deque(maxlen=MAX_POINTS)
data_h2s = collections.deque(maxlen=MAX_POINTS)
data_res_co  = collections.deque(maxlen=MAX_POINTS)
data_res_nh3 = collections.deque(maxlen=MAX_POINTS)
data_res_no2 = collections.deque(maxlen=MAX_POINTS)

# Climate and Hardware
data_temp = collections.deque(maxlen=MAX_POINTS)
data_v3v3 = collections.deque(maxlen=MAX_POINTS)
data_v5   = collections.deque(maxlen=MAX_POINTS)
data_co_v = collections.deque(maxlen=MAX_POINTS)
data_nh3_v = collections.deque(maxlen=MAX_POINTS)
data_no2_v = collections.deque(maxlen=MAX_POINTS)

# Initialize Modbus Client
print(f"Connecting to ESP32 at {ESP32_IP}...")
client = ModbusTcpClient(ESP32_IP, port=MODBUS_PORT)

if client.connect():
    print("Connected successfully!")
else:
    print("Failed to connect to ESP32. Check the IP address.")
    exit()

# Initialize CSV file with header if it doesn't exist
if not os.path.exists(CSV_FILENAME):
    with open(CSV_FILENAME, "w") as f:
        f.write("Timestamp,Uptime_sec,SO2_ppm,H2S_ppm,CO_V,NH3_V,NO2_V,R_CO_kOhm,R_NH3_kOhm,R_NO2_kOhm,Temp_C,V3V3,V5V\n")

# Create figure and subplots
fig, (ax1, ax4, ax2, ax3) = plt.subplots(4, 1, figsize=(10, 11))
fig.canvas.manager.set_window_title('AQM Dashboard - Live Data & Logging')

def update_plot(frame):
    result = client.read_input_registers(START_REG, count=COUNT_REG)
    
    if not result.isError():
        regs = result.registers
        
        # --- DECODE DATA ---
        status_word = regs[0]
        so2_ppm = regs[8] / 100.0
        h2s_ppm = regs[9] / 100.0
        co_mv   = regs[10]
        nh3_mv  = regs[11]
        no2_mv  = regs[12]
        temp_c  = regs[15] / 10.0
        v3v3_mv = regs[13]
        v3v3_volts = v3v3_mv / 1000.0
        v5_volts   = regs[14] / 1000.0
        
        # --- CALCULATE SENSOR RESISTANCES ---
        R_CO_LOAD  = 390000
        R_NH3_LOAD = 120000
        R_NO2_LOAD = 3900
        
        # Avoid division by zero
        co_res  = R_CO_LOAD * (co_mv / (v3v3_mv - co_mv))
        nh3_res = R_NH3_LOAD * (nh3_mv / (v3v3_mv - nh3_mv))
        no2_res = R_NO2_LOAD * (no2_mv / (v3v3_mv - no2_mv))

        uptime_sec = (regs[23] << 16) | regs[24]
        wifi_ok = (status_word & (1 << 2)) != 0
        relay_on = (status_word & (1 << 1)) != 0
        
        # --- STORE DATA ---
        now = datetime.now()
        now_str = now.strftime('%H:%M:%S')
        full_timestamp = now.strftime('%Y-%m-%d %H:%M:%S')
        
        time_x.append(now_str)
        data_so2.append(so2_ppm)
        data_h2s.append(h2s_ppm)
        data_res_co.append(co_res / 1000.0) 
        data_res_nh3.append(nh3_res / 1000.0)
        data_res_no2.append(no2_res / 1000.0)
        data_temp.append(temp_c)
        data_v3v3.append(v3v3_volts)
        data_v5.append(v5_volts)
        data_co_v.append(co_mv / 1000.0)
        data_nh3_v.append(nh3_mv / 1000.0)
        data_no2_v.append(no2_mv / 1000.0)
        
        # --- CSV LOGGING ---
        with open(CSV_FILENAME, "a") as f:
            f.write(f"{full_timestamp},{uptime_sec},{so2_ppm:.2f},{h2s_ppm:.2f},"
                    f"{co_mv/1000.0:.3f},{nh3_mv/1000.0:.3f},{no2_mv/1000.0:.3f},"
                    f"{co_res/1000.0:.2f},{nh3_res/1000.0:.2f},{no2_res/1000.0:.2f},"
                    f"{temp_c:.1f},{v3v3_volts:.3f},{v5_volts:.3f}\n")
        
        # --- DRAW GRAPHS ---
        ax1.clear(); ax2.clear(); ax3.clear(); ax4.clear()
        
        # Plot 1: Gases
        ax1.plot(time_x, data_so2, label="SO2", color='blue', linestyle='dotted')
        ax1.plot(time_x, data_h2s, label="H2S", color='green', linestyle='dotted')
        ax1.set_title(f"Gas Concentrations (PPM) | Wi-Fi: {'OK' if wifi_ok else 'DISCONNECTED'}")
        ax1.set_ylabel("PPM")
        ax1.legend(loc="upper left")
        ax1.grid(True)
        ax1.set_xticks([]) 
        
       # Plot 4: Sensor Resistances (kOhms)
        # 1. Create the primary axis for CO and NH3 (Left side)
        line1 = ax4.plot(time_x, data_res_co,  label="R_CO",  color='purple', linestyle='dotted')
        line2 = ax4.plot(time_x, data_res_nh3, label="R_NH3", color='cyan', linestyle='dotted')
        ax4.set_title("MiCS Sensor Resistances (kΩ)")
        ax4.set_ylabel("Resistance (kΩ)")
        ax4.grid(True)
        ax4.set_xticks([])

        # 2. Create a twin axis for NO2 (Right side)
        # Check if we already have a twin axis, if not, create it
        if not hasattr(ax4, 'twin_ax'):
            ax4.twin_ax = ax4.twinx()
        
        ax4.twin_ax.clear() # Clear the twin axis for the new frame
        line3 = ax4.twin_ax.plot(time_x, data_res_no2, label="R_NO2", color='brown', linestyle='dotted')
        # ax4.twin_ax.set_ylabel("Resistance (Ω)")
        
        # Merge legends from both axes into one
        lines = line1 + line2
        labels = [l.get_label() for l in lines]
        ax4.legend(lines, labels, loc="upper left", ncol=3)
        ax4.twin_ax.legend(line3, [l.get_label() for l in line3], loc="upper right")
        ax4.twin_ax.set_xticks([])

        # Plot 2: Temp
        ax2.plot(time_x, data_temp, label="Temperature", color='orange', linestyle='dotted')
        ax2.set_ylabel("°C")
        ax2.legend(loc="upper left")
        ax2.grid(True)
        ax2.set_xticks([]) 
        
        # Plot 3: Voltages
        ax3.plot(time_x, data_v3v3, label="3.3V Rail", color='teal', linestyle='dotted')
        ax3.plot(time_x, data_v5,   label="5.0V Rail", color='red', linestyle='dotted')
        ax3.plot(time_x, data_co_v, label="CO Sig", color='purple', linestyle='dotted')
        ax3.plot(time_x, data_nh3_v, label="NH3 Sig", color='cyan', linestyle='dotted')
        ax3.plot(time_x, data_no2_v, label="NO2 Sig", color='brown', linestyle='dotted')
        ax3.set_ylabel("Voltage (V)")
        ax3.set_ylim([0.0, 5.5]) 
        ax3.legend(loc="upper left", ncol=2, fontsize='small')
        ax3.grid(True)
        ax3.tick_params(axis='x', rotation=45) 
        
        plt.tight_layout()
        plt.subplots_adjust(left=0.1, right=0.9, top=0.95, bottom=0.1, hspace=0.3)  
    else:
        print(f"[{datetime.now().strftime('%H:%M:%S')}] Modbus Error!")

# Animation setup
ani = animation.FuncAnimation(fig, update_plot, interval=1000, cache_frame_data=False)

plt.show()
client.close()